#pragma once

#include <windows.h>

namespace Registry {

    inline constexpr auto AutoStartupKey = LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\Run)";

    inline bool IsInAutoStartup(LPCWSTR appName) {
        return RegGetValueW(HKEY_CURRENT_USER, AutoStartupKey, appName,
                            RRF_RT_REG_SZ, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    }

    inline bool AddToAutoStartup(LPCWSTR appName) {
        // Quoted, or a path with spaces is a command line Windows has to guess at
        wchar_t command[MAX_PATH + 2] = L"\"";
        const DWORD length = GetModuleFileNameW(nullptr, command + 1, MAX_PATH);
        if (length == 0 || length == MAX_PATH) {
            return false;
        }
        command[length + 1] = L'"';
        command[length + 2] = L'\0';

        return RegSetKeyValueW(HKEY_CURRENT_USER, AutoStartupKey, appName, REG_SZ,
                               command, (length + 3) * sizeof(wchar_t)) == ERROR_SUCCESS;
    }

    inline bool RemoveFromAutoStartup(LPCWSTR appName) {
        return RegDeleteKeyValueW(HKEY_CURRENT_USER, AutoStartupKey, appName) == ERROR_SUCCESS;
    }
}

