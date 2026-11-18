#include "kernox/modules/simulator/terminal_reader.hpp"

#include <cctype>
#include <iomanip>
#include <sstream>

namespace kernox::simulator {

TerminalReader::TerminalReader(std::istream& in, std::ostream& out)
    : in_(in), out_(out) {}

std::string TerminalReader::trim(std::string_view str) {
    size_t start = 0;
    while (start < str.size() && std::isspace(static_cast<unsigned char>(str[start]))) {
        start++;
    }
    size_t end = str.size();
    while (end > start && std::isspace(static_cast<unsigned char>(str[end - 1]))) {
        end--;
    }
    return std::string(str.substr(start, end - start));
}

bool TerminalReader::is_cancel_token(std::string_view str) noexcept {
    if (str == "c" || str == "C" || str == "cancel" || str == "CANCEL" ||
        str == "q" || str == "Q" || str == "quit" || str == "QUIT") {
        return true;
    }
    return false;
}

bool TerminalReader::is_eof() const noexcept {
    return in_.eof() || !in_.good();
}

ReadStatus TerminalReader::read_line(std::string& output, std::string_view prompt) {
    if (!prompt.empty()) {
        out_ << prompt;
        out_.flush();
    }

    output.clear();
    if (!std::getline(in_, output)) {
        return ReadStatus::Eof;
    }

    // Strip carriage return if present
    if (!output.empty() && output.back() == '\r') {
        output.pop_back();
    }

    output = trim(output);
    if (output.empty()) {
        return ReadStatus::Empty;
    }

    return ReadStatus::Success;
}

ReadStatus TerminalReader::read_string(
    std::string& output,
    std::string_view prompt,
    std::optional<std::string_view> default_val,
    bool allow_cancel
) {
    while (true) {
        std::ostringstream p_stream;
        p_stream << prompt;
        if (default_val.has_value()) {
            p_stream << " [default: " << *default_val << "]";
        }
        if (allow_cancel) {
            p_stream << " ('c' to cancel)";
        }
        p_stream << ": ";

        std::string raw;
        ReadStatus status = read_line(raw, p_stream.str());

        if (status == ReadStatus::Eof) {
            return ReadStatus::Eof;
        }

        if (status == ReadStatus::Empty) {
            if (default_val.has_value()) {
                output = std::string(*default_val);
                return ReadStatus::Success;
            }
            out_ << "  [Input cannot be empty. Please enter a valid value]\n";
            continue;
        }

        if (allow_cancel && is_cancel_token(raw)) {
            out_ << "  [Operation cancelled]\n";
            return ReadStatus::Cancelled;
        }

        output = raw;
        return ReadStatus::Success;
    }
}

ReadStatus TerminalReader::read_uint64(
    uint64_t& output,
    std::string_view prompt,
    std::optional<uint64_t> default_val,
    uint64_t min_val,
    uint64_t max_val,
    bool allow_cancel
) {
    while (true) {
        std::ostringstream p_stream;
        p_stream << prompt;
        if (default_val.has_value()) {
            p_stream << " [default: " << *default_val << "]";
        }
        if (allow_cancel) {
            p_stream << " ('c' to cancel)";
        }
        p_stream << ": ";

        std::string raw;
        ReadStatus status = read_line(raw, p_stream.str());

        if (status == ReadStatus::Eof) {
            return ReadStatus::Eof;
        }

        if (status == ReadStatus::Empty) {
            if (default_val.has_value()) {
                output = *default_val;
                return ReadStatus::Success;
            }
            out_ << "  [Input cannot be empty. Please enter a number]\n";
            continue;
        }

        if (allow_cancel && is_cancel_token(raw)) {
            out_ << "  [Operation cancelled]\n";
            return ReadStatus::Cancelled;
        }

        // Verify all characters are digits
        bool all_digits = true;
        for (char ch : raw) {
            if (!std::isdigit(static_cast<unsigned char>(ch))) {
                all_digits = false;
                break;
            }
        }

        if (!all_digits) {
            out_ << "  [Invalid input: '" << raw << "' is not a valid non-negative integer]\n";
            continue;
        }

        try {
            size_t idx = 0;
            uint64_t val = std::stoull(raw, &idx);
            if (idx != raw.size()) {
                out_ << "  [Invalid input: trailing characters in '" << raw << "']\n";
                continue;
            }

            if (val < min_val || val > max_val) {
                out_ << "  [Value " << val << " out of range. Allowed range: ["
                     << min_val << " - " << max_val << "]]\n";
                continue;
            }

            output = val;
            return ReadStatus::Success;
        } catch (...) {
            out_ << "  [Numeric overflow or invalid number: '" << raw << "']\n";
        }
    }
}

ReadStatus TerminalReader::read_uint32(
    uint32_t& output,
    std::string_view prompt,
    std::optional<uint32_t> default_val,
    uint32_t min_val,
    uint32_t max_val,
    bool allow_cancel
) {
    uint64_t tmp = 0;
    std::optional<uint64_t> def_u64 = default_val ? std::optional<uint64_t>(*default_val) : std::nullopt;
    ReadStatus status = read_uint64(
        tmp,
        prompt,
        def_u64,
        static_cast<uint64_t>(min_val),
        static_cast<uint64_t>(max_val),
        allow_cancel
    );
    if (status == ReadStatus::Success) {
        output = static_cast<uint32_t>(tmp);
    }
    return status;
}

ReadStatus TerminalReader::read_double(
    double& output,
    std::string_view prompt,
    std::optional<double> default_val,
    double min_val,
    double max_val,
    bool allow_cancel
) {
    while (true) {
        std::ostringstream p_stream;
        p_stream << prompt;
        if (default_val.has_value()) {
            p_stream << " [default: " << std::fixed << std::setprecision(2) << *default_val << "]";
        }
        if (allow_cancel) {
            p_stream << " ('c' to cancel)";
        }
        p_stream << ": ";

        std::string raw;
        ReadStatus status = read_line(raw, p_stream.str());

        if (status == ReadStatus::Eof) {
            return ReadStatus::Eof;
        }

        if (status == ReadStatus::Empty) {
            if (default_val.has_value()) {
                output = *default_val;
                return ReadStatus::Success;
            }
            out_ << "  [Input cannot be empty. Please enter a floating-point value]\n";
            continue;
        }

        if (allow_cancel && is_cancel_token(raw)) {
            out_ << "  [Operation cancelled]\n";
            return ReadStatus::Cancelled;
        }

        try {
            size_t idx = 0;
            double val = std::stod(raw, &idx);
            if (idx != raw.size()) {
                out_ << "  [Invalid input: trailing characters in '" << raw << "']\n";
                continue;
            }

            if (val < min_val || val > max_val) {
                out_ << "  [Value " << val << " out of range. Allowed range: ["
                     << min_val << " - " << max_val << "]]\n";
                continue;
            }

            output = val;
            return ReadStatus::Success;
        } catch (...) {
            out_ << "  [Invalid floating-point format or overflow: '" << raw << "']\n";
        }
    }
}

ReadStatus TerminalReader::read_bool(
    bool& output,
    std::string_view prompt,
    bool default_val,
    bool allow_cancel
) {
    while (true) {
        std::ostringstream p_stream;
        p_stream << prompt << (default_val ? " (Y/n)" : " (y/N)");
        if (allow_cancel) {
            p_stream << " ('c' to cancel)";
        }
        p_stream << ": ";

        std::string raw;
        ReadStatus status = read_line(raw, p_stream.str());

        if (status == ReadStatus::Eof) {
            return ReadStatus::Eof;
        }

        if (status == ReadStatus::Empty) {
            output = default_val;
            return ReadStatus::Success;
        }

        if (allow_cancel && is_cancel_token(raw)) {
            out_ << "  [Operation cancelled]\n";
            return ReadStatus::Cancelled;
        }

        std::string lower;
        lower.reserve(raw.size());
        for (char ch : raw) {
            lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }

        if (lower == "y" || lower == "yes" || lower == "true" || lower == "1") {
            output = true;
            return ReadStatus::Success;
        }
        if (lower == "n" || lower == "no" || lower == "false" || lower == "0") {
            output = false;
            return ReadStatus::Success;
        }

        out_ << "  [Please enter 'y' for yes or 'n' for no]\n";
    }
}

} // namespace kernox::simulator
