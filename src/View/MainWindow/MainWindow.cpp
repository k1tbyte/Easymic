#include "MainWindow.hpp"

#include "Utils.hpp"
#include "../../Resources/Resource.h"

#define PADDING 2

MainWindow::MainWindow(HINSTANCE hInstance, AppConfig& appConfig)
    : BaseWindow(hInstance)
    , appConfig_(appConfig), trayIcon_(std::make_unique<TrayIcon>())
{
}

MainWindow::~MainWindow() {
    if (currentIcon_) {
        DestroyIcon(currentIcon_);
        MessageBoxA(nullptr, "Destroyed icon", "Debug", MB_OK);
    }
}

std::shared_ptr<BaseWindow> MainWindow::SetWidth(LONG width) {
    return BaseWindow::SetWidth(width * PADDING);
}

std::shared_ptr<BaseWindow> MainWindow::SetHeight(LONG height) {
    return BaseWindow::SetHeight(height * PADDING);
}

bool MainWindow::Initialize(WindowConfig config) {
    config_ = config;

    if (!RegisterWindowClass(config)) {
        return false;
    }

    const int windowSize = appConfig_.indicatorSize;
    SetWidth(windowSize);
    SetHeight(windowSize);
    SetPositionX(appConfig_.windowPosX);
    SetPositionY(appConfig_.windowPosY);

    hwnd_ = CreateWindowExW(
        StyleEx,
        config.className,
        config.windowTitle,
        Style,
        appConfig_.windowPosX,
        appConfig_.windowPosY,
        windowSize,
        windowSize,
        nullptr,
        nullptr,
        hInstance_,
        nullptr
    );

    if (!hwnd_) {
        return false;
    }

    RegisterWindow(hwnd_);
    SetupMessageHandlers();

    _viewModel->Init();

    return true;
}

bool MainWindow::RegisterWindowClass(const WindowConfig& config) const {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = StaticWindowProc;
    wc.hInstance = hInstance_;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = config.className;

    return RegisterClassExW(&wc) != 0;
}

void MainWindow::SetupMessageHandlers() {
    RegisterMessageHandler(WM_CREATE, [this](WPARAM wp, LPARAM lp) {
        return OnCreate(wp, lp);
    });

    RegisterMessageHandler(WM_DESTROY, [this](WPARAM wp, LPARAM lp) {
        return OnDestroy(wp, lp);
    });

    RegisterMessageHandler(WM_PAINT, [this](WPARAM wp, LPARAM lp) {
        return OnPaint(wp, lp);
    });

    RegisterMessageHandler(WM_CLOSE, [this](WPARAM wp, LPARAM lp) {
        return OnClose(wp, lp);
    });

    RegisterMessageHandler(WM_TRAYICON, [this](WPARAM wp, LPARAM lp) {
        return OnTrayIconMessage(wp, lp);
    });

    RegisterMessageHandler(WM_COMMAND, [this](WPARAM wp, LPARAM lp) {
        _onTrayMenu(wp);
        return 0;
    });

    RegisterMessageHandler(WM_EXITSIZEMOVE, [this](WPARAM wp, LPARAM lp) {
        return OnExitSizeMove(wp, lp);
    });

    // Restoring tray icon on Explorer restart
    const UINT taskbarCreatedMsg = RegisterWindowMessageA("TaskbarCreated");
    RegisterMessageHandler(taskbarCreatedMsg, [this](WPARAM wp, LPARAM lp) {
        if (currentIcon_) {
            CreateTrayIcon(currentIcon_, _currentTooltip);
        }
        return 0;
    });
}

LRESULT MainWindow::OnCreate(WPARAM wParam, LPARAM lParam) {
    return 0;
}

LRESULT MainWindow::OnDestroy(WPARAM wParam, LPARAM lParam) {
    trayIcon_->Remove();
    if (IsOvershadowed()) {
        // Dont destroy shadow window, we can reuse it later
        Hide();
    }
    PostQuitMessage(0);
    return 0;
}

LRESULT MainWindow::OnPaint(WPARAM wParam, LPARAM lParam) {
    if (!_onRender) {
        return 0;
    }

    auto hwnd = GetEffectiveHandle();
    GDIRenderer::RenderLayeredWindow(hwnd, _size.x, _size.y, _onRender);

    ValidateRect(hwnd, nullptr);
    return 0;
}

LRESULT MainWindow::OnClose(WPARAM wParam, LPARAM lParam) {
    if (_onClose) {
        _onClose();
    }
    return 0;
}

LRESULT MainWindow::OnTrayIconMessage(WPARAM wParam, LPARAM lParam) {
    const UINT message = LOWORD(lParam);

    if (message == WM_LBUTTONDBLCLK || message == WM_RBUTTONUP) {
        ShowTrayContextMenu();
    }

    return 0;
}

LRESULT MainWindow::OnExitSizeMove(WPARAM wParam, LPARAM lParam) {
    // Can add callback for position saving
    return 0;
}

bool MainWindow::CreateTrayIcon(HICON icon, const std::wstring& tooltip) {
    if (!hwnd_) {
        return false;
    }

    currentIcon_ = icon;
    return trayIcon_->Create(hwnd_, 1, icon, tooltip, WM_TRAYICON);
}

void MainWindow::UpdateTrayIcon(HICON icon) {
    currentIcon_ = icon;
    trayIcon_->UpdateIcon(icon);
}

void MainWindow::UpdateTrayTooltip(const std::wstring &tooltip) {
    _currentTooltip = tooltip;
    trayIcon_->UpdateTooltip(tooltip);
}


void MainWindow::ShowTrayContextMenu() {
    HMENU menu = LoadMenu(hInstance_, MAKEINTRESOURCE(IDR_TRAY_MENU));
    if (!menu) {
        return;
    }

    HMENU subMenu = GetSubMenu(menu, 0);
    if (!subMenu) {
        DestroyMenu(menu);
        return;
    }

    POINT cursor;
    GetCursorPos(&cursor);

    SetForegroundWindow(hwnd_);

    TrackPopupMenu(
        subMenu,
        TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_BOTTOMALIGN,
        cursor.x,
        cursor.y,
        0,
        hwnd_,
        nullptr
    );

    DestroyMenu(menu);
}

