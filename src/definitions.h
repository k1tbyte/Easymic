//
// Created by kitbyte on 24.10.2025.
//

#pragma once

#include <windows.h>
#include <wrl/client.h>
#include "Logger.hpp"

using Microsoft::WRL::ComPtr;

#define APP_NAME L"EasyLauncher"

/// Identity only, no app name in it: the name never bought uniqueness - the GUID does that - nor
/// a namespace, which is what a Local\ or Global\ prefix is for. Carrying one only means a rename
/// can silently break the single-instance check.
#define MUTEX_NAME L"{8963D562-E35B-492A-A3D2-5FD724CE24B2}"

#define CONFIG_NAME L"config.json"

/// Still the old name - the GitHub repository has not been renamed, and this feeds the update
/// check's API URL.
#define REPO_NAME   "Easymic"
#define DEV_NAME    "k1tbyte"
#define REPO_URL    "https://github.com/" DEV_NAME "/" REPO_NAME
#define GITHUB_OWNER DEV_NAME
#define GITHUB_REPO REPO_NAME

/// A COM failure in the audio layer is not fatal - the app stays up, just without that device.
#define CHECK_HR(hr, msg) \
    if (FAILED(hr)) { \
        LOG_ERROR("%s: 0x%08lX", msg, hr); \
        return false; \
    }

#define LOGGING_ENABLED 0

// The first argument is a printf format string and must be a literal - a runtime string with a
// stray '%' in it reads arguments that were never passed.
#define LOG_ERROR(...) Logger::Log(Logger::Level::Error, __VA_ARGS__)

#if LOGGING_ENABLED
#define LOG_INFO(...) Logger::Log(Logger::Level::Info, __VA_ARGS__)
#define LOG_WARNING(...) Logger::Log(Logger::Level::Warning, __VA_ARGS__)
#else
#define LOG_INFO(...)
#define LOG_WARNING(...)
#endif

