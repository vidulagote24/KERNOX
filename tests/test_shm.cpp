#include "test_harness.hpp"
#include "kernox/modules/system/shared_memory.hpp"
#include "kernox/core/unique_fd.hpp"

#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <dirent.h>
#include <iostream>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace kernox;
using namespace kernox::system;

namespace {

// Counts currently open file descriptors for the process
size_t count_open_fds() {
    DIR* dir = ::opendir("/proc/self/fd");
    if (!dir) {
        return 0;
    }
    size_t count = 0;
    struct dirent* entry = nullptr;
    while ((entry = ::readdir(dir)) != nullptr) {
        if (entry->d_name[0] != '.') {
            count++;
        }
    }
    ::closedir(dir);
    return count;
}

// Bounded waitpid helper to prevent tests hanging indefinitely
bool bounded_waitpid(pid_t pid, int* status_out, std::chrono::milliseconds timeout = std::chrono::milliseconds(3000)) {
    auto start = std::chrono::steady_clock::now();
    int status = 0;
    while (std::chrono::steady_clock::now() - start < timeout) {
        pid_t wr = 0;
        do {
            wr = ::waitpid(pid, &status, WNOHANG);
        } while (wr < 0 && errno == EINTR);

        if (wr == pid) {
            if (status_out) *status_out = status;
            return true;
        }
        if (wr < 0 && errno == ECHILD) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // Timeout: kill child to prevent zombie
    ::kill(pid, SIGKILL);
    do {
        ::waitpid(pid, &status, 0);
    } while (errno == EINTR);
    return false;
}

} // namespace

int main() {
    std::cout << "[KERNOX TEST] Starting POSIX Shared Memory Test Suite...\n";

    // ------------------------------------------------------------------------
    // Test 1: Name validation and size checks
    // ------------------------------------------------------------------------
    {
        // Valid names
        KERNOX_ASSERT_TRUE(SharedMemory::validate_name("/kernox_test_shm").is_ok());
        KERNOX_ASSERT_TRUE(SharedMemory::validate_name("/a").is_ok());

        // Empty name
        KERNOX_ASSERT_TRUE(SharedMemory::validate_name("").is_err());
        KERNOX_ASSERT_EQ(SharedMemory::validate_name("").error().code, core::ErrorCode::InvalidArgument);

        // Missing leading slash
        KERNOX_ASSERT_TRUE(SharedMemory::validate_name("kernox_no_slash").is_err());
        KERNOX_ASSERT_EQ(SharedMemory::validate_name("kernox_no_slash").error().code, core::ErrorCode::InvalidArgument);

        // Slash only
        KERNOX_ASSERT_TRUE(SharedMemory::validate_name("/").is_err());

        // Multiple slashes
        KERNOX_ASSERT_TRUE(SharedMemory::validate_name("/kernox/nested/shm").is_err());

        // Name exceeding MAX_NAME_LENGTH
        std::string long_name = "/" + std::string(300, 'x');
        KERNOX_ASSERT_TRUE(SharedMemory::validate_name(long_name).is_err());

        // Zero size creation rejection
        auto shm_zero = SharedMemory::create("/kernox_test_zero", 0);
        KERNOX_ASSERT_TRUE(shm_zero.is_err());
        KERNOX_ASSERT_EQ(shm_zero.error().code, core::ErrorCode::InvalidArgument);

        std::cout << "  [PASS] Test 1: Name and size boundary validation verified\n";
    }

    // ------------------------------------------------------------------------
    // Test 2: Basic Creation, Mapping, and Intra-Process Read/Write
    // ------------------------------------------------------------------------
    {
        const std::string shm_name = "/kernox_test_basic_" + std::to_string(::getpid());
        constexpr size_t shm_size = 4096;

        auto shm_res = SharedMemory::create(shm_name, shm_size);
        KERNOX_ASSERT_TRUE(shm_res.is_ok());

        auto& shm = shm_res.value();
        KERNOX_ASSERT_TRUE(shm.is_valid());
        KERNOX_ASSERT_TRUE(shm.is_mapped());
        KERNOX_ASSERT_TRUE(shm.is_owner());
        KERNOX_ASSERT_EQ(shm.size(), shm_size);
        KERNOX_ASSERT_EQ(shm.name(), shm_name);
        KERNOX_ASSERT_TRUE(shm.data() != nullptr);
        KERNOX_ASSERT_TRUE(shm.fd() >= 0);

        // Write structured data
        const char msg[] = "KERNOX Operating Systems Shared Memory Test";
        std::memcpy(shm.data(), msg, sizeof(msg));

        // Read back and compare
        char read_buf[64]{};
        std::memcpy(read_buf, shm.data(), sizeof(msg));
        KERNOX_ASSERT_EQ(std::string(read_buf), std::string(msg));

        // Typed access
        auto* u32_ptr = shm.as<uint32_t>();
        *u32_ptr = 0xDEADBEEF;
        KERNOX_ASSERT_EQ(*shm.as<uint32_t>(), 0xDEADBEEF);

        std::cout << "  [PASS] Test 2: Creation, mapping, and intra-process read/write verified\n";
    }

    // ------------------------------------------------------------------------
    // Test 3: Existing Object Opening and ReadOnly mode
    // ------------------------------------------------------------------------
    {
        const std::string shm_name = "/kernox_test_open_" + std::to_string(::getpid());
        constexpr size_t shm_size = 2048;

        // Owner creates
        auto owner_res = SharedMemory::create(shm_name, shm_size);
        KERNOX_ASSERT_TRUE(owner_res.is_ok());
        auto& owner = owner_res.value();

        int* int_arr = owner.as<int>();
        int_arr[0] = 42;
        int_arr[1] = 1337;

        // Reader opens existing with auto-detected size (0)
        auto reader_res = SharedMemory::open_existing(shm_name, 0, ShmAccessMode::ReadOnly);
        KERNOX_ASSERT_TRUE(reader_res.is_ok());
        auto& reader = reader_res.value();

        KERNOX_ASSERT_TRUE(reader.is_valid());
        KERNOX_ASSERT_FALSE(reader.is_owner()); // NOT owner
        KERNOX_ASSERT_EQ(reader.size(), shm_size);
        KERNOX_ASSERT_TRUE(reader.access_mode() == ShmAccessMode::ReadOnly);

        const int* reader_arr = reader.as<int>();
        KERNOX_ASSERT_EQ(reader_arr[0], 42);
        KERNOX_ASSERT_EQ(reader_arr[1], 1337);

        // Destroy reader first; verify object still exists for owner
        reader_res = core::Result<SharedMemory>::err(core::ErrorCode::InvalidState, "");
        KERNOX_ASSERT_EQ(int_arr[0], 42);

        // Try opening non-existent object
        auto non_existent = SharedMemory::open_existing("/kernox_non_existent_shm_xyz");
        KERNOX_ASSERT_TRUE(non_existent.is_err());
        KERNOX_ASSERT_EQ(non_existent.error().code, core::ErrorCode::NotFound);

        // Try creating with duplicate name (O_EXCL check)
        auto dup_res = SharedMemory::create(shm_name, shm_size);
        KERNOX_ASSERT_TRUE(dup_res.is_err());
        KERNOX_ASSERT_EQ(dup_res.error().code, core::ErrorCode::InvalidState);

        std::cout << "  [PASS] Test 3: Open existing, auto-detection, and duplicate rejection verified\n";
    }

    // ------------------------------------------------------------------------
    // Test 4: Dynamic Resizing (ftruncate + remap)
    // ------------------------------------------------------------------------
    {
        const std::string shm_name = "/kernox_test_resize_" + std::to_string(::getpid());
        constexpr size_t initial_size = 1024;
        constexpr size_t expanded_size = 4096;

        auto shm_res = SharedMemory::create(shm_name, initial_size);
        KERNOX_ASSERT_TRUE(shm_res.is_ok());
        auto& shm = shm_res.value();

        // Write canary at offset 0
        uint32_t* canary = shm.as<uint32_t>();
        *canary = 0x12345678;

        // Resize to larger size
        auto resize_res = shm.resize(expanded_size);
        KERNOX_ASSERT_TRUE(resize_res.is_ok());
        KERNOX_ASSERT_EQ(shm.size(), expanded_size);

        // Canary data must be preserved across resize
        KERNOX_ASSERT_EQ(*shm.as<uint32_t>(), 0x12345678);

        // Can safely write into newly expanded region
        auto* byte_ptr = static_cast<uint8_t*>(shm.data());
        byte_ptr[expanded_size - 1] = 0xAA;
        KERNOX_ASSERT_EQ(byte_ptr[expanded_size - 1], 0xAA);

        // Reject zero resize
        KERNOX_ASSERT_TRUE(shm.resize(0).is_err());

        // Same size resize is ok no-op
        KERNOX_ASSERT_TRUE(shm.resize(expanded_size).is_ok());

        std::cout << "  [PASS] Test 4: Dynamic resizing and content preservation verified\n";
    }

    // ------------------------------------------------------------------------
    // Test 5: Move Semantics and Ownership Transfer
    // ------------------------------------------------------------------------
    {
        const std::string shm_name = "/kernox_test_move_" + std::to_string(::getpid());
        constexpr size_t shm_size = 1024;

        auto shm1_res = SharedMemory::create(shm_name, shm_size);
        KERNOX_ASSERT_TRUE(shm1_res.is_ok());

        // Move construct
        SharedMemory shm2 = std::move(shm1_res.value());
        KERNOX_ASSERT_TRUE(shm2.is_valid());
        KERNOX_ASSERT_TRUE(shm2.is_owner());
        KERNOX_ASSERT_EQ(shm2.size(), shm_size);

        // Moved-from object is empty and unowned
        KERNOX_ASSERT_FALSE(shm1_res.value().is_valid());
        KERNOX_ASSERT_FALSE(shm1_res.value().is_owner());

        // Move assignment
        SharedMemory shm3;
        shm3 = std::move(shm2);
        KERNOX_ASSERT_TRUE(shm3.is_valid());
        KERNOX_ASSERT_TRUE(shm3.is_owner());
        KERNOX_ASSERT_FALSE(shm2.is_valid());

        std::cout << "  [PASS] Test 5: Move constructor and move assignment verified\n";
    }

    // ------------------------------------------------------------------------
    // Test 6: Separate Process Inter-Process Shared Memory Communication
    // ------------------------------------------------------------------------
    {
        const std::string shm_name = "/kernox_test_proc_" + std::to_string(::getpid());
        constexpr size_t shm_size = 4096;

        struct TestPayload {
            uint32_t magic;
            uint32_t sender_pid;
            char message[128];
        };

        auto shm_res = SharedMemory::create(shm_name, shm_size);
        KERNOX_ASSERT_TRUE(shm_res.is_ok());
        auto& parent_shm = shm_res.value();

        auto* payload = parent_shm.as<TestPayload>();
        payload->magic = 0;
        payload->sender_pid = 0;
        std::memset(payload->message, 0, sizeof(payload->message));

        pid_t pid = ::fork();
        KERNOX_ASSERT_TRUE(pid >= 0);

        if (pid == 0) {
            // Child process: opens existing shared memory and writes payload
            auto child_shm = SharedMemory::open_existing(shm_name, sizeof(TestPayload));
            if (child_shm.is_err()) {
                ::_exit(1);
            }
            auto* child_payload = child_shm.value().as<TestPayload>();
            child_payload->magic = 0xCAFEBABE;
            child_payload->sender_pid = static_cast<uint32_t>(::getpid());
            const char text[] = "Hello from child process via POSIX shared memory!";
            std::memcpy(child_payload->message, text, sizeof(text));
            ::_exit(0);
        }

        // Parent process: bounded wait for child completion
        int child_status = 0;
        bool reaped = bounded_waitpid(pid, &child_status);
        KERNOX_ASSERT_TRUE(reaped);
        KERNOX_ASSERT_TRUE(WIFEXITED(child_status));
        KERNOX_ASSERT_EQ(WEXITSTATUS(child_status), 0);

        // Verify child wrote to shared memory
        KERNOX_ASSERT_EQ(payload->magic, 0xCAFEBABE);
        KERNOX_ASSERT_EQ(payload->sender_pid, static_cast<uint32_t>(pid));
        KERNOX_ASSERT_EQ(std::string(payload->message), "Hello from child process via POSIX shared memory!");

        std::cout << "  [PASS] Test 6: Cross-process shared memory communication verified\n";
    }

    // ------------------------------------------------------------------------
    // Test 7: Abrupt Child Process Termination & Intact Memory
    // ------------------------------------------------------------------------
    {
        const std::string shm_name = "/kernox_test_crash_" + std::to_string(::getpid());
        constexpr size_t shm_size = 1024;

        auto shm_res = SharedMemory::create(shm_name, shm_size);
        KERNOX_ASSERT_TRUE(shm_res.is_ok());
        auto& shm = shm_res.value();

        auto* int_ptr = shm.as<int>();
        *int_ptr = 999;

        pid_t pid = ::fork();
        KERNOX_ASSERT_TRUE(pid >= 0);

        if (pid == 0) {
            // Child opens existing and is killed abruptly with SIGKILL
            auto child_shm = SharedMemory::open_existing(shm_name);
            if (child_shm.is_ok()) {
                *child_shm.value().as<int>() = 1234;
            }
            ::kill(::getpid(), SIGKILL);
            ::_exit(1);
        }

        int status = 0;
        bool reaped = bounded_waitpid(pid, &status);
        KERNOX_ASSERT_TRUE(reaped);
        KERNOX_ASSERT_TRUE(WIFSIGNALED(status));
        KERNOX_ASSERT_EQ(WTERMSIG(status), SIGKILL);

        // Shared memory remains completely intact and readable by parent
        KERNOX_ASSERT_EQ(*shm.as<int>(), 1234);

        std::cout << "  [PASS] Test 7: Abrupt child SIGKILL termination resilience verified\n";
    }

    // ------------------------------------------------------------------------
    // Test 8: Zero Resource Leaks Across Repeated Iterations
    // ------------------------------------------------------------------------
    {
        const size_t initial_fds = count_open_fds();
        constexpr size_t iterations = 20;

        for (size_t i = 0; i < iterations; ++i) {
            std::string name = "/kernox_leak_test_" + std::to_string(::getpid()) + "_" + std::to_string(i);
            auto shm = SharedMemory::create(name, 1024);
            KERNOX_ASSERT_TRUE(shm.is_ok());
            *shm.value().as<int>() = static_cast<int>(i);

            auto opener = SharedMemory::open_existing(name);
            KERNOX_ASSERT_TRUE(opener.is_ok());
            KERNOX_ASSERT_EQ(*opener.value().as<int>(), static_cast<int>(i));

            // Both opener and shm go out of scope here
        }

        const size_t final_fds = count_open_fds();
        KERNOX_ASSERT_EQ(initial_fds, final_fds);

        std::cout << "  [PASS] Test 8: Zero descriptor and mapping leaks across 20 iterations (FDs: "
                  << final_fds << ")\n";
    }

    // ------------------------------------------------------------------------
    // Test 9: Explicit unmap and unlink Idempotency
    // ------------------------------------------------------------------------
    {
        const std::string shm_name = "/kernox_test_explicit_" + std::to_string(::getpid());
        auto shm_res = SharedMemory::create(shm_name, 1024);
        KERNOX_ASSERT_TRUE(shm_res.is_ok());
        auto& shm = shm_res.value();

        // Explicit unmap
        KERNOX_ASSERT_TRUE(shm.unmap().is_ok());
        KERNOX_ASSERT_FALSE(shm.is_mapped());
        KERNOX_ASSERT_TRUE(shm.data() == nullptr);

        // Repeated unmap is safe
        KERNOX_ASSERT_TRUE(shm.unmap().is_ok());

        // Explicit unlink
        KERNOX_ASSERT_TRUE(shm.unlink().is_ok());
        KERNOX_ASSERT_FALSE(shm.is_owner());

        // Repeated unlink is safe
        KERNOX_ASSERT_TRUE(shm.unlink().is_ok());

        std::cout << "  [PASS] Test 9: Explicit unmap and unlink idempotency verified\n";
    }

    std::cout << "\n[PASS] All POSIX Shared Memory tests passed successfully!\n";
    return 0;
}
