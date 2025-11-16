//
// Created by kitbyte on 24.10.2025.
//

#ifndef EASYMICTESTING_PCH_H
#define EASYMICTESTING_PCH_H

#include <windows.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

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

// Forward declare logger functions for global access
class Logger;
void InitializeLogger();

// Conditional logging macros - can be disabled in release builds
#ifndef NDEBUG
    #define LOG_INFO(...) Logger::Info(__VA_ARGS__)
    #define LOG_WARNING(...) Logger::Warning(__VA_ARGS__)
    #define LOG_ERROR(...) Logger::Error(__VA_ARGS__)
#else
    #define LOG_INFO(...) ((void)0)
    #define LOG_WARNING(...) ((void)0)
    #define LOG_ERROR(...) ((void)0)
#endif


#endif //EASYMICTESTING_PCH_H