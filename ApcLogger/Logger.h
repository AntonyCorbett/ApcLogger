#pragma once
#include <string>

namespace Logger
{
    enum class Level { Trace, Debug, Info, Warn, Error };  // NOLINT(performance-enum-size)

    // Initialize logger. Creates a folder (if needed) and a UTF-8 log file, e.g. "MyApp.log".
    void Init(const std::wstring& appName, const std::wstring& logFolderPath);

    // Optional: change minimum level (default: Info in Release, Debug in Debug)
    void SetMinLevel(Level level);

    // Structured logging APIs
    void Log(Level level, const wchar_t* fmt, ...);
    void LogLastError(Level level, const wchar_t* context, unsigned long errorCode = 0); // context = what failed

    // Cleanup (flush/close)
    void Shutdown();
}

// Convenience macros (require at least the format string)
#define LOG_TRACE(...) Logger::Log(Logger::Level::Trace, __VA_ARGS__)
#define LOG_DEBUG(...) Logger::Log(Logger::Level::Debug, __VA_ARGS__)
#define LOG_INFO(...)  Logger::Log(Logger::Level::Info,  __VA_ARGS__)
#define LOG_WARN(...)  Logger::Log(Logger::Level::Warn,  __VA_ARGS__)
#define LOG_ERROR(...) Logger::Log(Logger::Level::Error, __VA_ARGS__)