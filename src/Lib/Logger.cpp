#include "Logger.hpp"
#include <filesystem>
#include <iomanip>
#include <windows.h>
#include <fstream>
#include <sstream>

#define _CRT_SECURE_NO_WARNINGS

std::string Logger::logFilePath_;
std::mutex Logger::logMutex_;
bool Logger::initialized_ = false;
Event<Logger::Level, const std::string&, const std::string&> Logger::OnLogAdded;
Event<> Logger::OnLogCleared;


void Logger::Initialize() {

    if (initialized_) {
        return;
    }

    wchar_t modulePath[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, modulePath, MAX_PATH)) {
        return;
    }

    std::wstring wPath(modulePath);
    std::string logPath(wPath.begin(), wPath.end());

    size_t pos = logPath.find_last_of("\\");
    if (pos != std::string::npos) {
        logPath = logPath.substr(0, pos + 1) + "easymic.log";
    } else {
        logPath = "easymic.log";
    }

    std::lock_guard<std::mutex> lock(logMutex_);
    logFilePath_ = logPath;
    initialized_ = true;
    
    // Create directory if needed
    std::filesystem::path path(logPath);
    std::filesystem::create_directories(path.parent_path());
    
    // Check log file size and delete if too large
    CheckLogFileSize();
}

void Logger::CheckLogFileSize() {
    if (!std::filesystem::exists(logFilePath_)) {
        return;
    }
    
    try {
        size_t fileSize = std::filesystem::file_size(logFilePath_);
        if (fileSize > MAX_LOG_SIZE) {
            // Simply delete the file and start fresh
            std::filesystem::remove(logFilePath_);
        }
    } catch (...) {
        // Ignore errors
    }
}

std::string Logger::FormatString(const char* format, va_list args) {
    va_list argsCopy;
    va_copy(argsCopy, args);
    
    int size = vsnprintf(nullptr, 0, format, argsCopy);
    va_end(argsCopy);
    
    if (size < 0) {
        return "Format error";
    }
    
    std::string result(size + 1, '\0');
    vsnprintf(&result[0], size + 1, format, args);
    result.resize(size);
    
    return result;
}

void Logger::LogImpl(Level level, const std::string& message) {
    Initialize();
    if (!initialized_) {
        return;
    }

    std::lock_guard<std::mutex> lock(logMutex_);
    
    std::string formattedEntry = FormatLogEntry(level, message);
    
    std::ofstream logFile(logFilePath_, std::ios::app);
    if (logFile.is_open()) {
        logFile << formattedEntry << std::endl;
        logFile.close();
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
    if (!initialized_) {
        return "";
    }
    
    std::lock_guard<std::mutex> lock(logMutex_);
    
    std::ostringstream result;
    std::ifstream logFile(logFilePath_);
    if (!logFile.is_open()) {
        return "";
    }
    
    std::string line;
    while (std::getline(logFile, line)) {
        result << line << "\r\n";
    }
    
    return result.str();
}

void Logger::ClearLog() {
    Initialize();
    if (!initialized_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(logMutex_);
    
    std::ofstream logFile(logFilePath_, std::ios::trunc);
    if (logFile.is_open()) {
        logFile.close();
    }
    
    // Write clear message
    std::string formattedEntry = FormatLogEntry(Level::Info, "Log cleared");
    std::ofstream reopenFile(logFilePath_, std::ios::app);
    if (reopenFile.is_open()) {
        reopenFile << formattedEntry << std::endl;
        reopenFile.close();
    }
    
    OnLogCleared();
}

std::string Logger::FormatLogEntry(Level level, const std::string& message) {
    return "[" + GetTimestamp() + "] [" + LevelToString(level) + "] " + message;
}

std::string Logger::GetTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    struct tm timeInfo;
    localtime_s(&timeInfo, &time_t);
    
    std::ostringstream result;
    result << std::put_time(&timeInfo, "%Y-%m-%d %H:%M");
    return result.str();
}

std::string Logger::LevelToString(Level level) {
    switch (level) {
        case Level::Info:    return "INFO";
        case Level::Warning: return "WARN";
        case Level::Error:   return "ERROR";
        default:             return "UNKNOWN";
    }
}
