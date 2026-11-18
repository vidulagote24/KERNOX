#pragma once

#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace kernox::simulator {

// Status returned by all TerminalReader input operations.
enum class ReadStatus {
    Success,    // Valid value was read and stored
    Empty,      // User entered an empty line (and no default was provided)
    Cancelled,  // User explicitly cancelled the input operation ('c' or 'cancel')
    Eof         // End of input stream reached (e.g., Ctrl+D or closed pipe)
};

// Robust, reusable terminal input helper that uses std::getline exclusively,
// safely parses numeric values, handles EOF, and supports cancellation.
class TerminalReader {
public:
    explicit TerminalReader(std::istream& in = std::cin, std::ostream& out = std::cout);

    // Reads a raw trimmed string line.
    ReadStatus read_line(std::string& output, std::string_view prompt = "");

    // Reads a string with an optional default value and cancellation support.
    ReadStatus read_string(
        std::string& output,
        std::string_view prompt,
        std::optional<std::string_view> default_val = std::nullopt,
        bool allow_cancel = true
    );

    // Reads a uint64_t within [min_val, max_val].
    ReadStatus read_uint64(
        uint64_t& output,
        std::string_view prompt,
        std::optional<uint64_t> default_val = std::nullopt,
        uint64_t min_val = 0,
        uint64_t max_val = std::numeric_limits<uint64_t>::max(),
        bool allow_cancel = true
    );

    // Reads a uint32_t within [min_val, max_val].
    ReadStatus read_uint32(
        uint32_t& output,
        std::string_view prompt,
        std::optional<uint32_t> default_val = std::nullopt,
        uint32_t min_val = 0,
        uint32_t max_val = std::numeric_limits<uint32_t>::max(),
        bool allow_cancel = true
    );

    // Reads a double within [min_val, max_val].
    ReadStatus read_double(
        double& output,
        std::string_view prompt,
        std::optional<double> default_val = std::nullopt,
        double min_val = 0.0,
        double max_val = 1.0,
        bool allow_cancel = true
    );

    // Reads a boolean confirmation (y/n).
    ReadStatus read_bool(
        bool& output,
        std::string_view prompt,
        bool default_val = false,
        bool allow_cancel = true
    );

    // Returns true if the underlying stream has reached EOF or is in a failed state.
    [[nodiscard]] bool is_eof() const noexcept;

    std::ostream& out() noexcept { return out_; }
    std::istream& in() noexcept { return in_; }

private:
    std::istream& in_;
    std::ostream& out_;

    // Trims leading/trailing whitespace and carriage returns.
    static std::string trim(std::string_view str);

    // Checks if the given string represents a cancellation token ("c", "cancel", "q", "quit").
    static bool is_cancel_token(std::string_view str) noexcept;
};

} // namespace kernox::simulator
