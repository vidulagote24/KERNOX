#include "kernox/core/unique_fd.hpp"
#include "test_harness.hpp"

#include <cstring>
#include <unistd.h>

using namespace kernox::core;

int main() {
    // 1. Default constructor
    {
        UniqueFd fd;
        KERNOX_ASSERT_FALSE(fd.is_valid());
        KERNOX_ASSERT_EQ(fd.get(), -1);
        KERNOX_ASSERT_FALSE(static_cast<bool>(fd));
    }

    // 2. Value 0 is considered valid (standard input)
    {
        UniqueFd fd_zero(0);
        KERNOX_ASSERT_TRUE(fd_zero.is_valid());
        KERNOX_ASSERT_EQ(fd_zero.get(), 0);
        KERNOX_ASSERT_TRUE(static_cast<bool>(fd_zero));
        // Release so we do not close stdin upon test completion
        int raw = fd_zero.release();
        KERNOX_ASSERT_EQ(raw, 0);
        KERNOX_ASSERT_FALSE(fd_zero.is_valid());
    }

    // 3. Pipe creation and IPC data transfer
    {
        auto pipe_res = UniqueFd::create_pipe();
        KERNOX_ASSERT_TRUE(pipe_res.is_ok());

        auto [read_fd, write_fd] = std::move(pipe_res.value());
        KERNOX_ASSERT_TRUE(read_fd.is_valid());
        KERNOX_ASSERT_TRUE(write_fd.is_valid());

        const char msg[] = "OS_BENCHMARK_2026";
        ssize_t written = ::write(write_fd.get(), msg, sizeof(msg));
        KERNOX_ASSERT_EQ(written, static_cast<ssize_t>(sizeof(msg)));

        char buffer[32] = {0};
        ssize_t n_read = ::read(read_fd.get(), buffer, sizeof(buffer));
        KERNOX_ASSERT_EQ(n_read, static_cast<ssize_t>(sizeof(msg)));
        KERNOX_ASSERT_EQ(std::strcmp(buffer, msg), 0);

        // 4. Move construction
        UniqueFd moved_read(std::move(read_fd));
        KERNOX_ASSERT_FALSE(read_fd.is_valid());
        KERNOX_ASSERT_TRUE(moved_read.is_valid());

        // 5. Move assignment
        UniqueFd assign_target;
        assign_target = std::move(moved_read);
        KERNOX_ASSERT_FALSE(moved_read.is_valid());
        KERNOX_ASSERT_TRUE(assign_target.is_valid());

        // 6. Reset
        assign_target.reset(-1);
        KERNOX_ASSERT_FALSE(assign_target.is_valid());
    }

    std::cout << "[PASS] test_unique_fd completed successfully.\n";
    return 0;
}
