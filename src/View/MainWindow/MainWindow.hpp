#ifndef EASYMIC_MAINWINDOW_V2_HPP
#define EASYMIC_MAINWINDOW_V2_HPP

#include "../Core/BaseWindow.hpp"
#include "../Components/TrayIcon.hpp"
#include "../Components/GdiRenderer.hpp"
#include <memory>

#include "AppConfig.hpp"

class MainWindow final : public BaseWindow {
public:
    // Callbacks for logic delegation
    using OnTrayClickCallback = std::function<void()>;
    using OnTrayMenuCallback = std::function<void(UINT commandId)>;
    using OnCloseCallback = std::function<void()>;

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

    // Callbacks
    void SetOnTrayClick(OnTrayClickCallback callback) { onTrayClick_ = std::move(callback); }
    void SetOnTrayMenu(OnTrayMenuCallback callback) { onTrayMenu_ = std::move(callback); }
    void SetOnClose(OnCloseCallback callback) { onClose_ = std::move(callback); }

    // Rendering
    void SetRenderCallback(LayeredWindowRenderer::RenderCallback callback) {
        renderCallback_ = std::move(callback);
    }

    void Invalidate() {
        if (!hwnd_) {
            return;
        }
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    void UpdateSize(int newSize);
    void UpdatePosition(int x, int y);

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
    std::unique_ptr<GdiPlusContext> gdiContext_;

    // Callbacks
    OnTrayClickCallback onTrayClick_;
    OnTrayMenuCallback onTrayMenu_;
    OnCloseCallback onClose_;
    LayeredWindowRenderer::RenderCallback renderCallback_;

    // Resources
    HICON currentIcon_ = nullptr;
    int currentSize_ = 32;

    static constexpr UINT WM_TRAYICON = WM_USER + 1;
};

#endif //EASYMIC_MAINWINDOW_V2_HPP

