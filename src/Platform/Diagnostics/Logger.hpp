#pragma once

#include <cstdarg>
#include <mutex>
#include <string>
#include "Platform/Event.hpp"

/// Go through the LOG_* macros: they compile a level out of the build.
class Logger {
public:
    enum class Level {
        Info,
        Warning,
        Error
    };

    static void Initialize();
    static void Log(Level level, const char* format, ...);

    static std::string GetLogText();

    static Event<Level, const std::string&, const std::string&> OnLogAdded;

private:
    static std::wstring _logFilePath;
    static std::mutex _logMutex;
    static std::once_flag _initFlag;
    static constexpr size_t MAX_LOG_SIZE = 5 * 1024 * 1024;

    static const char* LevelToString(Level level);
    static std::string FormatLogEntry(Level level, const std::string& message);
    static std::string FormatString(const char* format, va_list args);
    static void CheckLogFileSize();
    static void LogImpl(Level level, const std::string& message);
};

