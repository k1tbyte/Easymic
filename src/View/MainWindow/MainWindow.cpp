#include "MainWindow.hpp"

#include "Core/Dispatcher.hpp"

#include "../../Resources/Resource.h"
#include "../Components/IndicatorLayout.hpp"

MainWindow::MainWindow(HINSTANCE hInstance, AppConfig& appConfig)
    : BaseWindow(hInstance), _config(appConfig)
{
}

bool MainWindow::Initialize(WindowConfig config) {
    if (!RegisterWindowClass(config)) {
        return false;
    }

    // Restoring the tray icon on an Explorer restart
    _taskbarCreatedMessage = RegisterWindowMessageA("TaskbarCreated");

    const int windowSize = IndicatorLayout::PillSize(_config.Indicator.Size);
    SetWidth(windowSize);
    SetHeight(windowSize);
    SetPositionX(_config.Indicator.PosX);
    SetPositionY(_config.Indicator.PosY);

    _hwnd = CreateWindowExW(
        StyleEx,
        config.className,
        config.windowTitle,
        Style,
        _config.Indicator.PosX,
        _config.Indicator.PosY,
        windowSize,
        windowSize,
        nullptr,
        nullptr,
        _hInstance,
        this
    );

    if (!_hwnd) {
        return false;
    }

    RegisterWindow(_hwnd);

    // This window owns the message loop, so it is the one the worker reaches the UI thread
    // through. Bound before the view model, which may post from its own Init.
    Dispatcher::BindUi(_hwnd);

    _viewModel->Init();

    return true;
}

bool MainWindow::RegisterWindowClass(const WindowConfig& config) const {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = StaticWindowProc;
    wc.hInstance = _hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = config.className;

    return RegisterClassExW(&wc) != 0;
}

void MainWindow::PostNotification(HWND target, const std::wstring& text) {
    if (text.empty()) {
        return;
    }

    auto* payload = new std::wstring(text);
    if (!PostMessageW(target, WM_SHOW_NOTIFICATION, 0, reinterpret_cast<LPARAM>(payload))) {
        delete payload;
    }
}

LRESULT MainWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == _taskbarCreatedMessage) {
        _trayIcon.Remove(); // clears isCreated_ even if NIM_DELETE fails (shell already lost it)
        if (_currentIcon) {
            CreateTrayIcon(_currentIcon, _currentTooltip);
        }
        return 0;
    }

    switch (message) {
        case WM_CREATE:
            return 0;

        case WM_DESTROY:
            return OnDestroy();

        case WM_PAINT:
            return OnPaint();

        case WM_MOVE:
        case WM_EXITSIZEMOVE:
            UpdateRect();
            return 0;

        case WM_CLOSE:
            if (OnClose) {
                OnClose();
            }
            return 0;

        case WM_TRAYICON:
            return OnTrayIconMessage(lParam);

        case WM_TIMER:
            if (OnTimer) {
                OnTimer(wParam);
            }
            return 0;

        case WM_COMMAND:
            if (OnTrayMenu) {
                OnTrayMenu(wParam);
            }
            return 0;

        // Broadcast to every top-level window when the light/dark theme is switched
        case WM_SETTINGCHANGE:
            if (OnThemeChanged && TrayIconTheme::IsColorSetChange(lParam)) {
                OnThemeChanged();
            }
            return 0;

        case WM_SHOW_NOTIFICATION: {
            const std::unique_ptr<std::wstring> payload(reinterpret_cast<std::wstring*>(lParam));
            if (OnNotification) {
                OnNotification(std::move(*payload));
            }
            return 0;
        }

        // This window owns the message loop, so it is the one the worker gets back to the UI
        // thread through - see Dispatcher::ToUi
        case Dispatcher::WM_DISPATCH_RUN:
            Dispatcher::RunPosted(lParam);
            return 0;

        default:
            return BaseWindow::HandleMessage(message, wParam, lParam);
    }
}

LRESULT MainWindow::OnDestroy() {
    _trayIcon.Remove();
    if (IsOvershadowed()) {
        // Dont destroy shadow window, we can reuse it later
        Hide();
    }
    PostQuitMessage(0);
    return 0;
}

LRESULT MainWindow::OnPaint() {
    HWND hwnd = GetEffectiveHandle();
    PAINTSTRUCT paintStruct{};
    const bool paintsOwnedWindow = hwnd == GetHandle();

    if (paintsOwnedWindow) {
        // ALWAYS use BeginPaint / EndPaint in WM_PAINT handler for windows we own.
        // The shadow window lives in a foreign UIAccess process and is only used as a render target.
        BeginPaint(hwnd, &paintStruct);
    }

    if (OnRender) {
        const POINT windowPos{GetPositionX(), GetPositionY()};
        GDIRenderer::RenderLayeredWindow(hwnd, _size.x, _size.y, windowPos, OnRender);
    }

    if (paintsOwnedWindow) {
        EndPaint(hwnd, &paintStruct);
        ValidateRect(hwnd, nullptr);
    }

    return 0;
}

LRESULT MainWindow::OnTrayIconMessage(LPARAM lParam) {
    const UINT message = LOWORD(lParam);

    if (message == WM_LBUTTONDBLCLK || message == WM_RBUTTONUP) {
        ShowTrayContextMenu();
    }

    return 0;
}

bool MainWindow::CreateTrayIcon(HICON icon, const std::wstring& tooltip) {
    if (!_hwnd) {
        return false;
    }

    _currentIcon = icon;
    return _trayIcon.Create(_hwnd, 1, icon, tooltip, WM_TRAYICON);
}

void MainWindow::UpdateTrayIcon(HICON icon) {
    _currentIcon = icon;
    _trayIcon.UpdateIcon(icon);
}

void MainWindow::UpdateTrayTooltip(const std::wstring &tooltip) {
    _currentTooltip = tooltip;
    _trayIcon.UpdateTooltip(tooltip);
}

void MainWindow::ShowTrayContextMenu() {
    HMENU menu = LoadMenu(_hInstance, MAKEINTRESOURCE(IDR_TRAY_MENU));
    if (!menu) {
        return;
    }

    HMENU subMenu = GetSubMenu(menu, 0);
    if (!subMenu) {
        DestroyMenu(menu);
        return;
    }

    const bool bellEnabled = _config.Mic.BellVolume > 0;
    InsertMenuW(subMenu, ID_APP_SETTINGS, MF_BYCOMMAND | MF_STRING,
               ID_APP_TOGGLE_BELL,
               bellEnabled ? L"Disable bell sound" : L"Enable bell sound");

    POINT cursor;
    GetCursorPos(&cursor);

    SetForegroundWindow(_hwnd);

    TrackPopupMenu(
        subMenu,
        TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_BOTTOMALIGN,
        cursor.x,
        cursor.y,
        0,
        _hwnd,
        nullptr
    );

    DestroyMenu(menu);
}
