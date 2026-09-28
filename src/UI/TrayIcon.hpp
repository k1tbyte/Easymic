#pragma once

#include <windows.h>
#include <shellapi.h>
#include <string>

/// The app's icon in the notification area.
class TrayIcon {
public:
    TrayIcon() = default;
    ~TrayIcon() {
        Remove();
    }

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    bool Create(HWND hwnd, UINT id, HICON icon, const std::wstring& tooltip, UINT callbackMessage) {
        if (_isCreated) {
            return false;
        }

        _iconData.cbSize = sizeof(NOTIFYICONDATAW);
        _iconData.hWnd = hwnd;
        _iconData.uID = id;
        _iconData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        _iconData.hIcon = icon;
        _iconData.uCallbackMessage = callbackMessage;

        wcsncpy_s(_iconData.szTip, tooltip.c_str(), _TRUNCATE);

        _isCreated = Shell_NotifyIconW(NIM_ADD, &_iconData);
        return _isCreated;
    }

    bool UpdateIcon(HICON icon) {
        if (!_isCreated) {
            return false;
        }

        _iconData.hIcon = icon;
        return Shell_NotifyIconW(NIM_MODIFY, &_iconData);
    }

    bool UpdateTooltip(const std::wstring& tooltip) {
        if (!_isCreated) {
            return false;
        }

        wcsncpy_s(_iconData.szTip, tooltip.c_str(), _TRUNCATE);
        return Shell_NotifyIconW(NIM_MODIFY, &_iconData);
    }

    void Remove() {
        if (!_isCreated) {
            return;
        }

        Shell_NotifyIconW(NIM_DELETE, &_iconData);
        _isCreated = false;
    }

private:
    NOTIFYICONDATAW _iconData{};
    bool _isCreated = false;
};

