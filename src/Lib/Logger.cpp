#include "Logger.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <windows.h>

std::string Logger::logFilePath_;
std::mutex Logger::logMutex_;
std::once_flag Logger::initFlag_;
Event<Logger::Level, const std::string&, const std::string&> Logger::OnLogAdded;
Event<> Logger::OnLogCleared;


void Logger::Initialize() {
    std::call_once(initFlag_, [] {
        wchar_t modulePath[MAX_PATH];
        if (!GetModuleFileNameW(nullptr, modulePath, MAX_PATH)) {
            return;
        }

        std::lock_guard lock(logMutex_);
        logFilePath_ = (std::filesystem::path(modulePath).parent_path() / L"easymic.log").string();
        CheckLogFileSize();
    });
}

void Logger::CheckLogFileSize() {
    std::error_code ec;
    if (std::filesystem::file_size(logFilePath_, ec) > MAX_LOG_SIZE) {
        std::filesystem::remove(logFilePath_, ec);
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
    if (logFilePath_.empty()) {
        return;
    }

    std::lock_guard lock(logMutex_);

    const std::string formattedEntry = FormatLogEntry(level, message);

    // Text mode on purpose: the log ends up in an edit control that wants CRLF
    if (std::ofstream logFile(logFilePath_, std::ios::app); logFile.is_open()) {
        logFile << formattedEntry << std::endl;
    }

    OnLogAdded(level, message, formattedEntry);
}

void Logger::Log(Level level, const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::string message = FormatString(format, args);
    va_end(args);

    LogImpl(level, message);
}

void Logger::Info(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::string message = FormatString(format, args);
    va_end(args);

    LogImpl(Level::Info, message);
}

void Logger::Warning(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::string message = FormatString(format, args);
    va_end(args);

    LogImpl(Level::Warning, message);
}

void Logger::Error(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::string message = FormatString(format, args);
    va_end(args);

    LogImpl(Level::Error, message);
}

std::string Logger::GetLogText() {
    Initialize();
    if (logFilePath_.empty()) {
        return {};
    }

    std::lock_guard lock(logMutex_);

    // Binary: the file already holds the CRLF the edit control expects
    std::ifstream logFile(logFilePath_, std::ios::binary | std::ios::ate);
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

void Logger::ClearLog() {
    Initialize();
    if (logFilePath_.empty()) {
        return;
    }

    {
        std::lock_guard lock(logMutex_);
        std::ofstream logFile(logFilePath_, std::ios::trunc);
    }

    LogImpl(Level::Info, "Log cleared");
    OnLogCleared();
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
