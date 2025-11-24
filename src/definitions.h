//
// Created by kitbyte on 24.10.2025.
//

#ifndef EASYMIC_DEFINITIONS_H
#define EASYMIC_DEFINITIONS_H

#include <windows.h>
#include <wrl/client.h>
#include "Logger.hpp"

using Microsoft::WRL::ComPtr;

#define APP_NAME L"Easymic"
#define MUTEX_NAME APP_NAME L"-8963D562-E35B-492A-A3D2-5FD724CE24B2"
#define CONFIG_NAME L"conf.b"

#define REPO_NAME   "Easymic"
#define DEV_NAME    "k1tbyte"
#define REPO_URL    "https://github.com/" DEV_NAME "/" REPO_NAME
#define GITHUB_OWNER DEV_NAME
#define GITHUB_REPO REPO_NAME 

#define CHECK_HR(hr, msg) \
    if (FAILED(hr)) { \
        printf("%s FAILED: 0x%08lX\n", msg, hr); \
        throw std::runtime_error(msg); \
    }

// Logging macros - always enabled for now
#define LOG_INFO(...) Logger::Info(__VA_ARGS__)
#define LOG_WARNING(...) Logger::Warning(__VA_ARGS__)
#define LOG_ERROR(...) Logger::Error(__VA_ARGS__)

#endif //EASYMIC_DEFINITIONS_H