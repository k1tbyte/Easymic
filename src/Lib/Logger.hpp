#ifndef EASYMIC_LOGGER_HPP
#define EASYMIC_LOGGER_HPP

#include <cstdarg>
#include <mutex>
#include <string>
#include "Event.hpp"

/**
 * @brief Simple file-based logger with thread-safe operations and printf-style formatting.
 *
 * Go through the LOG_* macros in definitions.h rather than calling Log directly - they are what
 * compiles a level out of the build.
 */
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

    /// Every line, from whatever thread produced it. Raised with no lock held.
    static Event<Level, const std::string&, const std::string&> OnLogAdded;

private:
    static std::string _logFilePath;
    static std::mutex _logMutex;
    static std::once_flag _initFlag;
    static constexpr size_t MAX_LOG_SIZE = 5 * 1024 * 1024; // 5MB

    static const char* LevelToString(Level level);
    static std::string FormatLogEntry(Level level, const std::string& message);
    static std::string FormatString(const char* format, va_list args);
    static void CheckLogFileSize();
    static void LogImpl(Level level, const std::string& message);
};

#endif //EASYMIC_LOGGER_HPP
