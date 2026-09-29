#include "MainWindow.hpp"

#include "Core/Dispatcher.hpp"
#include "Core/Hotkeys/Bindings.hpp"
#include "Core/Hotkeys/HotkeyService.hpp"
#include "Core/Lifecycle.hpp"
#include "Core/Tray.hpp"
#include "Windowing/Foreground.hpp"
#include "Gfx/TrayIconTheme.hpp"
#include "definitions.h"

#include "Resources/Resource.h"

namespace {
    constexpr auto ClassName = L"MainWindowClass";
}

MainWindow::MainWindow(HINSTANCE hInstance, AppConfig& config, Feedback& feedback)
    : BaseWindow(hInstance), _cfg(config), _feedback(feedback), _window(*this), _overlay(_window, config.Overlay)
{
    Tray::Add({.Id = "tray.app",
               .Title = L"App icon",
               .Order = Tray::Last,
               .Icon = [] { return LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP)); }});
}

bool MainWindow::Initialize() {
    if (!RegisterWindowClass()) {
        return false;
    }

    _taskbarCreatedMessage = RegisterWindowMessageA("TaskbarCreated");

    _hwnd = CreateWindowExW(OverlayWindow::StyleEx, ClassName, L"MainWindow", OverlayWindow::Style,
                            _window.Pos.x, _window.Pos.y, _window.Size.x, _window.Size.y,
                            nullptr, nullptr, _hInstance, this);
    if (!_hwnd) {
        return false;
    }

    // Bound first: everything below may post
    Dispatcher::BindUi(_hwnd);
    _postTarget = _hwnd;

    if (!Foreground::Start(&Dispatcher::ToUi)) {
        LOG_ERROR("Foreground tracking unavailable; app-specific hotkeys will use global bindings");
    }
    Overlay::Invalidate = &PostRelayout;
    Tray::Refresh = &PostTrayRefresh;
    _feedback.Bind(_hInstance, &PostNotification);
    _trayIcon.Create(_hwnd, 1, WM_TRAYICON);

    RestoreConfig();
    return true;
}

bool MainWindow::PreTranslate(MSG& message) const {
    const HWND settings = _settings ? _settings->GetHandle() : nullptr;
    return settings && IsDialogMessage(settings, &message);
}

bool MainWindow::RegisterWindowClass() const {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = StaticWindowProc;
    wc.hInstance = _hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = ClassName;

    return RegisterClassExW(&wc) != 0;
}

void MainWindow::PostNotification(std::wstring text) {
    if (text.empty()) {
        return;
    }

    auto* payload = new std::wstring(std::move(text));
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
        _trayIcon.Remove();
        _trayIcon.Add();
        return 0;
    }

    switch (message) {
        case WM_DESTROY:
            return OnDestroy();

        case WM_PAINT:
            return OnPaint();

        case WM_MOVE:
        case WM_EXITSIZEMOVE:
            _window.SyncBounds();
            return 0;

        // Settings lifts click-through: the whole pill is what the user drags
        case WM_NCHITTEST: {
            const LRESULT hit = DefWindowProcW(_hwnd, message, wParam, lParam);
            return hit == HTCLIENT ? HTCAPTION : hit;
        }

        // The app quits from the tray menu, never by closing its window
        case WM_CLOSE:
            return 0;

        case WM_TRAYICON:
            return OnTrayIconMessage(lParam);

        case WM_TIMER:
            _overlay.OnTimer(wParam);
            return 0;

        case WM_COMMAND:
            return OnCommand(LOWORD(wParam));

        // Broadcast when the light/dark theme is switched
        case WM_SETTINGCHANGE:
            return OnThemeChange(lParam);

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

        case Dispatcher::WM_DISPATCH_RUN:
            Dispatcher::RunPosted(lParam);
            return 0;

        default:
            return BaseWindow::HandleMessage(message, wParam, lParam);
    }
}

LRESULT MainWindow::OnCommand(const UINT command) {
    const size_t provider = command - TrayMenuFirst;
    if (command >= TrayMenuFirst && provider < Tray::Providers.size() && Tray::Providers[provider].MenuInvoke) {
        Tray::Providers[provider].MenuInvoke();
    } else if (command == ID_APP_EXIT) {
        PostQuitMessage(0);
    } else if (command == ID_APP_SETTINGS) {
        OpenSettings();
    } else if (command == ID_APP_SETTINGS_CLOSED && _settings && !_settings->GetHandle()) {
        // A window opened before this arrived is not the one that closed
        _settings.reset();
    }
    return 0;
}

LRESULT MainWindow::OnThemeChange(const LPARAM lParam) {
    if (!TrayIconTheme::IsColorSetChange(lParam)) {
        return 0;
    }

    for (const TrayProvider& provider : Tray::Providers) {
        if (provider.ThemeChanged) {
            provider.ThemeChanged();
        }
    }
    RefreshTray();
    return 0;
}

LRESULT MainWindow::OnDestroy() {
    _trayIcon.Remove();
    // A UIAccess overlay window belongs to another process: hidden, never destroyed
    _window.Hide();
    PostQuitMessage(0);
    return 0;
}

LRESULT MainWindow::OnPaint() {
    PAINTSTRUCT paint{};
    const bool paintsOwnWindow = _window.Target() == _hwnd;

    if (paintsOwnWindow) {
        BeginPaint(_hwnd, &paint);
    }
    _window.Paint([this](RenderContext& context) { _overlay.Render(context); });
    if (paintsOwnWindow) {
        EndPaint(_hwnd, &paint);
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

void MainWindow::RefreshTray() {
    const TrayProvider* owner = Tray::Owner(_cfg.Tray.Provider);
    if (!owner) {
        return;
    }

    _trayIcon.Update(owner->Icon(), owner->Tooltip ? owner->Tooltip() : std::wstring{APP_NAME});
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

    // After Settings and before the separator: the two fixed items never move
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
    TrackPopupMenu(subMenu, TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_BOTTOMALIGN, cursor.x, cursor.y, 0, _hwnd, nullptr);

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
#endif

    // Before the overlay: a layer may measure what a module knows again only once it resumes
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
    _window.SetInteractive(true);

    _settings = std::make_unique<SettingsWindow>(_hInstance, _hwnd, _cfg);
    _settings->OnApply += [this] { _overlay.CommitPosition(); };
    _settings->OnExit += [this] {
        _window.SetInteractive(false);
        RestoreConfig();
        // Posted: this fires inside the window's own WM_DESTROY, so dropping the last reference
        // here would unwind the object still running the callback
        PostMessageW(_hwnd, WM_COMMAND, ID_APP_SETTINGS_CLOSED, 0);
    };
    _settings->Show();
}
