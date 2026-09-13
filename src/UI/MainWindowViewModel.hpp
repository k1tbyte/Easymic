//
// Created by kitbyte on 31.10.2025.
//

#pragma once

#include <memory>

#include "Core/Feedback.hpp"
#include "Core/Hotkeys/Bindings.hpp"
#include "Core/Hotkeys/HotkeyService.hpp"
#include "Features/Microphone/Microphone.hpp"
#include "Features/Microphone/Wasapi/AudioManager.hpp"
#include "Overlay/OverlaySurface.hpp"
#include "Settings/SettingsWindowViewModel.hpp"
#include "ViewModel.hpp"
#include "MainWindow.hpp"
#include "BaseWindow.hpp"

/// The frame: the tray icon, the settings window, and putting the config back into effect when
/// that window closes. What the overlay shows is the overlay's business and what the microphone
/// is doing is the module's - this only asks them.
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
    }

private:
    void SuspendActivity() {
        _overlay.Suspend();
        Mic::Suspend();
        HotkeyService::Dispose(); // also drops every registered hotkey
    }

    void RefreshTrayIcon() const {
        _view->UpdateTrayIcon(Mic::TrayIcon());
    }

    void RestoreConfig() {
        Mic::Resume();
        HotkeyService::ClearHotkeys();
        HotkeyService::SetMultiPressWindow(_cfg.Core.MultiPressWindowMs);

#ifndef APP_NO_GLOBAL_HOOKS
        if (Bindings::Apply(_cfg.Bindings, _feedback)) {
            HotkeyService::Initialize();
        }
#endif // APP_NO_GLOBAL_HOOKS - Debug builds skip the desktop-wide hooks

        _overlay.Restore();
        Mic::Refresh();
    }

    void OpenSettings() {
        if (_settingsWindow && _settingsWindow->GetHandle()) {
            SetForegroundWindow(_settingsWindow->GetHandle());
            return;
        }

        SuspendActivity();

        auto settings = std::make_unique<SettingsWindow>(_view->GetHInstance());
        settings->AttachViewModel<SettingsWindowViewModel>(_cfg);

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
            case ID_APP_TOGGLE_BELL:
                Mic::ToggleBell();
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

    /// What the tray says about the microphone, redrawn from what the module has just settled
    /// on. The pill is not here any more - the module's own overlay layer draws that.
    void OnMicStateChanged() const {
        RefreshTrayIcon();

        if (!Mic::HasDevice()) {
            _view->UpdateTrayTooltip(APP_NAME L" - No device");
            return;
        }

        constexpr auto bufferSize = 255;
        wchar_t buffer[bufferSize];
        swprintf(buffer, bufferSize, APP_NAME L" - %ls [%d%%]",
                 Mic::Audio().CaptureDevice()->GetDeviceName(), _feedback.VolumePercent());
        _view->UpdateTrayTooltip(std::wstring(buffer));
    }

public:
    void Init() override {
        _overlay.Bind();
        _feedback.Bind(_view->GetHInstance(), &MainWindow::PostNotification);
        Mic::OnStateChanged += [this] { OnMicStateChanged(); };
        _view->CreateTrayIcon(nullptr, L"");

        _view->OnTrayMenu = [this](UINT_PTR commandId) { OnTrayMenuCommand(commandId); };
        _view->OnRender = [this](const RenderContext& context) {
            _overlay.Render(*context.graphics);
        };
        _view->OnRelayout = [this] { _overlay.Relayout(); };
        _view->OnTimer = [this](UINT_PTR timerId) { _overlay.OnTimer(timerId); };

        _view->OnThemeChanged = [this] {
            Mic::RefreshTheme();
            RefreshTrayIcon();
        };

        _view->OnNotification = [this](std::wstring text) { _overlay.ShowText(std::move(text)); };

        RestoreConfig();
    }
};
