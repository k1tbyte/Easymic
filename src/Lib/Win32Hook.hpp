//
// Created by kitbyte on 04.11.2025.
//

#pragma once

#include <windows.h>
#include <memory>

class Win32Hook {
    HHOOK _hook;
    /// Sampled right here because nothing else can: the next Win32 call, ours or the caller's
    /// second hook, overwrites the thread's last error before anyone gets to read it.
    DWORD _error;

    Win32Hook(int idHook, HOOKPROC callback, HINSTANCE hInstance, DWORD dwThreadId) {
        _hook = SetWindowsHookEx(idHook, callback, hInstance, dwThreadId);
        _error = _hook ? ERROR_SUCCESS : GetLastError();
    }
public:
    Win32Hook() = delete;
    Win32Hook(const Win32Hook&) = delete;
    Win32Hook& operator=(const Win32Hook&) = delete;

    static std::unique_ptr<Win32Hook> Create(int idHook, HOOKPROC callback, HINSTANCE hInstance, DWORD dwThreadId) {
        return std::unique_ptr<Win32Hook>(new Win32Hook(idHook, callback, hInstance, dwThreadId));
    }

    bool IsValid() const { return _hook != nullptr; }

    /// Why SetWindowsHookEx failed. Meaningless once IsValid is true.
    DWORD LastError() const { return _error; }

    ~Win32Hook() {
        if (_hook) {
            UnhookWindowsHookEx(_hook);
        }
    }
};

