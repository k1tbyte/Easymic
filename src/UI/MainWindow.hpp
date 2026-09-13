#pragma once

#include <functional>
#include <memory>
#include <string>

#include "BaseWindow.hpp"
#include "TrayIcon.hpp"
#include "LayeredWindow.hpp"
#include "AppConfig.hpp"

class MainWindow final : public BaseWindow {
public:
    static constexpr auto StyleEx = WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW;
    static constexpr auto Style = WS_POPUP | WS_DISABLED;

    struct WindowConfig {
        LPCWSTR windowTitle = L"MainWindow";
        LPCWSTR className = L"MainWindowClass";
    };

    explicit MainWindow(HINSTANCE hInstance, AppConfig& appConfig);
    ~MainWindow() override = default;

    bool Initialize(WindowConfig config);

    // Tray icon management
    bool CreateTrayIcon(HICON icon, const std::wstring& tooltip);
    void UpdateTrayIcon(HICON icon);
    void UpdateTrayTooltip(const std::wstring& tooltip);

    /**
     * @brief Hands a notification text to the window from any thread.
     *
     * The text travels with the message, so nothing is shared and nothing races. Takes no owner
     * state: a captured command may answer long after its action is gone, and a window that no
     * longer exists only costs a failed post.
     */
    static void PostNotification(const std::wstring& text);

    /// Asks the overlay to measure itself again, from any thread. Carries nothing but the
    /// request: whatever changed, the layer that changed it already knows.
    static void PostRelayout();

    // View model delegation - single subscriber each, assigned once during Init
    std::function<void(UINT_PTR commandId)> OnTrayMenu;
    std::function<void()> OnClose;
    std::function<void(UINT_PTR timerId)> OnTimer;
    std::function<void()> OnThemeChanged;
    std::function<void()> OnRelayout;
    std::function<void(std::wstring text)> OnNotification;
    LayeredWindow::RenderCallback OnRender;

    /// Anything that changes what the overlay should look like. Callers say what happened; the
    /// surface is what decides how wide the strip is and whether it is on screen at all.
    void Relayout() const {
        if (OnRelayout) {
            OnRelayout();
        }
    }

    void ToggleInteractivity(bool interactive) const {
        HWND hwnd = GetEffectiveHandle();

        LONG_PTR dwExStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
        LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
        if (interactive) {
            dwExStyle &= ~WS_EX_TRANSPARENT; // Remove transparent
            style &= ~WS_DISABLED;
        } else {
            dwExStyle |= WS_EX_TRANSPARENT;
            style |= WS_DISABLED;
        }

        SetWindowLongPtr(hwnd, GWL_EXSTYLE, dwExStyle);
        SetWindowLongPtr(hwnd, GWL_STYLE, style);
    }

private:
    bool RegisterWindowClass(const WindowConfig& config) const;

    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) override;
    LRESULT OnDestroy();
    LRESULT OnPaint();
    LRESULT OnTrayIconMessage(LPARAM lParam);

    void ShowTrayContextMenu();

    const AppConfig& _config;
    TrayIcon _trayIcon;

    // Not owned: LoadIcon returns shared icons, the view model keeps them alive
    HICON _currentIcon = nullptr;
    std::wstring _currentTooltip;

    /// Registered at runtime, so it cannot be a switch label - checked before the switch.
    UINT _taskbarCreatedMessage = 0;

    /// Where the two posters send. One copy of the handle rather than one per caller, and it is
    /// deliberately never cleared: posting to a destroyed window fails harmlessly, while posting
    /// to a null one would queue a thread message nobody dispatches.
    static inline HWND _postTarget = nullptr;

    static constexpr UINT WM_TRAYICON = WM_USER + 1;
    /// WM_APP, not WM_USER: the timer ids the overlay owns share nothing but the number line
    static constexpr UINT WM_SHOW_NOTIFICATION = WM_APP + 1;
    static constexpr UINT WM_OVERLAY_RELAYOUT = WM_APP + 2;
};

