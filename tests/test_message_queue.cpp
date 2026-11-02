#include "test_harness.hpp"
#include "kernox/modules/system/message_queue.hpp"

#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace kernox::modules::system;

namespace {

class PipeHandshake {
public:
    PipeHandshake() {
        if (::pipe(fds_) != 0) {
            fds_[0] = -1;
            fds_[1] = -1;
        }
    }

    ~PipeHandshake() {
        close_read();
        close_write();
    }

    void close_read() {
        if (fds_[0] >= 0) {
            ::close(fds_[0]);
            fds_[0] = -1;
        }
    }

    void close_write() {
        if (fds_[1] >= 0) {
            ::close(fds_[1]);
            fds_[1] = -1;
        }
    }

    void notify() {
        if (fds_[1] >= 0) {
            char b = 1;
            if (::write(fds_[1], &b, 1) < 0) {
                // write error ignored
            }
            close_write();
        }
    }

    void wait() {
        if (fds_[0] >= 0) {
            char b = 0;
            if (::read(fds_[0], &b, 1) < 0) {
                // read error ignored
            }
            close_read();
        }
    }

private:
    int fds_[2]{-1, -1};
};

bool bounded_waitpid(pid_t pid, int* status, int timeout_sec = 5) {
    for (int i = 0; i < timeout_sec * 20; ++i) {
        int res = ::waitpid(pid, status, WNOHANG);
        if (res == pid) return true;
        if (res < 0) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    ::kill(pid, SIGKILL);
    ::waitpid(pid, status, 0);
    return false;
}

} // namespace

int main() {
    std::cout << "=========================================================\n"
              << "  KERNOX: POSIX Message Queue IPC Test Suite             \n"
              << "=========================================================\n\n";

    // ------------------------------------------------------------------------
    // Test 1: Queue Creation and Attribute Inspection
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 1: Queue Creation & Attribute Inspection...\n";
        const std::string qname = "/kernox_test_q1_" + std::to_string(::getpid());
        PosixMessageQueue::unlink(qname); // Ensure clean state

        QueueAttributes init_attr{};
        init_attr.max_messages = 5;
        init_attr.max_msg_size = 256;
        init_attr.non_blocking = false;

        auto q_res = PosixMessageQueue::create(qname, init_attr);
        KERNOX_ASSERT_TRUE(q_res.is_ok());
        auto q = std::move(q_res.value());

        KERNOX_ASSERT_TRUE(q.is_valid());
        KERNOX_ASSERT_EQ(q.name(), qname);
        KERNOX_ASSERT_TRUE(q.is_owner());

        auto attr_res = q.get_attributes();
        KERNOX_ASSERT_TRUE(attr_res.is_ok());
        const auto& attr = attr_res.value();
        KERNOX_ASSERT_EQ(attr.max_messages, 5);
        KERNOX_ASSERT_EQ(attr.max_msg_size, 256);
        KERNOX_ASSERT_EQ(attr.current_messages, 0);
        KERNOX_ASSERT_FALSE(attr.non_blocking);

        std::cout << "  [PASS] Test 1: Created queue and verified attributes\n";
    }

    // ------------------------------------------------------------------------
    // Test 2: Basic Send and Receive
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 2: Basic Send and Receive...\n";
        const std::string qname = "/kernox_test_q2_" + std::to_string(::getpid());
        PosixMessageQueue::unlink(qname);

        QueueAttributes init_attr{5, 256, 0, false};
        auto q = PosixMessageQueue::create(qname, init_attr).value();

        const std::string payload = "KERNOX_MQ_PAYLOAD_BASIC";
        auto send_res = q.send_string(payload, 10);
        KERNOX_ASSERT_TRUE(send_res.is_ok());

        auto attr = q.get_attributes().value();
        KERNOX_ASSERT_EQ(attr.current_messages, 1);

        auto recv_res = q.receive_string();
        KERNOX_ASSERT_TRUE(recv_res.is_ok());
        KERNOX_ASSERT_EQ(recv_res.value().first, payload);
        KERNOX_ASSERT_EQ(recv_res.value().second, 10u);

        attr = q.get_attributes().value();
        KERNOX_ASSERT_EQ(attr.current_messages, 0);

        std::cout << "  [PASS] Test 2: Basic send and receive verified\n";
    }

    // ------------------------------------------------------------------------
    // Test 3: Message Priority Ordering
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 3: Message Priority Ordering...\n";
        const std::string qname = "/kernox_test_q3_" + std::to_string(::getpid());
        PosixMessageQueue::unlink(qname);

        QueueAttributes init_attr{10, 128, 0, false};
        auto q = PosixMessageQueue::create(qname, init_attr).value();

        // Enqueue with low priority, then high priority, then medium priority
        KERNOX_ASSERT_TRUE(q.send_string("PRIO_LOW", 1).is_ok());
        KERNOX_ASSERT_TRUE(q.send_string("PRIO_HIGH", 20).is_ok());
        KERNOX_ASSERT_TRUE(q.send_string("PRIO_MED", 10).is_ok());

        // Dequeue should arrive ordered by priority: HIGH (20), MED (10), LOW (1)
        auto r1 = q.receive_string().value();
        KERNOX_ASSERT_EQ(r1.first, "PRIO_HIGH");
        KERNOX_ASSERT_EQ(r1.second, 20u);

        auto r2 = q.receive_string().value();
        KERNOX_ASSERT_EQ(r2.first, "PRIO_MED");
        KERNOX_ASSERT_EQ(r2.second, 10u);

        auto r3 = q.receive_string().value();
        KERNOX_ASSERT_EQ(r3.first, "PRIO_LOW");
        KERNOX_ASSERT_EQ(r3.second, 1u);

        std::cout << "  [PASS] Test 3: Priority delivery verified (20 -> 10 -> 1)\n";
    }

    // ------------------------------------------------------------------------
    // Test 4: FIFO Ordering for Equal Priorities
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 4: FIFO Ordering for Equal Priorities...\n";
        const std::string qname = "/kernox_test_q4_" + std::to_string(::getpid());
        PosixMessageQueue::unlink(qname);

        QueueAttributes init_attr{10, 128, 0, false};
        auto q = PosixMessageQueue::create(qname, init_attr).value();

        KERNOX_ASSERT_TRUE(q.send_string("MSG_FIRST", 5).is_ok());
        KERNOX_ASSERT_TRUE(q.send_string("MSG_SECOND", 5).is_ok());
        KERNOX_ASSERT_TRUE(q.send_string("MSG_THIRD", 5).is_ok());

        auto r1 = q.receive_string().value();
        auto r2 = q.receive_string().value();
        auto r3 = q.receive_string().value();

        KERNOX_ASSERT_EQ(r1.first, "MSG_FIRST");
        KERNOX_ASSERT_EQ(r2.first, "MSG_SECOND");
        KERNOX_ASSERT_EQ(r3.first, "MSG_THIRD");

        std::cout << "  [PASS] Test 4: Strict FIFO order for equal priorities confirmed\n";
    }

    // ------------------------------------------------------------------------
    // Test 5: Timed Receive on Empty Queue
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 5: Timed Receive on Empty Queue...\n";
        const std::string qname = "/kernox_test_q5_" + std::to_string(::getpid());
        PosixMessageQueue::unlink(qname);

        QueueAttributes init_attr{5, 128, 0, false};
        auto q = PosixMessageQueue::create(qname, init_attr).value();

        auto t0 = std::chrono::steady_clock::now();
        auto recv_res = q.receive_string(0, std::chrono::milliseconds(150));
        auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();

        KERNOX_ASSERT_TRUE(recv_res.is_err());
        KERNOX_ASSERT_EQ(recv_res.error().code, kernox::core::ErrorCode::Timeout);
        KERNOX_ASSERT_TRUE(dur >= 100);

        std::cout << "  [PASS] Test 5: Timed receive correctly timed out after " << dur << "ms\n";
    }

    // ------------------------------------------------------------------------
    // Test 6: Timed Send on Full Queue
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 6: Timed Send on Full Queue...\n";
        const std::string qname = "/kernox_test_q6_" + std::to_string(::getpid());
        PosixMessageQueue::unlink(qname);

        // Queue with capacity = 2
        QueueAttributes init_attr{2, 128, 0, false};
        auto q = PosixMessageQueue::create(qname, init_attr).value();

        KERNOX_ASSERT_TRUE(q.send_string("M1").is_ok());
        KERNOX_ASSERT_TRUE(q.send_string("M2").is_ok());

        auto t0 = std::chrono::steady_clock::now();
        auto send_res = q.send_string("M3_OVERFLOW", 0, std::chrono::milliseconds(150));
        auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();

        KERNOX_ASSERT_TRUE(send_res.is_err());
        KERNOX_ASSERT_EQ(send_res.error().code, kernox::core::ErrorCode::Timeout);
        KERNOX_ASSERT_TRUE(dur >= 100);

        std::cout << "  [PASS] Test 6: Timed send on full queue correctly timed out after " << dur << "ms\n";
    }

    // ------------------------------------------------------------------------
    // Test 7: Cross-Process Communication
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 7: Cross-Process Communication...\n";
        const std::string qname = "/kernox_test_q7_" + std::to_string(::getpid());
        PosixMessageQueue::unlink(qname);

        QueueAttributes init_attr{10, 256, 0, false};
        auto server_q = PosixMessageQueue::create(qname, init_attr).value();

        PipeHandshake handshake;
        pid_t child = ::fork();
        KERNOX_ASSERT_TRUE(child >= 0);

        if (child == 0) {
            handshake.close_read();
            auto client_q_res = PosixMessageQueue::open(qname, QueueAccess::ReadWrite);
            if (client_q_res.is_err()) ::_exit(1);
            auto client_q = std::move(client_q_res.value());

            // Signal ready
            handshake.notify();

            // Receive command from parent
            auto msg = client_q.receive_string(0, std::chrono::milliseconds(3000));
            if (!msg.is_ok() || msg.value().first != "PING_FROM_PARENT") ::_exit(2);

            // Respond with priority
            if (client_q.send_string("PONG_FROM_CHILD", 15).is_err()) ::_exit(3);

            ::_exit(0);
        }

        handshake.close_write();
        handshake.wait();

        // Send ping to child
        KERNOX_ASSERT_TRUE(server_q.send_string("PING_FROM_PARENT").is_ok());

        // Receive response from child
        auto resp = server_q.receive_string(0, std::chrono::milliseconds(3000));
        KERNOX_ASSERT_TRUE(resp.is_ok());
        KERNOX_ASSERT_EQ(resp.value().first, "PONG_FROM_CHILD");
        KERNOX_ASSERT_EQ(resp.value().second, 15u);

        int st = 0;
        KERNOX_ASSERT_TRUE(bounded_waitpid(child, &st));
        KERNOX_ASSERT_TRUE(WIFEXITED(st) && WEXITSTATUS(st) == 0);

        std::cout << "  [PASS] Test 7: Cross-process request/response verified\n";
    }

    // ------------------------------------------------------------------------
    // Test 8: Non-Blocking Behavior
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 8: Non-Blocking Behavior...\n";
        const std::string qname = "/kernox_test_q8_" + std::to_string(::getpid());
        PosixMessageQueue::unlink(qname);

        QueueAttributes init_attr{1, 128, 0, false};
        auto q = PosixMessageQueue::create(qname, init_attr).value();

        KERNOX_ASSERT_TRUE(q.set_non_blocking(true).is_ok());

        auto attr = q.get_attributes().value();
        KERNOX_ASSERT_TRUE(attr.non_blocking);

        // Receive on empty non-blocking queue should fail immediately
        auto r_empty = q.receive_string();
        KERNOX_ASSERT_TRUE(r_empty.is_err());
        KERNOX_ASSERT_EQ(r_empty.error().code, kernox::core::ErrorCode::Timeout);

        // Send 1 message
        KERNOX_ASSERT_TRUE(q.send_string("SOLO_MSG").is_ok());

        // Send on full non-blocking queue should fail immediately
        auto s_full = q.send_string("SECOND_MSG");
        KERNOX_ASSERT_TRUE(s_full.is_err());
        KERNOX_ASSERT_EQ(s_full.error().code, kernox::core::ErrorCode::Timeout);

        // Toggle back to blocking
        KERNOX_ASSERT_TRUE(q.set_non_blocking(false).is_ok());
        attr = q.get_attributes().value();
        KERNOX_ASSERT_FALSE(attr.non_blocking);

        auto r_ok = q.receive_string().value();
        KERNOX_ASSERT_EQ(r_ok.first, "SOLO_MSG");

        std::cout << "  [PASS] Test 8: Non-blocking toggling and immediate return verified\n";
    }

    // ------------------------------------------------------------------------
    // Test 9: Move Semantics & Invalidation
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 9: Move Semantics & Invalidation...\n";
        const std::string qname = "/kernox_test_q9_" + std::to_string(::getpid());
        PosixMessageQueue::unlink(qname);

        QueueAttributes init_attr{5, 128, 0, false};
        auto q1 = PosixMessageQueue::create(qname, init_attr).value();
        KERNOX_ASSERT_TRUE(q1.is_valid());

        // Move constructor
        PosixMessageQueue q2(std::move(q1));
        KERNOX_ASSERT_FALSE(q1.is_valid());
        KERNOX_ASSERT_TRUE(q2.is_valid());
        KERNOX_ASSERT_EQ(q2.name(), qname);

        // Move assignment
        PosixMessageQueue q3;
        q3 = std::move(q2);
        KERNOX_ASSERT_FALSE(q2.is_valid());
        KERNOX_ASSERT_TRUE(q3.is_valid());
        KERNOX_ASSERT_EQ(q3.name(), qname);

        // Verify q3 works
        KERNOX_ASSERT_TRUE(q3.send_string("MOVED_MSG").is_ok());
        auto r = q3.receive_string().value();
        KERNOX_ASSERT_EQ(r.first, "MOVED_MSG");

        std::cout << "  [PASS] Test 9: Move construction and move assignment verified\n";
    }

    // ------------------------------------------------------------------------
    // Test 10: Ownership and Unlink Lifecycle
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 10: Ownership and Unlink Lifecycle...\n";
        const std::string qname = "/kernox_test_q10_" + std::to_string(::getpid());
        PosixMessageQueue::unlink(qname);

        {
            QueueAttributes init_attr{5, 128, 0, false};
            auto q_owner = PosixMessageQueue::create(qname, init_attr).value();

            // Client opens queue
            auto q_client = PosixMessageQueue::open(qname).value();
            KERNOX_ASSERT_FALSE(q_client.unlink_on_close());

            // Destruct client; server queue should remain open and accessible
            q_client.close();

            auto q_reopen = PosixMessageQueue::open(qname);
            KERNOX_ASSERT_TRUE(q_reopen.is_ok());
        } // q_owner destructs here and unlinks

        // Queue should now be unlinked
        auto q_post = PosixMessageQueue::open(qname);
        KERNOX_ASSERT_TRUE(q_post.is_err());
        KERNOX_ASSERT_EQ(q_post.error().code, kernox::core::ErrorCode::NotFound);

        std::cout << "  [PASS] Test 10: Automatic cleanup on owner destruction verified\n";
    }

    // ------------------------------------------------------------------------
    // Test 11: Invalid Queue Names and Oversized Payloads
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 11: Invalid Queue Names & Oversized Payloads...\n";

        // Missing slash
        auto r1 = PosixMessageQueue::create("no_slash_q");
        KERNOX_ASSERT_TRUE(r1.is_err());
        KERNOX_ASSERT_EQ(r1.error().code, kernox::core::ErrorCode::InvalidArgument);

        // Just slash
        auto r2 = PosixMessageQueue::create("/");
        KERNOX_ASSERT_TRUE(r2.is_err());
        KERNOX_ASSERT_EQ(r2.error().code, kernox::core::ErrorCode::InvalidArgument);

        // Nested slashes
        auto r3 = PosixMessageQueue::create("/nested/path");
        KERNOX_ASSERT_TRUE(r3.is_err());
        KERNOX_ASSERT_EQ(r3.error().code, kernox::core::ErrorCode::InvalidArgument);

        // Oversized payload
        const std::string qname = "/kernox_test_q11_" + std::to_string(::getpid());
        PosixMessageQueue::unlink(qname);

        QueueAttributes init_attr{5, 64, 0, false}; // max 64 bytes
        auto q = PosixMessageQueue::create(qname, init_attr).value();

        std::string big_payload(128, 'X');
        auto r4 = q.send_string(big_payload);
        KERNOX_ASSERT_TRUE(r4.is_err());
        KERNOX_ASSERT_EQ(r4.error().code, kernox::core::ErrorCode::InvalidArgument);

        std::cout << "  [PASS] Test 11: Name and payload boundary enforcement verified\n";
    }

    // ------------------------------------------------------------------------
    // Test 12: Missing and Duplicate Queue Error Handling
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 12: Missing and Duplicate Queue Errors...\n";
        const std::string qname = "/kernox_test_q12_" + std::to_string(::getpid());
        PosixMessageQueue::unlink(qname);

        // Open nonexistent
        auto r_missing = PosixMessageQueue::open(qname);
        KERNOX_ASSERT_TRUE(r_missing.is_err());
        KERNOX_ASSERT_EQ(r_missing.error().code, kernox::core::ErrorCode::NotFound);

        // Create initial
        auto q = PosixMessageQueue::create(qname).value();

        // Duplicate create should fail
        auto r_dup = PosixMessageQueue::create(qname);
        KERNOX_ASSERT_TRUE(r_dup.is_err());
        KERNOX_ASSERT_EQ(r_dup.error().code, kernox::core::ErrorCode::InvalidState);

        // open_or_create should succeed on existing queue
        auto r_open_or_create = PosixMessageQueue::open_or_create(qname);
        KERNOX_ASSERT_TRUE(r_open_or_create.is_ok());

        std::cout << "  [PASS] Test 12: NotFound and InvalidState duplicate errors verified\n";
    }

    std::cout << "\n=========================================================\n"
              << "  [SUCCESS] All 12 POSIX Message Queue Tests Passed!     \n"
              << "=========================================================\n";
    return 0;
}
