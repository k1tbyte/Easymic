//
// Created by kitbyte on 04.11.2025.
//

#ifndef EASYMIC_WIN32HOOK_H
#define EASYMIC_WIN32HOOK_H

#include <windows.h>
#include <memory>

class Win32Hook {
    HHOOK _hook;

    Win32Hook(int idHook, HOOKPROC callback, HINSTANCE hInstance, DWORD dwThreadId) {
        _hook = SetWindowsHookEx(idHook, callback, hInstance, dwThreadId);
    }
public:
    Win32Hook() = delete;
    Win32Hook(const Win32Hook&) = delete;
    Win32Hook& operator=(const Win32Hook&) = delete;

    static std::unique_ptr<Win32Hook> Create(int idHook, HOOKPROC callback, HINSTANCE hInstance, DWORD dwThreadId) {
        return std::unique_ptr<Win32Hook>(new Win32Hook(idHook, callback, hInstance, dwThreadId));
    }

    ~Win32Hook() {
        if (_hook) {
            UnhookWindowsHookEx(_hook);
        }
    }
};

#endif //EASYMIC_WIN32HOOK_H