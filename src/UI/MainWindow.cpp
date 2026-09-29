#include "MainWindow.hpp"

#include "Core/Dispatcher.hpp"
#include "Core/Hotkeys/Bindings.hpp"
#include "Core/Hotkeys/HotkeyService.hpp"
#include "Core/Lifecycle.hpp"
#include "Core/Tray.hpp"
#include "Foreground.hpp"
#include "Logger.hpp"
#include "TrayIconTheme.hpp"

#include "Resources/Resource.h"

MainWindow::MainWindow(HINSTANCE hInstance, AppConfig& config, Feedback& feedback)
    : BaseWindow(hInstance), _cfg(config), _feedback(feedback), _overlay(this, config.Overlay)
{
    // Last, so it closes the radio and is what a choice nobody registered falls back to
    Tray::Add({.Id = "tray.app",
               .Title = L"App icon",
               .Order = Tray::Last,
               .Icon = [] { return LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP)); }});
}

bool MainWindow::Initialize(WindowConfig config) {
    if (!RegisterWindowClass(config)) {
        return false;
    }

    // Restoring the tray icon on an Explorer restart
    _taskbarCreatedMessage = RegisterWindowMessageA("TaskbarCreated");

    // Placed and sized before this, by the surface that owns the overlay's settings
    _hwnd = CreateWindowExW(
        OverlaySurface::StyleEx,
        config.className,
        config.windowTitle,
        OverlaySurface::Style,
        GetPositionX(),
        GetPositionY(),
        GetWidth(),
        GetHeight(),
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
    // through. Bound first: everything below may post.
    Dispatcher::BindUi(_hwnd);
    _postTarget = _hwnd;

    if (!Foreground::Start(&Dispatcher::ToUi)) {
        LOG_ERROR("Foreground tracking unavailable; app-specific hotkeys will use global bindings");
    }
    Overlay::Invalidate = &PostRelayout;
    Tray::Refresh = &PostTrayRefresh;
    _feedback.Bind(_hInstance, &PostNotification);
    CreateTrayIcon(nullptr, L"");

    RestoreConfig();
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

void MainWindow::PostNotification(const std::wstring& text) {
    if (text.empty()) {
        return;
    }

    auto* payload = new std::wstring(text);
    if (!PostMessageW(_postTarget, WM_SHOW_NOTIFICATION, 0, reinterpret_cast<LPARAM>(payload))) {
        delete payload;
    }
}

void MainWindow::PostRelayout() {
    PostMessageW(_postTarget, WM_OVERLAY_RELAYOUT, 0, 0);
}

void MainWindow::PostTrayRefresh() {
    PostMessageW(_postTarget, WM_TRAY_REFRESH, 0, 0);
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

        // The app quits from the tray menu, never by closing its window
        case WM_CLOSE:
            return 0;

        case WM_TRAYICON:
            return OnTrayIconMessage(lParam);

        case WM_TIMER:
            _overlay.OnTimer(wParam);
            return 0;

        case WM_COMMAND: {
            const UINT command = LOWORD(wParam);
            if (command >= TrayMenuFirst && command - TrayMenuFirst < Tray::Providers.size()
                && Tray::Providers[command - TrayMenuFirst].MenuInvoke) {
                Tray::Providers[command - TrayMenuFirst].MenuInvoke();
            } else if (command == ID_APP_EXIT) {
                PostQuitMessage(0);
            } else if (command == ID_APP_SETTINGS) {
                OpenSettings();
            } else if (command == ID_APP_SETTINGS_CLOSED && _settings && !_settings->GetHandle()) {
                // Checked: a window opened before this arrived is not the one that closed
                _settings.reset();
            }
            return 0;
        }

        // Broadcast to every top-level window when the light/dark theme is switched
        case WM_SETTINGCHANGE:
            if (TrayIconTheme::IsColorSetChange(lParam)) {
                for (const TrayProvider& provider : Tray::Providers) {
                    if (provider.ThemeChanged) {
                        provider.ThemeChanged();
                    }
                }
                RefreshTray();
            }
            return 0;

        case WM_OVERLAY_RELAYOUT:
            _overlay.Relayout();
            return 0;

        case WM_TRAY_REFRESH:
            RefreshTray();
            return 0;

        case WM_SHOW_NOTIFICATION: {
            const std::unique_ptr<std::wstring> payload(reinterpret_cast<std::wstring*>(lParam));
            _overlay.ShowText(std::move(*payload));
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

    const POINT windowPos{GetPositionX(), GetPositionY()};
    LayeredWindow::Render(hwnd, _surface, _size.x, _size.y, windowPos,
                          [this](RenderContext& context) { _overlay.Render(context); });

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

void MainWindow::RefreshTray() {
    const TrayProvider* owner = Tray::Owner(_cfg.Tray.Provider);
    if (!owner) {
        return;
    }

    if (const HICON icon = owner->Icon()) {
        _currentIcon = icon;
        _trayIcon.UpdateIcon(icon);
    }
    _currentTooltip = owner->Tooltip ? owner->Tooltip() : std::wstring{APP_NAME};
    _trayIcon.UpdateTooltip(_currentTooltip);
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

    // Between Settings and the separator, so the two items that were always there never move
    UINT position = 1;
    for (size_t i = 0; i < Tray::Providers.size(); i++) {
        const TrayProvider& provider = Tray::Providers[i];
        if (provider.MenuLabel && provider.MenuInvoke) {
            InsertMenuW(subMenu, position++, MF_BYPOSITION | MF_STRING, TrayMenuFirst + i, provider.MenuLabel());
        }
    }

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

void MainWindow::SuspendActivity() {
    _overlay.Suspend();
    Lifecycle::Suspend();
    HotkeyService::ClearHotkeys();
    // Actions read and save the config the settings window is about to edit
    Dispatcher::Stop();
}

void MainWindow::RestoreConfig() {
    Dispatcher::Start();
    HotkeyService::SetMultiPressWindow(_cfg.Core.MultiPressWindowMs);

#ifndef APP_NO_GLOBAL_HOOKS
    Bindings::Apply(_cfg.Bindings, _feedback);
#endif // APP_NO_GLOBAL_HOOKS - Debug builds skip the desktop-wide hooks for bindings

    // Ahead of the overlay: a layer may measure what a module only knows again once it resumes
    Lifecycle::Restore();
    _overlay.Restore();
    RefreshTray();
}

void MainWindow::OpenSettings() {
    if (_settings && _settings->GetHandle()) {
        SetForegroundWindow(_settings->GetHandle());
        return;
    }

    SuspendActivity();
    // Not shown from here: SuspendActivity already put the overlay into preview, and whether it
    // is on screen is the surface's decision alone
    ToggleInteractivity(true);

    _settings = std::make_unique<SettingsWindow>(_hInstance, _hwnd, _cfg);
    _settings->OnApply += [this] { _overlay.CommitPosition(); };
    _settings->OnExit += [this] {
        ToggleInteractivity(false);
        RestoreConfig();
        // Never from here: this fires inside the window's own WM_DESTROY, so dropping the
        // last reference would unwind the object still running the callback
        PostMessageW(_hwnd, WM_COMMAND, ID_APP_SETTINGS_CLOSED, 0);
    };
    _settings->Show();
}

void MainWindow::ToggleInteractivity(const bool interactive) const {
    const HWND hwnd = GetEffectiveHandle();

    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    if (interactive) {
        exStyle &= ~WS_EX_TRANSPARENT;
        style &= ~WS_DISABLED;
    } else {
        exStyle |= WS_EX_TRANSPARENT;
        style |= WS_DISABLED;
    }

    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);
    SetWindowLongPtrW(hwnd, GWL_STYLE, style);
}
