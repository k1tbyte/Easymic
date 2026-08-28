#include "Logger.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <windows.h>

std::string Logger::_logFilePath;
std::mutex Logger::_logMutex;
std::once_flag Logger::_initFlag;
Event<Logger::Level, const std::string&, const std::string&> Logger::OnLogAdded;


void Logger::Initialize() {
    std::call_once(_initFlag, [] {
        wchar_t modulePath[MAX_PATH];
        if (!GetModuleFileNameW(nullptr, modulePath, MAX_PATH)) {
            return;
        }

        std::lock_guard lock(_logMutex);
        _logFilePath = (std::filesystem::path(modulePath).parent_path() / L"easylauncher.log").string();
        CheckLogFileSize();
    });
}

void Logger::CheckLogFileSize() {
    std::error_code ec;
    if (std::filesystem::file_size(_logFilePath, ec) > MAX_LOG_SIZE) {
        std::filesystem::remove(_logFilePath, ec);
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

        // Text mode on purpose: the log ends up in an edit control that wants CRLF
        if (std::ofstream logFile(_logFilePath, std::ios::app); logFile.is_open()) {
            logFile << formattedEntry << std::endl;
        }
    }

    // Raised with nothing held: a log line can come from any thread, and a subscriber that has
    // to reach the UI thread would otherwise deadlock against the UI thread's own logging
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

    // Binary: the file already holds the CRLF the edit control expects
    std::ifstream logFile(_logFilePath, std::ios::binary | std::ios::ate);
    if (!logFile.is_open()) {
        return {};
    }

    const auto size = logFile.tellg();
    if (size <= 0) {
        return {};
    }

    std::string text(size, '\0');
    logFile.seekg(0, std::ios::beg);
    logFile.read(text.data(), size);
    return text;
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
