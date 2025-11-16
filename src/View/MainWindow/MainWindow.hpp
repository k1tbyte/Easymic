#ifndef EASYMIC_MAINWINDOW_V2_HPP
#define EASYMIC_MAINWINDOW_V2_HPP

#include "../Core/BaseWindow.hpp"
#include "../Components/TrayIcon.hpp"
#include "../Components/GdiRenderer.hpp"
#include "../Components/LayeredWindow.hpp"
#include <memory>

#include "AppConfig.hpp"

class MainWindow final : public BaseWindow {
public:
    // Callbacks for logic delegation
    using OnTrayClickCallback = std::function<void()>;
    using OnTrayMenuCallback = std::function<void(UINT_PTR commandId)>;
    using OnCloseCallback = std::function<void()>;
    using OnTimerCallback = std::function<void(UINT_PTR timerId)>;

    static constexpr auto StyleEx = WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW;
    static constexpr auto Style = WS_POPUP | WS_DISABLED;

    struct WindowConfig {
        LPCWSTR windowTitle = L"MainWindow";
        LPCWSTR className = L"MainWindowClass";
    };

    explicit MainWindow(HINSTANCE hInstance, AppConfig& appConfig);
    ~MainWindow() override;

    bool Initialize(WindowConfig config);

    // Tray icon management
    bool CreateTrayIcon(HICON icon, const std::wstring& tooltip);
    void UpdateTrayIcon(HICON icon);
    void UpdateTrayTooltip(const std::wstring& tooltip);

    std::shared_ptr<BaseWindow> SetWidth(LONG width) override;
    std::shared_ptr<BaseWindow> SetHeight(LONG height) override;

    // Callbacks
    void SetOnTrayClick(OnTrayClickCallback callback) { _onTrayClick = std::move(callback); }
    void SetOnTrayMenu(OnTrayMenuCallback callback) { _onTrayMenu = std::move(callback); }
    void SetOnClose(OnCloseCallback callback) { _onClose = std::move(callback); }

    // Rendering
    void SetRenderCallback(GDIRenderer::RenderCallback callback) {
        _onRender = std::move(callback);
    }

    void ToggleInteractivity(bool interactive) const {

        auto *hwnd = GetEffectiveHandle();

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

    void SetTimerCallback(OnTimerCallback callback) {
        _onTimer = std::move(callback);
    }

private:
    bool RegisterWindowClass(const WindowConfig& config) const;
    void SetupMessageHandlers();

    // Message handlers
    LRESULT OnCreate(WPARAM wParam, LPARAM lParam);
    LRESULT OnDestroy(WPARAM wParam, LPARAM lParam);
    LRESULT OnPaint(WPARAM wParam, LPARAM lParam);
    LRESULT OnClose(WPARAM wParam, LPARAM lParam);
    LRESULT OnTrayIconMessage(WPARAM wParam, LPARAM lParam);
    LRESULT OnExitSizeMove(WPARAM wParam, LPARAM lParam);

    void ShowTrayContextMenu();

    const AppConfig& appConfig_;
    WindowConfig config_;
    std::unique_ptr<TrayIcon> trayIcon_;

    // Callbacks
    OnTrayClickCallback _onTrayClick;
    OnTrayMenuCallback _onTrayMenu;
    OnCloseCallback _onClose;
    OnTimerCallback _onTimer;
    GDIRenderer::RenderCallback _onRender;

    // Resources
    HICON currentIcon_ = nullptr;
    std::wstring _currentTooltip;

    static constexpr UINT WM_TRAYICON = WM_USER + 1;
};

#endif //EASYMIC_MAINWINDOW_V2_HPP

