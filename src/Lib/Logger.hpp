#ifndef EASYMIC_LOGGER_HPP
#define EASYMIC_LOGGER_HPP

#include <string>
#include <mutex>
#include <chrono>
#include <cstdarg>
#include "Event.hpp"

/**
 * @brief Simple file-based logger with thread-safe operations and printf-style formatting
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
    static void Info(const char* format, ...);
    static void Warning(const char* format, ...);
    static void Error(const char* format, ...);
    
    static std::string GetLogText();
    static void ClearLog();
    
    // Events for UI updates
    static Event<Level, const std::string&, const std::string&> OnLogAdded;
    static Event<> OnLogCleared;

private:
    static std::string logFilePath_;
    static std::mutex logMutex_;
    static bool initialized_;
    static constexpr size_t MAX_LOG_SIZE = 5 * 1024 * 1024; // 5MB

    static std::string GetTimestamp();
    static std::string LevelToString(Level level);
    static std::string FormatLogEntry(Level level, const std::string& message);
    static std::string FormatString(const char* format, va_list args);
    static void CheckLogFileSize();
    static void LogImpl(Level level, const std::string& message);
};

#endif //EASYMIC_LOGGER_HPP
