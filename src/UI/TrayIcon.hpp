#pragma once

#include <windows.h>
#include <shellapi.h>
#include <string>

class TrayIcon {
public:
    TrayIcon() = default;
    ~TrayIcon() {
        Remove();
    }

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    bool Create(HWND hwnd, UINT id, UINT callbackMessage) {
        _iconData.cbSize = sizeof(NOTIFYICONDATAW);
        _iconData.hWnd = hwnd;
        _iconData.uID = id;
        _iconData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        _iconData.uCallbackMessage = callbackMessage;
        return Add();
    }

    bool Add() {
        _isCreated = Shell_NotifyIconW(NIM_ADD, &_iconData);
        return _isCreated;
    }

    /// A null icon keeps the current one. The icon is not owned: providers keep theirs alive.
    bool Update(HICON icon, const std::wstring& tooltip) {
        if (!icon) {
            icon = _iconData.hIcon;
        }
        if (!_isCreated || (icon == _iconData.hIcon && tooltip == _iconData.szTip)) {
            return false;
        }

        _iconData.hIcon = icon;
        wcsncpy_s(_iconData.szTip, tooltip.c_str(), _TRUNCATE);
        return Shell_NotifyIconW(NIM_MODIFY, &_iconData);
    }

    /// Also clears the flag when the shell already lost the icon and NIM_DELETE fails.
    void Remove() {
        if (_isCreated) {
            Shell_NotifyIconW(NIM_DELETE, &_iconData);
            _isCreated = false;
        }
    }

private:
    NOTIFYICONDATAW _iconData{};
    bool _isCreated = false;
};
