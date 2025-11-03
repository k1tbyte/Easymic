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

    static constexpr auto StyleEx = WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW;
    static constexpr auto Style = WS_POPUP;

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
    GDIRenderer::RenderCallback _onRender;

    // Resources
    HICON currentIcon_ = nullptr;
    std::wstring _currentTooltip;

    static constexpr UINT WM_TRAYICON = WM_USER + 1;
};

#endif //EASYMIC_MAINWINDOW_V2_HPP

