#pragma once
#include <cstdio>
#include <ctime>
#include <string_view>

namespace dz {

enum class LogLevel : int {
    Debug   = 0,
    Info    = 1,
    Warning = 2,
    Error   = 3,
    Fatal   = 4,
};

class Logger {
public:
    static Logger& instance() noexcept {
        static Logger s;
        return s;
    }

    void setMinLevel(LogLevel level) noexcept { m_minLevel = level; }

    void log(LogLevel level, const char* file, int line,
             const char* fmt, ...) noexcept
    {
        if (level < m_minLevel) return;

        const char* prefix = levelPrefix(level);
        char timeBuf[20];
        timeStr(timeBuf, sizeof(timeBuf));

        FILE* out = (level >= LogLevel::Warning) ? stderr : stdout;
        std::fprintf(out, "[%s] %s ", timeBuf, prefix);

        va_list args;
        va_start(args, fmt);
        std::vfprintf(out, fmt, args);
        va_end(args);

        if (level >= LogLevel::Error)
            std::fprintf(out, "  (%s:%d)", file, line);

        std::fputc('\n', out);
        std::fflush(out);
    }

private:
    Logger() = default;
    LogLevel m_minLevel = LogLevel::Debug;

    static const char* levelPrefix(LogLevel l) noexcept {
        switch (l) {
            case LogLevel::Debug:   return "[DBG]";
            case LogLevel::Info:    return "[INF]";
            case LogLevel::Warning: return "[WRN]";
            case LogLevel::Error:   return "[ERR]";
            case LogLevel::Fatal:   return "[FTL]";
        }
        return "[???]";
    }

    static void timeStr(char* buf, int size) noexcept {
        time_t t = time(nullptr);
        struct tm tm_{};
#ifdef _WIN32
        localtime_s(&tm_, &t);
#else
        localtime_r(&t, &tm_);
#endif
        strftime(buf, size, "%H:%M:%S", &tm_);
    }
};

} // namespace dz

// ─────────────────────────────────────────────────────────────────────────────
// Convenience macros
// ─────────────────────────────────────────────────────────────────────────────
#define DZ_LOG_DEBUG(fmt, ...)   dz::Logger::instance().log(dz::LogLevel::Debug,   __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define DZ_LOG_INFO(fmt, ...)    dz::Logger::instance().log(dz::LogLevel::Info,    __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define DZ_LOG_WARN(fmt, ...)    dz::Logger::instance().log(dz::LogLevel::Warning, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define DZ_LOG_ERROR(fmt, ...)   dz::Logger::instance().log(dz::LogLevel::Error,   __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define DZ_LOG_FATAL(fmt, ...)   dz::Logger::instance().log(dz::LogLevel::Fatal,   __FILE__, __LINE__, fmt, ##__VA_ARGS__)
