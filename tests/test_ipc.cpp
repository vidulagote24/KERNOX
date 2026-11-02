#include "kernox/modules/system/ipc.hpp"
#include "test_harness.hpp"

#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <poll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace kernox::system;
using namespace kernox::core;

static size_t count_open_fds() {
    size_t count = 0;
    DIR* dir = ::opendir("/proc/self/fd");
    if (!dir) {
        return 0;
    }
    while (struct dirent* entry = ::readdir(dir)) {
        if (entry->d_name[0] != '.') {
            count++;
        }
    }
    ::closedir(dir);
    return count;
}

int main() {
    std::cout << "[KERNOX TEST] Starting Linux IPC (Pipes & FIFOs) Test Suite...\n";

    // ------------------------------------------------------------------------
    // Test 1: Invalid arguments and low-level boundary checks
    // ------------------------------------------------------------------------
    {
        uint8_t dummy = 0;
        KERNOX_ASSERT_TRUE(write_all(-1, &dummy, 1).is_err());
        KERNOX_ASSERT_TRUE(write_all(1, nullptr, 1).is_err());
        KERNOX_ASSERT_TRUE(write_all(1, &dummy, 0).is_ok());

        KERNOX_ASSERT_TRUE(read_exact(-1, &dummy, 1).is_err());
        KERNOX_ASSERT_TRUE(read_exact(0, nullptr, 1).is_err());
        KERNOX_ASSERT_TRUE(read_exact(0, &dummy, 0).is_ok());

        KERNOX_ASSERT_TRUE(read_up_to(-1, &dummy, 1).is_err());
        KERNOX_ASSERT_TRUE(read_up_to(0, nullptr, 1).is_err());
        KERNOX_ASSERT_EQ(read_up_to(0, &dummy, 0).value(), 0);

        KERNOX_ASSERT_TRUE(poll_fd(-1, POLLIN, std::chrono::milliseconds(10)).is_err());

        KERNOX_ASSERT_TRUE(NamedFifo::create("").is_err());
        KERNOX_ASSERT_TRUE(NamedFifo::open_existing("").is_err());
        std::cout << "  [PASS] Test 1: Invalid argument and boundary checks\n";
    }

    // ------------------------------------------------------------------------
    // Test 2: AnonymousPipe creation, write_all, and read_exact
    // ------------------------------------------------------------------------
    {
        auto pipe_res = AnonymousPipe::create(true, 65536);
        KERNOX_ASSERT_TRUE(pipe_res.is_ok());

        auto pipe = std::move(pipe_res.value());
        KERNOX_ASSERT_TRUE(pipe.is_valid());

        const std::string msg = "Hello from Linux kernel anonymous pipe!";
        auto wr_res = write_all(pipe.write_fd().get(), msg.data(), msg.size());
        KERNOX_ASSERT_TRUE(wr_res.is_ok());
        KERNOX_ASSERT_EQ(wr_res.value(), msg.size());

        std::string rx_buf(msg.size(), '\0');
        auto rd_res = read_exact(pipe.read_fd().get(), rx_buf.data(), msg.size());
        KERNOX_ASSERT_TRUE(rd_res.is_ok());
        KERNOX_ASSERT_EQ(rx_buf, msg);

        std::cout << "  [PASS] Test 2: AnonymousPipe basic write_all and read_exact\n";
    }

    // ------------------------------------------------------------------------
    // Test 3: Binary-safety with null bytes and control characters
    // ------------------------------------------------------------------------
    {
        auto pipe_res = AnonymousPipe::create(true);
        KERNOX_ASSERT_TRUE(pipe_res.is_ok());
        auto pipe = std::move(pipe_res.value());

        std::vector<uint8_t> binary_payload = {
            0x00, 0xFF, 0x00, 0xAA, 0x55, 0x00, 0x01, 0x02,
            0x00, 0x10, 0x20, 0x00, 0x7F, 0x80, 0x00, 0xFE
        };

        auto wr = write_all(pipe.write_fd().get(), binary_payload.data(), binary_payload.size());
        KERNOX_ASSERT_TRUE(wr.is_ok());
        KERNOX_ASSERT_EQ(wr.value(), binary_payload.size());

        std::vector<uint8_t> received(binary_payload.size(), 0xCC);
        auto rd = read_exact(pipe.read_fd().get(), received.data(), binary_payload.size());
        KERNOX_ASSERT_TRUE(rd.is_ok());
        KERNOX_ASSERT_TRUE(received == binary_payload);

        std::cout << "  [PASS] Test 3: Binary-safety with null bytes verified\n";
    }

    // ------------------------------------------------------------------------
    // Test 4: Framed message transmission (send_message / receive_message)
    // ------------------------------------------------------------------------
    {
        auto pipe_res = AnonymousPipe::create(true);
        KERNOX_ASSERT_TRUE(pipe_res.is_ok());
        auto pipe = std::move(pipe_res.value());

        std::vector<uint8_t> msg1 = {1, 2, 3, 4, 0, 5};
        std::vector<uint8_t> msg2 = {10, 20, 30, 0, 40, 50, 60, 70};

        auto s1 = send_message(pipe.write_fd().get(), msg1);
        KERNOX_ASSERT_TRUE(s1.is_ok());
        auto s2 = send_message(pipe.write_fd().get(), msg2);
        KERNOX_ASSERT_TRUE(s2.is_ok());

        auto r1 = receive_message(pipe.read_fd().get(), std::chrono::milliseconds(1000));
        KERNOX_ASSERT_TRUE(r1.is_ok());
        KERNOX_ASSERT_TRUE(r1.value() == msg1);

        auto r2 = receive_message(pipe.read_fd().get(), std::chrono::milliseconds(1000));
        KERNOX_ASSERT_TRUE(r2.is_ok());
        KERNOX_ASSERT_TRUE(r2.value() == msg2);

        // Receive with timeout when no data is sent
        auto r3 = receive_message(pipe.read_fd().get(), std::chrono::milliseconds(50));
        KERNOX_ASSERT_TRUE(r3.is_err());
        KERNOX_ASSERT_EQ(r3.error().code, ErrorCode::Timeout);

        std::cout << "  [PASS] Test 4: Framed messaging and timeout behavior\n";
    }

    // ------------------------------------------------------------------------
    // Test 5: Broken pipe and EPIPE handling (no SIGPIPE crash)
    // ------------------------------------------------------------------------
    {
        auto pipe_res = AnonymousPipe::create(true);
        KERNOX_ASSERT_TRUE(pipe_res.is_ok());
        auto pipe = std::move(pipe_res.value());

        // Explicitly close the reader end
        pipe.close_read();

        // Write to reader-less pipe must fail with IoError / EPIPE without process crash
        const uint8_t byte = 0x42;
        auto wr_res = write_all(pipe.write_fd().get(), &byte, 1);
        KERNOX_ASSERT_TRUE(wr_res.is_err());
        KERNOX_ASSERT_EQ(wr_res.error().code, ErrorCode::IoError);
        KERNOX_ASSERT_EQ(wr_res.error().system_errno, EPIPE);

        std::cout << "  [PASS] Test 5: Broken pipe EPIPE handling verified without SIGPIPE crash\n";
    }

    // ------------------------------------------------------------------------
    // Test 6: EOF detection on writer closure
    // ------------------------------------------------------------------------
    {
        auto pipe_res = AnonymousPipe::create(true);
        KERNOX_ASSERT_TRUE(pipe_res.is_ok());
        auto pipe = std::move(pipe_res.value());

        // Close write end to produce EOF
        pipe.close_write();

        uint8_t buf[16];
        auto rd_up_to = read_up_to(pipe.read_fd().get(), buf, sizeof(buf));
        KERNOX_ASSERT_TRUE(rd_up_to.is_ok());
        KERNOX_ASSERT_EQ(rd_up_to.value(), 0); // 0 bytes indicates clean EOF

        auto rd_exact = read_exact(pipe.read_fd().get(), buf, 4);
        KERNOX_ASSERT_TRUE(rd_exact.is_err()); // Premature EOF
        KERNOX_ASSERT_EQ(rd_exact.error().code, ErrorCode::IoError);

        std::cout << "  [PASS] Test 6: Clean EOF detection verified\n";
    }

    // ------------------------------------------------------------------------
    // Test 7: Large payload (1 MiB) streaming between parent and child
    // ------------------------------------------------------------------------
    {
        IpcTransferConfig cfg{1048576, 5, std::chrono::milliseconds(10000), true};
        auto res = run_pipe_throughput_benchmark(cfg, 2);
        KERNOX_ASSERT_TRUE(res.is_ok());
        KERNOX_ASSERT_TRUE(res.value().data_verified);
        KERNOX_ASSERT_EQ(res.value().total_bytes, 5 * 1048576);
        KERNOX_ASSERT_TRUE(res.value().throughput_mib_s > 0.0);

        std::cout << "  [PASS] Test 7: Large payload (1 MiB) pipe streaming throughput: "
                  << res.value().throughput_mib_s << " MiB/s\n";
    }

    // ------------------------------------------------------------------------
    // Test 8: Pipe round-trip ping-pong latency
    // ------------------------------------------------------------------------
    {
        IpcTransferConfig cfg{64, 100, std::chrono::milliseconds(5000), true};
        auto res = run_pipe_latency_benchmark(cfg, 10);
        KERNOX_ASSERT_TRUE(res.is_ok());
        KERNOX_ASSERT_TRUE(res.value().data_verified);
        KERNOX_ASSERT_TRUE(res.value().avg_round_trip_us > 0.0);
        KERNOX_ASSERT_TRUE(res.value().min_round_trip_us <= res.value().max_round_trip_us);

        std::cout << "  [PASS] Test 8: Pipe round-trip latency: avg="
                  << res.value().avg_round_trip_us << " us, p50="
                  << res.value().p50_round_trip_us << " us\n";
    }

    // ------------------------------------------------------------------------
    // Test 9: Named FIFO creation, ownership, and destructor cleanup
    // ------------------------------------------------------------------------
    {
        std::string fifo_path = "/tmp/kernox_test_fifo_" + std::to_string(::getpid());
        // Clean up in case of stale leftover
        ::unlink(fifo_path.c_str());

        {
            auto fifo_res = NamedFifo::create(fifo_path, 0600);
            KERNOX_ASSERT_TRUE(fifo_res.is_ok());
            KERNOX_ASSERT_TRUE(fifo_res.value().is_owned());

            struct stat st{};
            KERNOX_ASSERT_EQ(::lstat(fifo_path.c_str(), &st), 0);
            KERNOX_ASSERT_TRUE(S_ISFIFO(st.st_mode));

            // Attempting to create duplicate FIFO must fail
            auto dup_res = NamedFifo::create(fifo_path, 0600);
            KERNOX_ASSERT_TRUE(dup_res.is_err());
            KERNOX_ASSERT_EQ(dup_res.error().code, ErrorCode::InvalidState);
        }

        // Scope exit must have unlinked the owned FIFO
        struct stat st_after{};
        KERNOX_ASSERT_NE(::lstat(fifo_path.c_str(), &st_after), 0);
        KERNOX_ASSERT_EQ(errno, ENOENT);

        std::cout << "  [PASS] Test 9: NamedFifo creation, ownership, and RAII cleanup\n";
    }

    // ------------------------------------------------------------------------
    // Test 10: Named FIFO path safety (do not overwrite pre-existing regular file)
    // ------------------------------------------------------------------------
    {
        std::string reg_file = "/tmp/kernox_test_regular_" + std::to_string(::getpid()) + ".txt";
        {
            std::ofstream ofs(reg_file);
            ofs << "PROTECTED USER DATA";
        }

        auto fifo_res = NamedFifo::create(reg_file);
        KERNOX_ASSERT_TRUE(fifo_res.is_err());
        KERNOX_ASSERT_EQ(fifo_res.error().code, ErrorCode::InvalidState);

        // Verify regular file was NOT modified or deleted
        struct stat st{};
        KERNOX_ASSERT_EQ(::lstat(reg_file.c_str(), &st), 0);
        KERNOX_ASSERT_TRUE(S_ISREG(st.st_mode));
        ::unlink(reg_file.c_str());

        std::cout << "  [PASS] Test 10: NamedFifo protection of pre-existing user files\n";
    }

    // ------------------------------------------------------------------------
    // Test 11: Named FIFO open_write timeout without reader
    // ------------------------------------------------------------------------
    {
        std::string fifo_path = "/tmp/kernox_test_fifo_timeout_" + std::to_string(::getpid());
        auto fifo_res = NamedFifo::create(fifo_path);
        KERNOX_ASSERT_TRUE(fifo_res.is_ok());

        auto wr_open = fifo_res.value().open_write(std::chrono::milliseconds(50));
        KERNOX_ASSERT_TRUE(wr_open.is_err());
        KERNOX_ASSERT_EQ(wr_open.error().code, ErrorCode::Timeout);

        std::cout << "  [PASS] Test 11: NamedFifo open_write timeout without reader\n";
    }

    // ------------------------------------------------------------------------
    // Test 12: Named FIFO throughput and latency benchmarks
    // ------------------------------------------------------------------------
    {
        IpcTransferConfig tp_cfg{4096, 50, std::chrono::milliseconds(5000), true};
        auto tp_res = run_fifo_throughput_benchmark("/tmp", tp_cfg, 5);
        KERNOX_ASSERT_TRUE(tp_res.is_ok());
        KERNOX_ASSERT_TRUE(tp_res.value().data_verified);
        KERNOX_ASSERT_TRUE(tp_res.value().throughput_mib_s > 0.0);

        IpcTransferConfig lat_cfg{64, 50, std::chrono::milliseconds(5000), true};
        auto lat_res = run_fifo_latency_benchmark("/tmp", lat_cfg, 5);
        KERNOX_ASSERT_TRUE(lat_res.is_ok());
        KERNOX_ASSERT_TRUE(lat_res.value().data_verified);
        KERNOX_ASSERT_TRUE(lat_res.value().avg_round_trip_us > 0.0);

        std::cout << "  [PASS] Test 12: NamedFifo throughput (" << tp_res.value().throughput_mib_s
                  << " MiB/s) and latency (avg: " << lat_res.value().avg_round_trip_us << " us)\n";
    }

    // ------------------------------------------------------------------------
    // Test 13: Resource leak check (no file descriptor leaks across repetitions)
    // ------------------------------------------------------------------------
    {
        size_t baseline_fds = count_open_fds();

        for (int i = 0; i < 20; ++i) {
            auto pipe_res = AnonymousPipe::create(true);
            KERNOX_ASSERT_TRUE(pipe_res.is_ok());
            auto pipe = std::move(pipe_res.value());
            uint8_t byte = static_cast<uint8_t>(i);
            (void)write_all(pipe.write_fd().get(), &byte, 1);
            uint8_t rx = 0;
            (void)read_exact(pipe.read_fd().get(), &rx, 1);
        }

        size_t post_fds = count_open_fds();
        KERNOX_ASSERT_EQ(baseline_fds, post_fds);

        std::cout << "  [PASS] Test 13: Zero file descriptor leaks across 20 iterations (FDs: "
                  << post_fds << ")\n";
    }

    // ------------------------------------------------------------------------
    // Test 14: Comprehensive benchmark suite runner smoke test
    // ------------------------------------------------------------------------
    {
        auto suite_res = run_ipc_benchmark_suite("all", {64, 4096}, 20, 5, "/tmp");
        KERNOX_ASSERT_TRUE(suite_res.is_ok());
        const auto& suite = suite_res.value();
        KERNOX_ASSERT_EQ(suite.throughput_results.size(), 4); // 2 pipe + 2 fifo
        KERNOX_ASSERT_EQ(suite.latency_results.size(), 4);

        for (const auto& tp : suite.throughput_results) {
            KERNOX_ASSERT_TRUE(tp.throughput_mib_s > 0.0);
            KERNOX_ASSERT_TRUE(tp.data_verified);
        }
        for (const auto& lat : suite.latency_results) {
            KERNOX_ASSERT_TRUE(lat.avg_round_trip_us > 0.0);
            KERNOX_ASSERT_TRUE(lat.data_verified);
        }

        std::cout << "  [PASS] Test 14: Full benchmark suite runner verified\n";
    }

    // ------------------------------------------------------------------------
    // Test 15: Empty frame (0-byte payload) send/receive across pipe
    // ------------------------------------------------------------------------
    {
        auto pipe_res = AnonymousPipe::create(true);
        KERNOX_ASSERT_TRUE(pipe_res.is_ok());
        auto pipe = std::move(pipe_res.value());

        std::vector<uint8_t> empty_msg{};
        auto send_res = send_message(pipe.write_fd().get(), empty_msg);
        KERNOX_ASSERT_TRUE(send_res.is_ok());

        auto recv_res = receive_message(pipe.read_fd().get(), std::chrono::milliseconds(1000));
        KERNOX_ASSERT_TRUE(recv_res.is_ok());
        KERNOX_ASSERT_TRUE(recv_res.value().empty());

        std::cout << "  [PASS] Test 15: Empty frame (0-byte payload) round-trip verified\n";
    }

    // ------------------------------------------------------------------------
    // Test 16: Oversized frame rejection and memory bounds enforcement
    // ------------------------------------------------------------------------
    {
        auto pipe_res = AnonymousPipe::create(true);
        KERNOX_ASSERT_TRUE(pipe_res.is_ok());
        auto pipe = std::move(pipe_res.value());

        // Craft a 4-byte big-endian header requesting 128 MiB (exceeding MAX_FRAME_PAYLOAD_SIZE = 64 MiB)
        uint32_t oversized_len = htonl(128 * 1024 * 1024);
        auto wr_res = write_all(pipe.write_fd().get(), &oversized_len, sizeof(oversized_len));
        KERNOX_ASSERT_TRUE(wr_res.is_ok());

        // receive_message must reject this immediately with InvalidArgument without allocating
        auto recv_res = receive_message(pipe.read_fd().get(), std::chrono::milliseconds(1000));
        KERNOX_ASSERT_TRUE(recv_res.is_err());
        KERNOX_ASSERT_EQ(recv_res.error().code, ErrorCode::InvalidArgument);

        std::cout << "  [PASS] Test 16: Oversized frame rejection (uncontrolled allocation defense)\n";
    }

    // ------------------------------------------------------------------------
    // Test 17: Partial frame followed by EOF detection
    // ------------------------------------------------------------------------
    {
        auto pipe_res = AnonymousPipe::create(true);
        KERNOX_ASSERT_TRUE(pipe_res.is_ok());
        auto pipe = std::move(pipe_res.value());

        // Write a 4-byte header stating 64 bytes of payload follow
        uint32_t header = htonl(64);
        KERNOX_ASSERT_TRUE(write_all(pipe.write_fd().get(), &header, sizeof(header)).is_ok());

        // Write only 16 bytes of payload (partial frame)
        std::vector<uint8_t> partial(16, 0xAA);
        KERNOX_ASSERT_TRUE(write_all(pipe.write_fd().get(), partial.data(), partial.size()).is_ok());

        // Close write end to trigger EOF on the reader side
        pipe.close_write();

        // receive_message must encounter premature EOF and fail with IoError
        auto recv_res = receive_message(pipe.read_fd().get(), std::chrono::milliseconds(1000));
        KERNOX_ASSERT_TRUE(recv_res.is_err());
        KERNOX_ASSERT_EQ(recv_res.error().code, ErrorCode::IoError);

        std::cout << "  [PASS] Test 17: Partial frame followed by premature EOF detected\n";
    }

    // ------------------------------------------------------------------------
    // Test 18: poll_fd error handling on invalid / closed descriptor (POLLNVAL)
    // ------------------------------------------------------------------------
    {
        int closed_fd = -1;
        {
            auto pipe_res = AnonymousPipe::create(true);
            KERNOX_ASSERT_TRUE(pipe_res.is_ok());
            closed_fd = pipe_res.value().read_fd().release();
            ::close(closed_fd);
        }

        auto poll_res = poll_fd(closed_fd, POLLIN, std::chrono::milliseconds(50));
        KERNOX_ASSERT_TRUE(poll_res.is_err());
        KERNOX_ASSERT_EQ(poll_res.error().code, ErrorCode::InvalidArgument);

        std::cout << "  [PASS] Test 18: poll_fd POLLNVAL handling on closed descriptor\n";
    }

    // ------------------------------------------------------------------------
    // Test 19: AnonymousPipe endpoint state inspection and selective closure
    // ------------------------------------------------------------------------
    {
        auto pipe_res = AnonymousPipe::create(true);
        KERNOX_ASSERT_TRUE(pipe_res.is_ok());
        auto pipe = std::move(pipe_res.value());

        KERNOX_ASSERT_TRUE(pipe.is_valid());
        KERNOX_ASSERT_TRUE(pipe.has_read_end());
        KERNOX_ASSERT_TRUE(pipe.has_write_end());

        pipe.close_read();
        KERNOX_ASSERT_FALSE(pipe.has_read_end());
        KERNOX_ASSERT_TRUE(pipe.has_write_end());
        KERNOX_ASSERT_FALSE(pipe.is_valid());

        pipe.close_write();
        KERNOX_ASSERT_FALSE(pipe.has_write_end());
        KERNOX_ASSERT_FALSE(pipe.is_valid());

        std::cout << "  [PASS] Test 19: AnonymousPipe endpoint state queries and selective closure\n";
    }

    // ------------------------------------------------------------------------
    // Test 20: NamedFifo unlink idempotency
    // ------------------------------------------------------------------------
    {
        std::string fifo_path = "/tmp/kernox_test_fifo_idempotent_" + std::to_string(::getpid());
        auto fifo_res = NamedFifo::create(fifo_path);
        KERNOX_ASSERT_TRUE(fifo_res.is_ok());
        auto fifo = std::move(fifo_res.value());

        KERNOX_ASSERT_TRUE(fifo.is_owned());
        KERNOX_ASSERT_TRUE(fifo.unlink().is_ok());
        KERNOX_ASSERT_FALSE(fifo.is_owned());

        // Repeated unlink() must be safe and idempotent
        KERNOX_ASSERT_TRUE(fifo.unlink().is_ok());

        std::cout << "  [PASS] Test 20: NamedFifo unlink() idempotency verified\n";
    }

    std::cout << "\n[PASS] All Linux IPC automated tests passed successfully!\n";
    return 0;
}
