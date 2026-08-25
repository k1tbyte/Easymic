#ifndef EASYMIC_MAINWINDOW_V2_HPP
#define EASYMIC_MAINWINDOW_V2_HPP

#include <functional>
#include <memory>
#include <string>

#include "../Core/BaseWindow.hpp"
#include "../Components/TrayIcon.hpp"
#include "../Components/TrayIconTheme.hpp"
#include "../Components/GdiRenderer.hpp"
#include "../Components/LayeredWindow.hpp"
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
    static void PostNotification(HWND target, const std::wstring& text);

    // View model delegation - single subscriber each, assigned once during Init
    std::function<void(UINT_PTR commandId)> OnTrayMenu;
    std::function<void()> OnClose;
    std::function<void(UINT_PTR timerId)> OnTimer;
    std::function<void()> OnThemeChanged;
    std::function<void()> OnRelayout;
    std::function<void(std::wstring text)> OnNotification;
    GDIRenderer::RenderCallback OnRender;

    /// Anything that changes what the indicator should look like - only the view model knows how
    /// to lay it out, so callers say what happened instead of resizing the window themselves.
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

    static constexpr UINT WM_TRAYICON = WM_USER + 1;
    /// WM_APP, not WM_USER: the timer ids the view model owns share nothing but the number line
    static constexpr UINT WM_SHOW_NOTIFICATION = WM_APP + 1;
};

#endif //EASYMIC_MAINWINDOW_V2_HPP
