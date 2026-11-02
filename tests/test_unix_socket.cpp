#include "kernox/core/result.hpp"
#include "kernox/core/unique_fd.hpp"
#include "kernox/modules/system/unix_socket.hpp"
#include "test_harness.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace kernox;
using namespace kernox::system;

namespace {

/**
 * @brief Bounded waitpid helper to guarantee automated tests never hang indefinitely.
 *        If the child does not exit within the timeout, escalates to SIGKILL and reaps.
 */
bool bounded_waitpid(
    pid_t pid,
    int* status_out,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(3000)) {
    auto start = std::chrono::steady_clock::now();
    int status = 0;
    while (std::chrono::steady_clock::now() - start < timeout) {
        pid_t wr = 0;
        do {
            wr = ::wait4(pid, &status, WNOHANG, nullptr);
        } while (wr < 0 && errno == EINTR);

        if (wr > 0) {
            if (status_out != nullptr) {
                *status_out = status;
            }
            return true;
        }
        if (wr < 0 && errno == ECHILD) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // Timeout: forcibly kill child to prevent zombie
    ::kill(pid, SIGKILL);
    do {
        ::waitpid(pid, &status, 0);
    } while (errno == EINTR);
    return false;
}

struct PipeHandshake {
    int fds[2]{-1, -1};

    PipeHandshake() {
        if (::pipe(fds) != 0) {
            std::cerr << "pipe() failed in test handshake\n";
        }
    }

    ~PipeHandshake() {
        if (fds[0] >= 0) ::close(fds[0]);
        if (fds[1] >= 0) ::close(fds[1]);
    }

    void notify() {
        char b = 1;
        ssize_t rc = ::write(fds[1], &b, 1);
        (void)rc;
    }

    void wait() {
        char b = 0;
        ssize_t rc = ::read(fds[0], &b, 1);
        (void)rc;
    }

    void close_read() {
        if (fds[0] >= 0) { ::close(fds[0]); fds[0] = -1; }
    }

    void close_write() {
        if (fds[1] >= 0) { ::close(fds[1]); fds[1] = -1; }
    }
};

} // namespace

int main() {
    std::cout << "=========================================================\n";
    std::cout << "  KERNOX: UNIX Domain Socket IPC Test Suite              \n";
    std::cout << "=========================================================\n\n";

    // ------------------------------------------------------------------------
    // Test 1: Stream Socket Client-Server Communication
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 1: Stream Socket Client-Server Communication...\n";
        const std::string sock_path = "/tmp/kernox_test_stream_" + std::to_string(::getpid()) + ".sock";

        auto server_res = UnixSocket::bind_and_listen(sock_path);
        KERNOX_ASSERT_TRUE(server_res.is_ok());
        UnixSocket server = std::move(server_res.value());
        KERNOX_ASSERT_TRUE(server.is_valid());
        KERNOX_ASSERT_TRUE(server.is_bound_owner());

        PipeHandshake child_ready;
        pid_t child = ::fork();
        KERNOX_ASSERT_TRUE(child >= 0);

        if (child == 0) {
            child_ready.close_read();
            auto client_res = UnixSocket::connect(sock_path, std::chrono::milliseconds(2000));
            if (!client_res.is_ok()) ::_exit(1);
            UnixSocket client = std::move(client_res.value());

            // Client sends a request
            if (client.send_string("PING_KERNOX_STREAM").is_err()) ::_exit(2);

            // Client receives response
            auto resp_res = client.receive_string(128, std::chrono::milliseconds(2000));
            if (!resp_res.is_ok() || resp_res.value() != "PONG_KERNOX_STREAM") ::_exit(3);

            child_ready.notify();
            ::_exit(0);
        }

        child_ready.close_write();

        // Server accepts connection
        auto conn_res = server.accept(std::chrono::milliseconds(3000));
        KERNOX_ASSERT_TRUE(conn_res.is_ok());
        UnixSocket conn = std::move(conn_res.value());

        // Server receives message
        auto msg_res = conn.receive_string(128, std::chrono::milliseconds(2000));
        KERNOX_ASSERT_TRUE(msg_res.is_ok());
        KERNOX_ASSERT_EQ(msg_res.value(), "PING_KERNOX_STREAM");

        // Server sends echo reply
        KERNOX_ASSERT_TRUE(conn.send_string("PONG_KERNOX_STREAM").is_ok());

        child_ready.wait();
        int st = 0;
        KERNOX_ASSERT_TRUE(bounded_waitpid(child, &st));
        KERNOX_ASSERT_TRUE(WIFEXITED(st) && WEXITSTATUS(st) == 0);

        std::cout << "  [PASS] Test 1: Full-duplex stream echo communication verified\n";
    }

    // ------------------------------------------------------------------------
    // Test 2: Datagram Communication (Message Boundary Preservation)
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 2: Datagram Communication (Preserving Boundaries)...\n";
        const std::string srv_path = "/tmp/kernox_test_dgram_srv_" + std::to_string(::getpid()) + ".sock";
        const std::string cli_path = "/tmp/kernox_test_dgram_cli_" + std::to_string(::getpid()) + ".sock";

        auto srv_res = UnixSocket::bind_datagram(srv_path);
        KERNOX_ASSERT_TRUE(srv_res.is_ok());
        UnixSocket srv = std::move(srv_res.value());
        KERNOX_ASSERT_EQ(srv.type(), SocketType::Datagram);

        PipeHandshake child_ready;
        pid_t child = ::fork();
        KERNOX_ASSERT_TRUE(child >= 0);

        if (child == 0) {
            child_ready.close_read();
            auto cli_res = UnixSocket::bind_datagram(cli_path);
            if (!cli_res.is_ok()) ::_exit(1);
            UnixSocket cli = std::move(cli_res.value());

            // Send two distinct datagrams
            if (cli.send_to_string(srv_path, "DGRAM_PACKET_ONE").is_err()) ::_exit(2);
            if (cli.send_to_string(srv_path, "DGRAM_PACKET_TWO").is_err()) ::_exit(3);

            child_ready.notify();
            ::_exit(0);
        }

        child_ready.close_write();
        child_ready.wait();

        // Server receives datagram 1
        auto pkt1_res = srv.receive_from_string(256, std::chrono::milliseconds(2000));
        KERNOX_ASSERT_TRUE(pkt1_res.is_ok());
        KERNOX_ASSERT_EQ(pkt1_res.value().first, "DGRAM_PACKET_ONE");
        KERNOX_ASSERT_EQ(pkt1_res.value().second, cli_path);

        // Server receives datagram 2: confirms boundaries were strictly preserved!
        auto pkt2_res = srv.receive_from_string(256, std::chrono::milliseconds(2000));
        KERNOX_ASSERT_TRUE(pkt2_res.is_ok());
        KERNOX_ASSERT_EQ(pkt2_res.value().first, "DGRAM_PACKET_TWO");
        KERNOX_ASSERT_EQ(pkt2_res.value().second, cli_path);

        int st = 0;
        KERNOX_ASSERT_TRUE(bounded_waitpid(child, &st));
        KERNOX_ASSERT_TRUE(WIFEXITED(st) && WEXITSTATUS(st) == 0);

        std::cout << "  [PASS] Test 2: Datagram message boundaries and sender path preserved\n";
    }

    // ------------------------------------------------------------------------
    // Test 3: Multiple Messages & Message Integrity
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 3: Multiple Stream Messages & Byte Integrity...\n";
        const std::string sock_path = "/tmp/kernox_test_multi_" + std::to_string(::getpid()) + ".sock";

        auto server = UnixSocket::bind_and_listen(sock_path);
        KERNOX_ASSERT_TRUE(server.is_ok());

        pid_t child = ::fork();
        KERNOX_ASSERT_TRUE(child >= 0);

        constexpr size_t NUM_MSGS = 100;
        constexpr size_t MSG_SIZE = 128;

        if (child == 0) {
            auto client = UnixSocket::connect(sock_path, std::chrono::milliseconds(2000));
            if (!client.is_ok()) ::_exit(1);

            for (size_t i = 0; i < NUM_MSGS; ++i) {
                std::vector<uint8_t> payload(MSG_SIZE, static_cast<uint8_t>(i & 0xFF));
                if (client.value().send_all(payload.data(), payload.size()).is_err()) {
                    ::_exit(2);
                }
            }
            client.value().shutdown(ShutdownHow::Write);
            ::_exit(0);
        }

        auto conn_res = server.value().accept(std::chrono::milliseconds(3000));
        KERNOX_ASSERT_TRUE(conn_res.is_ok());
        UnixSocket conn = std::move(conn_res.value());

        std::vector<uint8_t> total_received;
        std::vector<uint8_t> chunk(256);

        while (true) {
            auto rec_res = conn.receive(chunk.data(), chunk.size(), std::chrono::milliseconds(2000));
            KERNOX_ASSERT_TRUE(rec_res.is_ok());
            if (rec_res.value() == 0) {
                break; // EOF
            }
            total_received.insert(total_received.end(), chunk.begin(), chunk.begin() + static_cast<ptrdiff_t>(rec_res.value()));
        }

        KERNOX_ASSERT_EQ(total_received.size(), NUM_MSGS * MSG_SIZE);

        // Verify content integrity
        for (size_t i = 0; i < NUM_MSGS; ++i) {
            for (size_t j = 0; j < MSG_SIZE; ++j) {
                uint8_t expected = static_cast<uint8_t>(i & 0xFF);
                uint8_t actual = total_received[(i * MSG_SIZE) + j];
                KERNOX_ASSERT_EQ(actual, expected);
            }
        }

        int st = 0;
        KERNOX_ASSERT_TRUE(bounded_waitpid(child, &st));
        KERNOX_ASSERT_TRUE(WIFEXITED(st) && WEXITSTATUS(st) == 0);

        std::cout << "  [PASS] Test 3: 100 consecutive stream messages transferred with 100% byte integrity\n";
    }

    // ------------------------------------------------------------------------
    // Test 4: Peer Credential Inspection (SO_PEERCRED)
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 4: Peer Credential Inspection (SO_PEERCRED)...\n";
        const std::string sock_path = "/tmp/kernox_test_cred_" + std::to_string(::getpid()) + ".sock";

        auto server = UnixSocket::bind_and_listen(sock_path);
        KERNOX_ASSERT_TRUE(server.is_ok());

        PipeHandshake child_ready;
        pid_t child = ::fork();
        KERNOX_ASSERT_TRUE(child >= 0);

        if (child == 0) {
            child_ready.close_read();
            auto client = UnixSocket::connect(sock_path, std::chrono::milliseconds(2000));
            if (!client.is_ok()) ::_exit(1);

            child_ready.notify();
            // Wait until parent acknowledges
            char b = 0;
            auto rec = client.value().receive(&b, 1, std::chrono::milliseconds(2000));
            (void)rec;
            ::_exit(0);
        }

        child_ready.close_write();
        child_ready.wait();

        auto conn = server.value().accept(std::chrono::milliseconds(2000));
        KERNOX_ASSERT_TRUE(conn.is_ok());

        auto creds_res = conn.value().get_peer_credentials();
        KERNOX_ASSERT_TRUE(creds_res.is_ok());
        const auto& creds = creds_res.value();

        // Verify credentials match child process attributes
        KERNOX_ASSERT_EQ(creds.pid, child);
        KERNOX_ASSERT_EQ(creds.uid, ::getuid());
        KERNOX_ASSERT_EQ(creds.gid, ::getgid());

        // Wake and finish child
        char b = 1;
        KERNOX_ASSERT_TRUE(conn.value().send(&b, 1).is_ok());

        int st = 0;
        KERNOX_ASSERT_TRUE(bounded_waitpid(child, &st));
        KERNOX_ASSERT_TRUE(WIFEXITED(st) && WEXITSTATUS(st) == 0);

        std::cout << "  [PASS] Test 4: SO_PEERCRED verified child PID (" << creds.pid
                  << "), UID (" << creds.uid << "), GID (" << creds.gid << ")\n";
    }

    // ------------------------------------------------------------------------
    // Test 5: File Descriptor Passing (SCM_RIGHTS)
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 5: File Descriptor Transfer (SCM_RIGHTS)...\n";
        const std::string sock_path = "/tmp/kernox_test_scm_" + std::to_string(::getpid()) + ".sock";

        auto server = UnixSocket::bind_and_listen(sock_path);
        KERNOX_ASSERT_TRUE(server.is_ok());

        // Create an anonymous pipe; parent will pass read-end to child across socket!
        int pipe_fds[2]{-1, -1};
        KERNOX_ASSERT_EQ(::pipe(pipe_fds), 0);
        core::UniqueFd pipe_read(pipe_fds[0]);
        core::UniqueFd pipe_write(pipe_fds[1]);

        PipeHandshake child_ready;
        pid_t child = ::fork();
        KERNOX_ASSERT_TRUE(child >= 0);

        if (child == 0) {
            child_ready.close_read();
            pipe_read.reset(-1);
            pipe_write.reset(-1);

            auto client = UnixSocket::connect(sock_path, std::chrono::milliseconds(2000));
            if (!client.is_ok()) ::_exit(1);

            child_ready.notify();

            // Receive transmitted file descriptor from parent
            char tag[32]{};
            size_t bytes_read = 0;
            auto recv_fd_res = client.value().receive_fd(tag, sizeof(tag), &bytes_read, std::chrono::milliseconds(2000));
            if (!recv_fd_res.is_ok()) ::_exit(2);
            core::UniqueFd received_pipe_fd = std::move(recv_fd_res.value());

            if (!received_pipe_fd.is_valid()) ::_exit(3);
            if (std::string_view(tag, bytes_read) != "PIPE_HANDLE") ::_exit(4);

            // Read through the transmitted file descriptor!
            char secret_buf[64]{};
            ssize_t s_rc = ::read(received_pipe_fd.get(), secret_buf, sizeof(secret_buf));
            if (s_rc <= 0 || std::string_view(secret_buf, static_cast<size_t>(s_rc)) != "KERNOX_FD_SECRET_42") {
                ::_exit(5);
            }

            ::_exit(0);
        }

        child_ready.close_write();
        child_ready.wait();

        auto conn = server.value().accept(std::chrono::milliseconds(2000));
        KERNOX_ASSERT_TRUE(conn.is_ok());

        // Send the read-end descriptor to child
        const char tag[] = "PIPE_HANDLE";
        auto send_res = conn.value().send_fd(pipe_read.get(), tag, sizeof(tag) - 1, std::chrono::milliseconds(2000));
        KERNOX_ASSERT_TRUE(send_res.is_ok());

        // Parent closes its read end; writes secret payload into write end
        pipe_read.reset(-1);
        const char secret[] = "KERNOX_FD_SECRET_42";
        ssize_t w_rc = ::write(pipe_write.get(), secret, sizeof(secret) - 1);
        KERNOX_ASSERT_EQ(w_rc, static_cast<ssize_t>(sizeof(secret) - 1));

        int st = 0;
        KERNOX_ASSERT_TRUE(bounded_waitpid(child, &st));
        KERNOX_ASSERT_TRUE(WIFEXITED(st) && WEXITSTATUS(st) == 0);

        std::cout << "  [PASS] Test 5: SCM_RIGHTS file descriptor transmitted and successfully read by child\n";
    }

    // ------------------------------------------------------------------------
    // Test 6: Invalid Socket Paths (Boundary Validation)
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 6: Invalid Socket Paths & Defensive Validation...\n";

        // 1. Empty path
        KERNOX_ASSERT_TRUE(UnixSocket::validate_path("").is_err());
        auto empty_bind = UnixSocket::create_stream().value().bind("");
        KERNOX_ASSERT_TRUE(empty_bind.is_err());
        KERNOX_ASSERT_EQ(empty_bind.error().code, core::ErrorCode::InvalidArgument);

        // 2. Excessively long path (> MAX_PATH_LENGTH = 107 chars)
        std::string long_path(110, 'a');
        KERNOX_ASSERT_TRUE(UnixSocket::validate_path(long_path).is_err());
        auto long_bind = UnixSocket::create_stream().value().bind(long_path);
        KERNOX_ASSERT_TRUE(long_bind.is_err());
        KERNOX_ASSERT_EQ(long_bind.error().code, core::ErrorCode::InvalidArgument);

        // 3. Embedded null characters
        std::string null_path = "/tmp/bad\0socket";
        std::string_view null_view(null_path.data(), 15);
        KERNOX_ASSERT_TRUE(UnixSocket::validate_path(null_view).is_err());

        std::cout << "  [PASS] Test 6: Empty, oversized, and null-embedded paths safely rejected\n";
    }

    // ------------------------------------------------------------------------
    // Test 7: Connection Refusal and Missing Paths
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 7: Connection Refusal & Missing Paths...\n";

        // Connect to non-existent path
        auto conn_missing = UnixSocket::connect("/tmp/kernox_nonexistent_1234567.sock");
        KERNOX_ASSERT_TRUE(conn_missing.is_err());
        KERNOX_ASSERT_EQ(conn_missing.error().code, core::ErrorCode::NotFound);

        // Timed connect to non-existent path
        auto timed_missing = UnixSocket::connect("/tmp/kernox_nonexistent_1234567.sock", std::chrono::milliseconds(50));
        KERNOX_ASSERT_TRUE(timed_missing.is_err());
        KERNOX_ASSERT_EQ(timed_missing.error().code, core::ErrorCode::NotFound);

        std::cout << "  [PASS] Test 7: Missing socket connection safely returned NotFound\n";
    }

    // ------------------------------------------------------------------------
    // Test 8: Socket Cleanup and Resource Ownership
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 8: Socket Cleanup & Path Ownership Semantics...\n";
        const std::string sock_path = "/tmp/kernox_test_clean_" + std::to_string(::getpid()) + ".sock";

        struct stat st{};
        // Scope 1: Server binds path, then destructs. File must be automatically unlinked!
        {
            auto server = UnixSocket::bind_and_listen(sock_path);
            KERNOX_ASSERT_TRUE(server.is_ok());
            KERNOX_ASSERT_TRUE(server.value().is_bound_owner());

            // Socket file must exist on filesystem
            KERNOX_ASSERT_EQ(::stat(sock_path.c_str(), &st), 0);
        }

        // After server destruction, socket file must NO LONGER exist!
        KERNOX_ASSERT_NE(::stat(sock_path.c_str(), &st), 0);
        KERNOX_ASSERT_EQ(errno, ENOENT);

        // Scope 2: Client connection must NEVER unlink server path on client destruction
        {
            auto server = UnixSocket::bind_and_listen(sock_path);
            KERNOX_ASSERT_TRUE(server.is_ok());

            {
                auto client = UnixSocket::connect(sock_path);
                KERNOX_ASSERT_TRUE(client.is_ok());
                KERNOX_ASSERT_FALSE(client.value().is_bound_owner());
                // Client destructs here
            }

            // Server's path must STILL exist!
            KERNOX_ASSERT_EQ(::stat(sock_path.c_str(), &st), 0);
        }

        // After server destructs, path unlinked
        KERNOX_ASSERT_NE(::stat(sock_path.c_str(), &st), 0);
        KERNOX_ASSERT_EQ(errno, ENOENT);

        std::cout << "  [PASS] Test 8: Path unlinked strictly by owner; client destruction preserved server path\n";
    }

    // ------------------------------------------------------------------------
    // Test 9: Non-blocking and Bounded Timeout Behavior
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 9: Non-blocking & Bounded Timeout Behavior...\n";
        const std::string sock_path = "/tmp/kernox_test_timeout_" + std::to_string(::getpid()) + ".sock";

        auto server = UnixSocket::bind_and_listen(sock_path);
        KERNOX_ASSERT_TRUE(server.is_ok());

        // 1. Accept timeout
        auto start = std::chrono::steady_clock::now();
        auto accept_to = server.value().accept(std::chrono::milliseconds(60));
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);

        KERNOX_ASSERT_TRUE(accept_to.is_err());
        KERNOX_ASSERT_EQ(accept_to.error().code, core::ErrorCode::Timeout);
        KERNOX_ASSERT_TRUE(elapsed.count() >= 40 && elapsed.count() < 1500);

        // 2. Receive timeout on connected pair
        auto pair = UnixSocket::create_pair(SocketType::Stream);
        KERNOX_ASSERT_TRUE(pair.is_ok());

        char buf[16]{};
        start = std::chrono::steady_clock::now();
        auto rec_to = pair.value().first.receive(buf, sizeof(buf), std::chrono::milliseconds(50));
        elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);

        KERNOX_ASSERT_TRUE(rec_to.is_err());
        KERNOX_ASSERT_EQ(rec_to.error().code, core::ErrorCode::Timeout);
        KERNOX_ASSERT_TRUE(elapsed.count() >= 35 && elapsed.count() < 1500);

        std::cout << "  [PASS] Test 9: Accept and receive bounded timeouts verified\n";
    }

    // ------------------------------------------------------------------------
    // Test 10: Move Semantics & Socketpair
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 10: Move Semantics, Ownership Transfer & Socketpair...\n";
        const std::string sock_path = "/tmp/kernox_test_move_" + std::to_string(::getpid()) + ".sock";

        UnixSocket s1;
        KERNOX_ASSERT_FALSE(s1.is_valid());

        {
            auto s2_res = UnixSocket::bind_and_listen(sock_path);
            KERNOX_ASSERT_TRUE(s2_res.is_ok());
            UnixSocket s2 = std::move(s2_res.value());
            KERNOX_ASSERT_TRUE(s2.is_valid());
            KERNOX_ASSERT_TRUE(s2.is_bound_owner());

            // Move assignment
            s1 = std::move(s2);
            KERNOX_ASSERT_FALSE(s2.is_valid());
            KERNOX_ASSERT_FALSE(s2.is_bound_owner());
            KERNOX_ASSERT_TRUE(s1.is_valid());
            KERNOX_ASSERT_TRUE(s1.is_bound_owner());
        }

        // s2 fell out of scope, but s1 holds ownership; file must exist!
        struct stat st{};
        KERNOX_ASSERT_EQ(::stat(sock_path.c_str(), &st), 0);

        // Explicit release
        core::UniqueFd released_fd = s1.release();
        KERNOX_ASSERT_FALSE(s1.is_valid());
        KERNOX_ASSERT_TRUE(released_fd.is_valid());

        // Test anonymous socketpair
        auto pair_res = UnixSocket::create_pair(SocketType::Stream);
        KERNOX_ASSERT_TRUE(pair_res.is_ok());
        auto [sock_a, sock_b] = std::move(pair_res.value());

        KERNOX_ASSERT_TRUE(sock_a.send_string("HELLO_PAIR").is_ok());
        auto rec_res = sock_b.receive_string(64, std::chrono::milliseconds(1000));
        KERNOX_ASSERT_TRUE(rec_res.is_ok());
        KERNOX_ASSERT_EQ(rec_res.value(), "HELLO_PAIR");

        ::unlink(sock_path.c_str());
        std::cout << "  [PASS] Test 10: Move semantics, ownership transfer, and socketpair verified\n";
    }

    std::cout << "\n=========================================================\n";
    std::cout << "  [SUCCESS] All 10 UNIX Domain Socket Tests Passed!      \n";
    std::cout << "=========================================================\n";
    return 0;
}
