#pragma once

#include <chrono>
#include <cstdint>
#include <string_view>

namespace kernox::core {

class Timer {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Duration = Clock::duration;

    // Constructs and starts the timer immediately
    Timer() noexcept : start_time_(Clock::now()) {}

    // Resets the start time to the current monotonic time
    void reset() noexcept {
        start_time_ = Clock::now();
    }

    // Returns the raw duration since construction or last reset
    [[nodiscard]] Duration elapsed() const noexcept {
        return Clock::now() - start_time_;
    }

    // Returns elapsed time in nanoseconds
    [[nodiscard]] int64_t elapsed_nanoseconds() const noexcept {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed()).count();
    }

    // Returns elapsed time in microseconds
    [[nodiscard]] int64_t elapsed_microseconds() const noexcept {
        return std::chrono::duration_cast<std::chrono::microseconds>(elapsed()).count();
    }

    // Returns elapsed time in milliseconds
    [[nodiscard]] int64_t elapsed_milliseconds() const noexcept {
        return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed()).count();
    }

    // Returns elapsed time in fractional seconds
    [[nodiscard]] double elapsed_seconds() const noexcept {
        return std::chrono::duration<double>(elapsed()).count();
    }

private:
    TimePoint start_time_;
};

} // namespace kernox::core
