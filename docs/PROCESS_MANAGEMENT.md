# KERNOX Linux Process Management Subsystem (Phase 4A.2)

## 1. Overview and Architecture

The KERNOX Linux Process Management Subsystem (`kernox::system::ProcessManager`) provides robust, production-grade POSIX process execution, sandboxed resource control, lifecycle monitoring, timeout enforcement, and `/proc/<pid>/stat` telemetry.

It builds directly on KERNOX's foundational RAII primitives:
- `kernox::core::UniqueFd`: RAII encapsulation of POSIX file descriptors, ensuring zero leaks and automatic cleanup across fork and exec barriers.
- `kernox::core::Result<T, Error>`: Type-safe, exception-free error propagation with structured POSIX `errno` preservation.
- `kernox::core::ChildProcess`: Low-level POSIX process primitives.

```
+-------------------------------------------------------------------------+
|                           KERNOX CLI / API                             |
|          kernox process run ...        kernox process stat <pid>        |
+------------------------------------+------------------------------------+
                                     |
                                     v
+-------------------------------------------------------------------------+
|                  kernox::system::ProcessManager                         |
|   - ProcessConfig (executable, args, env, timeout, grace, limits)       |
|   - ProcessLimits::validate() (soft <= hard, sanity checks)             |
|   - Parent-child error pipe protocol (pipe2 with O_CLOEXEC)             |
|   - Per-child resource accounting via wait4(2)                          |
+-------------------+--------------------------------+--------------------+
                    |                                |
       (Fork & Exec Pipeline)                        | (/proc telemetry)
                    v                                v
+---------------------------------------+  +------------------------------+
|             Child Process             |  |   ProcessParser::parse_stat  |
| 1. Apply setrlimit(2) (CPU/AS/NOFILE) |  |   - Parenthesis-safe comm    |
| 2. Error pipe write on failure        |  |   - utime, stime, vsize, rss |
| 3. Direct execvp / execvpe (no shell) |  +------------------------------+
+---------------------------------------+
```

---

## 2. Safe Executable and Argument Handling

### Direct `execvp` / `execvpe` (No Shell Concatenation)
Process arguments are passed directly to `execvp` or `execvpe` as a `char* const argv[]` array prepared in the parent before `fork()`.
- **No `/bin/sh -c`:** User commands are never concatenated into a string or interpreted by a command shell.
- **Metacharacter Safety:** Arguments containing shell metacharacters (`;`, `&`, `|`, `>`, `<`, `$`, `` ` ``, `*`) are treated strictly as literal data payload without risk of command injection.
- **Space Safety:** Executable paths and argument elements with spaces are passed intact without truncation or word-splitting.

### Explicit Environment Handling
- When `ProcessConfig::environment` is empty, the child inherits the parent's environment via `::execvp`.
- When explicit environment entries are provided (`KEY=VALUE`), they are passed directly to `::execve` or `::execvpe`.

---

## 3. Parent-Child Error Reporting Protocol

A common vulnerability in POSIX process launchers is the inability to distinguish between a failed `exec` (e.g., non-existent binary or permission denied) and a target binary that ran and returned an exit code (e.g. 127).

KERNOX solves this using an internal synchronization pipe:
1. Before `fork()`, parent creates a unidirectional pipe with `O_CLOEXEC`.
2. The child inherits the write descriptor.
3. If `setrlimit` or `exec` fails, the child writes a structured `ChildErrorPayload` containing the failure stage and `errno`, then calls `_exit(127)`.
4. If `exec` succeeds, the write descriptor is automatically closed by the kernel via `FD_CLOEXEC`.
5. The parent reads from the read descriptor:
   - **0 bytes read (EOF):** Successful launch. The parent proceeds to monitoring.
   - **`sizeof(ChildErrorPayload)` bytes read:** Setup/exec failure in child. The parent reaps the child process immediately and returns an appropriate `ErrorCode` (`NotFound` for `ENOENT`, `PermissionDenied` for `EACCES`, etc.).

---

## 4. Timeout Enforcement and Escalation

When `ProcessConfig::timeout` is configured:
1. **Wall-Clock Monitoring:** The parent polls the child using non-blocking `wait4(pid, &raw_status, WNOHANG, &rusage)` with short sleeps (5ms).
2. **Normal Termination:** If the child terminates within the timeout window, `timed_out` is `false` and resource accounting is recorded.
3. **Graceful Escalation (`SIGTERM`):** If the timeout expires, the parent marks `timed_out = true` and sends `SIGTERM`.
4. **Grace Period:** The parent monitors the child for a configurable grace period (`config.grace_period`, default: 200ms). Setting `grace_period = 0ms` causes immediate escalation to `SIGKILL` after the initial `SIGTERM` attempt if the child does not exit immediately.
5. **Forced Escalation (`SIGKILL`):** If the child does not exit before the grace period expires, `SIGKILL` is issued.
6. **Safe Bounded Reaping:** The child is reaped via `reap_child` using bounded `wait4` polling (avoiding unbounded hangs). Once reaped, the PID is never signaled again by `ProcessManager`, guarding against PID reuse races under the documented single-owner contract.

---

## 5. POSIX Resource Limits (`setrlimit`)

The following resource limits can be applied to the child prior to execution:

| Limit | POSIX Resource | CLI Option | Description |
|---|---|---|---|
| CPU Time | `RLIMIT_CPU` | `--cpu-limit <sec>` | CPU time limit in seconds; kernel sends `SIGXCPU` then `SIGKILL` |
| Address Space | `RLIMIT_AS` | `--as-limit <bytes>` | Maximum total virtual memory size in bytes |
| Open Descriptors | `RLIMIT_NOFILE` | `--nofile-limit <num>` | Maximum number of open file descriptors |
| File Size | `RLIMIT_FSIZE` | `--fsize-limit <bytes>` | Maximum file size created in bytes; sends `SIGXFSZ` |

### Validation Invariants
- Each limit supports independent `soft` and `hard` settings (`LimitSetting`).
- `ProcessLimits::validate()` rejects configurations where `soft > hard`.
- Rejects zero values where invalid (e.g., `max_open_files` soft limit cannot be 0).
- If `setrlimit` fails in the child (e.g. unprivileged attempt to raise hard limit), the exact error is reported back to the parent via the error pipe.

### Security Disclaimer: Why Limits Are Not a Complete Sandbox
> [!WARNING]
> While `setrlimit` restricts CPU, memory, descriptors, and file sizes, it **does not constitute a security boundary or containment sandbox**. It does not provide namespace isolation (network, mount, PID, IPC), chroot/pivot_root filesystem virtualization, seccomp-bpf syscall filtering, or capability dropping. Processes still share the host filesystem, network stack, and kernel attack surface.

---

## 6. Per-Child Resource Usage (`wait4`)

Resource metrics are gathered using Linux `wait4(2)`. Unlike cumulative `getrusage(RUSAGE_CHILDREN)` (which sums all dead children across the entire parent lifetime), `wait4` with the specific child PID yields isolated, per-process telemetry:

| Metric | Source Field | Unit | Meaning |
|---|---|---|---|
| User CPU Time | `ru_utime` | Microseconds (us) | Time spent executing in user mode |
| System CPU Time | `ru_stime` | Microseconds (us) | Time spent executing in kernel mode |
| Maximum RSS | `ru_maxrss` | Kilobytes (KB) | Peak resident set size |
| Minor Page Faults | `ru_minflt` | Count | Page reclaims without disk I/O |
| Major Page Faults | `ru_majflt` | Count | Page faults requiring disk I/O |
| Voluntary Switches | `ru_nvcsw` | Count | Context switches caused by blocking (I/O, locks) |
| Involuntary Switches | `ru_nivcsw` | Count | Context switches caused by time-slice expiration |

---

## 7. Linux `/proc/<pid>/stat` Parsing

`ProcessParser::parse_proc_pid_stat` parses process status information from `/proc/<pid>/stat`.

### Handling Comm Names with Spaces and Parentheses
According to `proc(5)`, field 2 (`comm`) is enclosed in parentheses and may contain spaces or nested parentheses (e.g., `1234 (bash (sub) script) S ...`).
- **Parsing Invariant:** The parser identifies the first `'('` and the *last* `')'` in the line.
- Text between the first `'('` and last `')'` is extracted as `comm`.
- Fields after the last `')'` are tokenized to extract state, PPID, utime, stime, vsize, and RSS.

---

## 8. CLI Usage Examples

### Running an Executable
```bash
# Basic run with wait4 resource accounting
kernox process run /bin/echo "KERNOX process manager operational"

# Execution with wall-clock timeout (terminates via SIGTERM/SIGKILL if exceeded)
kernox process run --timeout 500 /bin/sleep 2

# Execution with CPU and memory limits
kernox process run --cpu-limit 5 --as-limit 67108864 /bin/ls -la /tmp

# Using '--' to separate options from arguments with hyphens
kernox process run -- /bin/ls -la
```

### Inspecting Process Telemetry
```bash
# Inspect init / systemd
kernox process stat 1

# Inspect current process
kernox process stat $$
```

---

## 9. Limitations and Ownership Contract

### Single-Owner Reaping Invariant
`kernox::system::ProcessManager` assumes exclusive ownership over the lifecycle and reaping of every child process it spawns. In the standard Linux POSIX process model, child processes belong to the entire host process rather than individual threads. Consequently, the following constraints are required of embedding environments:

1. **No Broad Reaping:** Host applications or external libraries must not concurrently reap children using broad calls such as `wait(NULL)`, `waitpid(-1, ...)`, or `wait4(-1, ...)`. Doing so can intercept and reap KERNOX child exit statuses prematurely.
2. **No Automatic Reaping via `SIGCHLD`:** Host applications must not set `SIGCHLD` handling to `SIG_IGN` or configure `SA_NOCLDWAIT`. These settings instruct the Linux kernel to discard child zombie states automatically, preventing exit status collection and causing subsequent `wait4` calls to fail with `ECHILD`.
3. **PID-Reuse Signaling Hazards:** Under standard POSIX integer PIDs, if a competing thread or signal handler reaps a child process outside of `ProcessManager`, a time-of-check to time-of-use (TOCTOU) window exists where the original PID could be recycled by the operating system before `ProcessManager` escalates signals (e.g., `SIGKILL`). `ProcessManager` mitigates this by strictly checking `wait4(pid, ..., WNOHANG)` before signaling and only issuing `kill` when the child is positively confirmed alive and waitable. However, if external code reaps the child during that window, the PID could be reassigned.

### Future Architectural Evolution: Linux `pidfd`
In contemporary Linux environments (kernel 5.3+), file-descriptor-based process handles (`pidfd_open(2)`, `pidfd_send_signal(2)`, and `waitid(P_PIDFD, ...)`) provide structural immunity against PID recycling races. Because a `pidfd` holds an immutable reference to a specific process instance, signaling through `pidfd_send_signal` cannot affect a recycled process even if the original target has already been reaped.

While KERNOX targets modern Linux systems (Ubuntu 24.04 LTS), the current implementation relies strictly on standard POSIX `pid_t` and `wait4` semantics. Linux `pidfd` handles may be evaluated in a future architectural revision if concurrent external reaping or multi-threaded embedding support becomes a formalized project requirement. Today, compliance with the single-owner contract is the enforced invariant.

