#ifndef EASYMIC_TRAYICON_HPP
#define EASYMIC_TRAYICON_HPP

#include <windows.h>
#include <shellapi.h>
#include <string>

/**
 * @brief Управление иконкой в системном трее
 * Single Responsibility: только работа с tray icon
 */
class TrayIcon {
public:
    TrayIcon() = default;
    ~TrayIcon() {
        Remove();
    }

    // Запрет копирования
    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    bool Create(HWND hwnd, UINT id, HICON icon, const std::wstring& tooltip, UINT callbackMessage) {
        if (isCreated_) {
            return false;
        }

        iconData_.cbSize = sizeof(NOTIFYICONDATAW);
        iconData_.hWnd = hwnd;
        iconData_.uID = id;
        iconData_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        iconData_.hIcon = icon;
        iconData_.uCallbackMessage = callbackMessage;

        wcsncpy_s(iconData_.szTip, tooltip.c_str(), _TRUNCATE);

        isCreated_ = Shell_NotifyIconW(NIM_ADD, &iconData_);
        return isCreated_;
    }

    bool UpdateIcon(HICON icon) {
        if (!isCreated_) {
            return false;
        }

        iconData_.hIcon = icon;
        return Shell_NotifyIconW(NIM_MODIFY, &iconData_);
    }

    bool UpdateTooltip(const std::wstring& tooltip) {
        if (!isCreated_) {
            return false;
        }

        wcsncpy_s(iconData_.szTip, tooltip.c_str(), _TRUNCATE);
        return Shell_NotifyIconW(NIM_MODIFY, &iconData_);
    }

    void Remove() {
        if (!isCreated_) {
            return;
        }

        Shell_NotifyIconW(NIM_DELETE, &iconData_);
        isCreated_ = false;
    }

private:
    NOTIFYICONDATAW iconData_{};
    bool isCreated_ = false;
};

#endif //EASYMIC_TRAYICON_HPP
