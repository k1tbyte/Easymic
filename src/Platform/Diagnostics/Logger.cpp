#include "Logger.hpp"
#include <cstdio>
#include <windows.h>

#include "Platform/File.hpp"

std::wstring Logger::_logFilePath;
std::mutex Logger::_logMutex;
std::once_flag Logger::_initFlag;
Event<Logger::Level, const std::string&, const std::string&> Logger::OnLogAdded;


void Logger::Initialize() {
    std::call_once(_initFlag, [] {
        std::lock_guard lock(_logMutex);
        _logFilePath = File::NextToExe(L"easylauncher.log");
        CheckLogFileSize();
    });
}

void Logger::CheckLogFileSize() {
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (GetFileAttributesExW(_logFilePath.c_str(), GetFileExInfoStandard, &info)
        && (info.nFileSizeHigh || info.nFileSizeLow > MAX_LOG_SIZE)) {
        DeleteFileW(_logFilePath.c_str());
    }
}

std::string Logger::FormatString(const char* format, va_list args) {
    va_list argsCopy;
    va_copy(argsCopy, args);
    const int size = vsnprintf(nullptr, 0, format, argsCopy);
    va_end(argsCopy);

    if (size < 0) {
        return "Format error";
    }

    std::string result(size, '\0');
    vsnprintf(result.data(), size + 1, format, args);
    return result;
}

void Logger::LogImpl(Level level, const std::string& message) {
    Initialize();
    if (_logFilePath.empty()) {
        return;
    }

    std::string formattedEntry;
    {
        std::lock_guard lock(_logMutex);
        formattedEntry = FormatLogEntry(level, message);

        // CRLF: the log ends up in an edit control
        std::string line;
        for (const char c : formattedEntry) {
            if (c == '\n') {
                line += '\r';
            }
            line += c;
        }
        File::Write(_logFilePath.c_str(), line + "\r\n", true);
    }

    // No lock held: a subscriber that has to reach the UI thread would deadlock against the UI thread's own logging
    OnLogAdded(level, message, formattedEntry);
}

void Logger::Log(Level level, const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::string message = FormatString(format, args);
    va_end(args);

    LogImpl(level, message);
}

std::string Logger::GetLogText() {
    Initialize();
    if (_logFilePath.empty()) {
        return {};
    }

    std::lock_guard lock(_logMutex);
    return File::Read(_logFilePath.c_str()).value_or(std::string{});
}

std::string Logger::FormatLogEntry(Level level, const std::string& message) {
    SYSTEMTIME time;
    GetLocalTime(&time);

    char header[32];
    snprintf(header, sizeof(header), "[%04u-%02u-%02u %02u:%02u] [%s] ",
             time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, LevelToString(level));

    return header + message;
}

const char* Logger::LevelToString(Level level) {
    switch (level) {
        case Level::Info:    return "INFO";
        case Level::Warning: return "WARN";
        case Level::Error:   return "ERROR";
        default:             return "UNKNOWN";
    }
}
