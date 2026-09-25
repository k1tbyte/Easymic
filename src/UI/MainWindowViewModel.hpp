//
// Created by kitbyte on 31.10.2025.
//

#pragma once

#include <memory>

#include "Core/Dispatcher.hpp"
#include "Core/Feedback.hpp"
#include "Core/Hotkeys/Bindings.hpp"
#include "Core/Hotkeys/HotkeyService.hpp"
#include "Core/Lifecycle.hpp"
#include "Core/Tray.hpp"
#include "Foreground.hpp"
#include "Overlay/OverlaySurface.hpp"
#include "Settings/SettingsWindowViewModel.hpp"
#include "ViewModel.hpp"
#include "MainWindow.hpp"
#include "BaseWindow.hpp"

/// The frame: the tray icon, the settings window, and putting the config back into effect when
/// that window closes. What the overlay and the tray show belongs to whoever registered into
/// them - this only asks.
class MainWindowViewModel final : public BaseViewModel<MainWindow> {
private:
    std::unique_ptr<SettingsWindow> _settingsWindow;

    AppConfig &_cfg;
    Feedback &_feedback;
    OverlaySurface _overlay;

public:
    MainWindowViewModel(BaseWindow* baseView, AppConfig& config, Feedback& feedback)
        : BaseViewModel(baseView), _cfg(config), _feedback(feedback),
          _overlay(_view, config.Overlay) {
        // Last, so it closes the radio and is what a choice nobody registered falls back to
        Tray::Add({.Id = "tray.app",
                   .Title = L"App icon",
                   .Order = Tray::Last,
                   .Icon = [] { return LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP)); }});
    }

private:
    void SuspendActivity() {
        _overlay.Suspend();
        Lifecycle::Suspend();
        HotkeyService::ClearHotkeys();
        // Actions read and save the config the settings window is about to edit
        Dispatcher::Stop();
    }

    /// The provider the config names paints the icon, and says the tooltip.
    void RefreshTray() const {
        const TrayProvider* owner = Tray::Owner(_cfg.Tray.Provider);
        if (!owner) {
            return;
        }

        if (const HICON icon = owner->Icon()) {
            _view->UpdateTrayIcon(icon);
        }
        _view->UpdateTrayTooltip(owner->Tooltip ? owner->Tooltip() : std::wstring{APP_NAME});
    }

    void RestoreConfig() {
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

    void OpenSettings() {
        if (_settingsWindow && _settingsWindow->GetHandle()) {
            SetForegroundWindow(_settingsWindow->GetHandle());
            return;
        }

        SuspendActivity();

        auto settings = std::make_unique<SettingsWindow>(_view->GetHInstance());
        settings->AttachViewModel<SettingsWindowViewModel>(_cfg, [this] { _overlay.CommitPosition(); },
                                                           [this] { _overlay.PreviewFont(); });

        if (!settings->Initialize({.parentHwnd = _view->GetHandle()})) {
            // A tray app that cannot open its settings has no business taking the desktop with it
            LOG_ERROR("Failed to initialize settings window");
            RestoreConfig();
            return;
        }

        settings->OnExit += [this] {
            RestoreConfig();
            // Never from here: this fires inside the window's own WM_DESTROY, so dropping the
            // last reference would unwind the object still running the callback
            PostMessageW(_view->GetHandle(), WM_COMMAND, ID_APP_SETTINGS_CLOSED, 0);
        };

        _settingsWindow = std::move(settings);
        _settingsWindow->Show();
    }

    void OnTrayMenuCommand(const UINT_PTR commandId) {
        switch (commandId) {
            case ID_APP_EXIT:
                PostQuitMessage(0);
                break;
            case ID_APP_SETTINGS:
                OpenSettings();
                break;
            case ID_APP_SETTINGS_CLOSED:
                _settingsWindow.reset();
                break;
            default:
                break;
        }
    }

public:
    void Init() override {
        if (!Foreground::Start()) {
            LOG_ERROR("Foreground tracking unavailable; app-specific hotkeys will use global bindings");
        }
        _overlay.Bind();
        Tray::Refresh = &MainWindow::PostTrayRefresh;
        _feedback.Bind(_view->GetHInstance(), &MainWindow::PostNotification);
        _view->CreateTrayIcon(nullptr, L"");

        _view->OnTrayMenu = [this](UINT_PTR commandId) { OnTrayMenuCommand(commandId); };
        _view->OnTrayRefresh = [this] { RefreshTray(); };
        _view->OnRender = [this](RenderContext& context) { _overlay.Render(context); };
        _view->OnRelayout = [this] { _overlay.Relayout(); };
        _view->OnTimer = [this](UINT_PTR timerId) { _overlay.OnTimer(timerId); };

        _view->OnThemeChanged = [this] {
            for (const TrayProvider& provider : Tray::Providers) {
                if (provider.ThemeChanged) {
                    provider.ThemeChanged();
                }
            }
            RefreshTray();
        };

        _view->OnNotification = [this](std::wstring text) { _overlay.ShowText(std::move(text)); };

        RestoreConfig();
    }
};
