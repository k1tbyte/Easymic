//
// Created by kitbyte on 03.11.2025.
//

#ifndef EASYMIC_UIACCESSMANAGER_HPP
#define EASYMIC_UIACCESSMANAGER_HPP

#include <windows.h>

class UIAccessManager {
public:
    UIAccessManager(const UIAccessManager&) = delete;
    UIAccessManager& operator=(const UIAccessManager&) = delete;
    UIAccessManager(UIAccessManager&&) = delete;
    UIAccessManager& operator=(UIAccessManager&&) = delete;
    UIAccessManager() = delete;

    static HWND GetOrCreateWindow(const char* key, DWORD exStyle, DWORD style);
    static bool InjectDisplayAffinity(HWND hWnd, DWORD affinity);
};
#endif //EASYMIC_UIACCESSMANAGER_HPP