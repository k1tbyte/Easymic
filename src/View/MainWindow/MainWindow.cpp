#include "MainWindow.hpp"
#include "../../Resources/Resource.h"

MainWindow::MainWindow(HINSTANCE hInstance)
    : BaseWindow(hInstance)
    , trayIcon_(std::make_unique<TrayIcon>())
{
}

MainWindow::~MainWindow() {
    if (currentIcon_) {
        DestroyIcon(currentIcon_);
    }
}

bool MainWindow::Initialize(const Config& config) {
    config_ = config;

    if (!RegisterWindowClass(config)) {
        return false;
    }

    const int windowSize = config.size * 2;
    currentSize_ = windowSize;

    hwnd_ = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
        config.className,
        config.windowTitle,
        WS_POPUP,
        config.posX,
        config.posY,
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

    return true;
}

bool MainWindow::RegisterWindowClass(const Config& config) const {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = BaseWindow::StaticWindowProc;
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

    RegisterMessageHandler(WM_EXITSIZEMOVE, [this](WPARAM wp, LPARAM lp) {
        return OnExitSizeMove(wp, lp);
    });

    // Восстановление tray icon при перезапуске Explorer
    const UINT taskbarCreatedMsg = RegisterWindowMessageA("TaskbarCreated");
    RegisterMessageHandler(taskbarCreatedMsg, [this](WPARAM wp, LPARAM lp) {
        if (currentIcon_) {
            CreateTrayIcon(currentIcon_, L"");
        }
        return 0;
    });
}

LRESULT MainWindow::OnCreate(WPARAM wParam, LPARAM lParam) {
    gdiContext_ = std::make_unique<GdiPlusContext>();
    return 0;
}

LRESULT MainWindow::OnDestroy(WPARAM wParam, LPARAM lParam) {
    trayIcon_->Remove();
    gdiContext_.reset();
    PostQuitMessage(0);
    return 0;
}

LRESULT MainWindow::OnPaint(WPARAM wParam, LPARAM lParam) {
    if (!renderCallback_) {
        return 0;
    }

    LayeredWindowRenderer::Render(hwnd_, currentSize_, currentSize_, renderCallback_);
    ValidateRect(hwnd_, nullptr);
    return 0;
}

LRESULT MainWindow::OnClose(WPARAM wParam, LPARAM lParam) {
    if (onClose_) {
        onClose_();
    }
    return 0;
}

LRESULT MainWindow::OnTrayIconMessage(WPARAM wParam, LPARAM lParam) {
    const UINT message = LOWORD(lParam);

    if (message == WM_LBUTTONDBLCLK && onTrayClick_) {
        onTrayClick_();
        return 0;
    }

    if (message == WM_RBUTTONUP) {
        ShowTrayContextMenu();
        return 0;
    }

    return 0;
}

LRESULT MainWindow::OnExitSizeMove(WPARAM wParam, LPARAM lParam) {
    // Можно добавить callback для сохранения позиции
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

void MainWindow::UpdateSize(int newSize) {
    if (!hwnd_) {
        return;
    }

    currentSize_ = newSize;

    RECT rect;
    GetWindowRect(hwnd_, &rect);

    SetWindowPos(
        hwnd_,
        nullptr,
        rect.left,
        rect.top,
        newSize,
        newSize,
        SWP_NOZORDER | SWP_NOACTIVATE
    );

    Invalidate();
}

void MainWindow::UpdatePosition(int x, int y) {
    if (!hwnd_) {
        return;
    }

    SetWindowPos(
        hwnd_,
        nullptr,
        x,
        y,
        0,
        0,
        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
    );
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

    const UINT command = TrackPopupMenu(
        subMenu,
        TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_BOTTOMALIGN | TPM_RETURNCMD,
        cursor.x,
        cursor.y,
        0,
        hwnd_,
        nullptr
    );

    if (command != 0 && onTrayMenu_) {
        onTrayMenu_(command);
    }

    DestroyMenu(menu);
}

