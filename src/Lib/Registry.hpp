#pragma once

#include <windows.h>

namespace Registry {

    inline constexpr auto AutoStartupKey = LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\Run)";

    inline bool IsInAutoStartup(LPCWSTR appName) {
        return RegGetValueW(HKEY_CURRENT_USER, AutoStartupKey, appName,
                            RRF_RT_REG_SZ, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    }

    inline bool AddToAutoStartup(LPCWSTR appName) {
        wchar_t modulePath[MAX_PATH];
        const DWORD length = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
        if (length == 0) {
            return false;
        }

        return RegSetKeyValueW(HKEY_CURRENT_USER, AutoStartupKey, appName, REG_SZ,
                               modulePath, (length + 1) * sizeof(wchar_t)) == ERROR_SUCCESS;
    }

    inline bool RemoveFromAutoStartup(LPCWSTR appName) {
        return RegDeleteKeyValueW(HKEY_CURRENT_USER, AutoStartupKey, appName) == ERROR_SUCCESS;
    }
}

