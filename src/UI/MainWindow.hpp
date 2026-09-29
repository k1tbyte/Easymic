#pragma once

#include <memory>
#include <string>

#include "AppConfig.hpp"
#include "BaseWindow.hpp"
#include "Core/Feedback.hpp"
#include "Overlay/OverlaySurface.hpp"
#include "Overlay/OverlayWindow.hpp"
#include "Settings/SettingsWindow.hpp"
#include "TrayIcon.hpp"

/// The frame: hosts the overlay and tray icon, opens settings, and restores the config when they close.
class MainWindow final : public BaseWindow {
public:
    MainWindow(HINSTANCE hInstance, AppConfig& config, Feedback& feedback);
    ~MainWindow() override = default;

    bool Initialize();

    /// True when the settings window took the message: Tab, Enter and Esc there need IsDialogMessage.
    bool PreTranslate(MSG& message) const;

    /// From any thread, with no owner state: a captured command may answer after its action is gone.
    static void PostNotification(std::wstring text);
    static void PostRelayout();
    static void PostTrayRefresh();

private:
    bool RegisterWindowClass() const;

    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) override;
    LRESULT OnCommand(UINT command);
    LRESULT OnDestroy();
    LRESULT OnPaint();
    LRESULT OnThemeChange(LPARAM lParam);
    LRESULT OnTrayIconMessage(LPARAM lParam);

    void ShowTrayContextMenu();
    void RefreshTray();

    void SuspendActivity();
    void RestoreConfig();
    void OpenSettings();

    AppConfig& _cfg;
    Feedback& _feedback;
    OverlayWindow _window;
    OverlaySurface _overlay;
    std::unique_ptr<SettingsWindow> _settings;
    TrayIcon _trayIcon;

    UINT _taskbarCreatedMessage = 0;

    /// Never cleared: a post to a destroyed window fails harmlessly, one to null queues a thread message nobody dispatches.
    static inline HWND _postTarget = nullptr;

    static constexpr UINT WM_TRAYICON = WM_USER + 1;
    static constexpr UINT WM_SHOW_NOTIFICATION = WM_APP + 1;
    static constexpr UINT WM_OVERLAY_RELAYOUT = WM_APP + 2;
    static constexpr UINT WM_TRAY_REFRESH = WM_APP + 3;
    static constexpr UINT TrayMenuFirst = 41000;
};
