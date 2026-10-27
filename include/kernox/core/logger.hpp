#pragma once

#include <mutex>
#include <string>
#include <string_view>

namespace kernox::core {

enum class LogLevel {
    Debug = 0,
    Info,
    Warn,
    Error,
    Off
};

[[nodiscard]] std::string_view to_string(LogLevel level) noexcept;

class Logger {
public:
    static Logger& instance() noexcept;

    void set_level(LogLevel level) noexcept;
    [[nodiscard]] LogLevel get_level() const noexcept;

    void log(LogLevel level, std::string_view message);

    void debug(std::string_view message) { log(LogLevel::Debug, message); }
    void info(std::string_view message)  { log(LogLevel::Info, message); }
    void warn(std::string_view message)  { log(LogLevel::Warn, message); }
    void error(std::string_view message) { log(LogLevel::Error, message); }

private:
    Logger() = default;
    ~Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    LogLevel current_level_{LogLevel::Info};
    mutable std::mutex mutex_{};
};

// Convenience free functions
inline void log_debug(std::string_view msg) { Logger::instance().debug(msg); }
inline void log_info(std::string_view msg)  { Logger::instance().info(msg); }
inline void log_warn(std::string_view msg)  { Logger::instance().warn(msg); }
inline void log_error(std::string_view msg) { Logger::instance().error(msg); }

} // namespace kernox::core
