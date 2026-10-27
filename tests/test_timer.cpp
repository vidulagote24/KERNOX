#include "kernox/core/timer.hpp"
#include "test_harness.hpp"

#include <thread>

using namespace kernox::core;

int main() {
    Timer timer;

    // Small controlled sleep
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    int64_t ns = timer.elapsed_nanoseconds();
    int64_t us = timer.elapsed_microseconds();
    int64_t ms = timer.elapsed_milliseconds();
    double sec = timer.elapsed_seconds();

    KERNOX_ASSERT_TRUE(ns > 0);
    KERNOX_ASSERT_TRUE(us > 0);
    KERNOX_ASSERT_TRUE(ms >= 5); // At least 5ms after 10ms sleep
    KERNOX_ASSERT_TRUE(sec > 0.0);

    // Monotonic progression: subsequent reading must be >= previous
    int64_t us_second = timer.elapsed_microseconds();
    KERNOX_ASSERT_TRUE(us_second >= us);

    // Test reset
    timer.reset();
    int64_t ms_after_reset = timer.elapsed_milliseconds();
    KERNOX_ASSERT_TRUE(ms_after_reset < 10); // Immediately after reset should be near 0

    std::cout << "[PASS] test_timer completed successfully.\n";
    return 0;
}
