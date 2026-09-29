#pragma once

#include <windows.h>
#include <wrl/client.h>
#include "Diagnostics/Logger.hpp"

using Microsoft::WRL::ComPtr;

#define APP_NAME L"EasyLauncher"

/// No app name in it: a rename would silently break the single-instance check.
#define MUTEX_NAME L"{8963D562-E35B-492A-A3D2-5FD724CE24B2}"

#define CONFIG_NAME L"config.json"

/// The repository kept its old name; the update check's API URL is built from it.
#define REPO_NAME   "Easymic"
#define DEV_NAME    "k1tbyte"
#define REPO_URL    "https://github.com/" DEV_NAME "/" REPO_NAME

#define CHECK_HR(hr, msg) \
    if (FAILED(hr)) { \
        LOG_ERROR("%s: 0x%08lX", msg, hr); \
        return false; \
    }

#define LOGGING_ENABLED 0

// Format must be a literal: a runtime string with a stray '%' reads arguments that were never passed.
#define LOG_ERROR(...) Logger::Log(Logger::Level::Error, __VA_ARGS__)

#if LOGGING_ENABLED
#define LOG_INFO(...) Logger::Log(Logger::Level::Info, __VA_ARGS__)
#define LOG_WARNING(...) Logger::Log(Logger::Level::Warning, __VA_ARGS__)
#else
#define LOG_INFO(...)
#define LOG_WARNING(...)
#endif

