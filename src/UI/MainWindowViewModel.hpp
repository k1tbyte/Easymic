//
// Created by kitbyte on 31.10.2025.
//

#pragma once

#include <memory>

#include "Features/Microphone/Wasapi/AudioManager.hpp"
#include "Core/Bindings.hpp"
#include "Core/Dispatcher.hpp"
#include "Core/Feedback.hpp"
#include "Core/HotkeyService.hpp"
#include "Features/Microphone/Microphone.hpp"
#include "Settings/SettingsWindowViewModel.hpp"
#include "UACService.hpp"
#include "UIAccess/UIAccessManager.hpp"
#include "ViewModel.hpp"
#include "MainWindow.hpp"
#include "IndicatorIcons.hpp"
#include "IndicatorLayout.hpp"
#include "BaseWindow.hpp"

using namespace Gdiplus;

/// The indicator, the tray icon and the settings window. Microphone state belongs to the module
/// that owns the device - this only draws what it reports.
class MainWindowViewModel final : public BaseViewModel<MainWindow> {
private:
    static constexpr UINT ID_PEAK_TIMER = WM_USER + 100;
    static constexpr UINT ID_NOTIFICATION_TIMER = WM_USER + 101;
    static constexpr int PEAK_TIMER_INTERVAL_MS = 150;
    static constexpr int NOTIFICATION_DURATION_MS = 2000;
    static constexpr int PEAK_METER_DEBOUNCE_PHASES = 2;
    static constexpr const char *SHADOW_WINDOW_KEY = "EasyLauncherIndicator";

    std::unique_ptr<SettingsWindow> _settingsWindow;

    AppConfig &_cfg;
    Feedback &_feedback;

    std::unique_ptr<IndicatorIcons> _icons;
    Bitmap* _bitmapToDisplay = nullptr;

    bool _isPeakMeterActive = false;
    int _peakMeterPhase = 0;

    IndicatorLayout _layout;
    /// UI thread only - the worker hands its text over through the message queue. Empty is "none".
    std::wstring _notificationText;
    /// Where the user left the indicator. Widening it for a notification must not move this.
    POINT _anchor{};

public:
    MainWindowViewModel(BaseWindow* baseView, AppConfig& config, Feedback& feedback)
        : BaseViewModel(baseView), _cfg(config), _feedback(feedback) {
    }

private:
    void KillNotificationTimer() {
        if (!_notificationText.empty()) {
            KillTimer(_view->GetHandle(), ID_NOTIFICATION_TIMER);
            _notificationText.clear();
        }
    }

    void SuspendActivity() {
        KillPeakMeter();
        KillNotificationTimer();
        if (_view->IsOvershadowed()) {
            _view->Hide();
            _view->SetShadowHwnd(nullptr);
        }
        Mic::Audio().StopWatchingForCaptureSessions();
        HotkeyService::Dispose(); // also drops every registered hotkey
    }

    void RefreshTrayIcon() const {
        _view->UpdateTrayIcon(_icons->Tray(!Mic::HasDevice() || Mic::Muted()));
    }

    void KillPeakMeter() {
        if (_isPeakMeterActive) {
            KillTimer(_view->GetHandle(), ID_PEAK_TIMER);
            _isPeakMeterActive = false;
            _peakMeterPhase = 0;
        }
    }

    void ApplyDisplayAffinity() const {
        const auto affinity = _cfg.Indicator.ExcludeFromCapture ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE;
        DWORD existingAffinity = 0;
        GetWindowDisplayAffinity(_view->GetEffectiveHandle(), &existingAffinity);

        if (existingAffinity == affinity) {
            return;
        }

        _view->IsOvershadowed()
            ? UIAccessManager::InjectDisplayAffinity(_view->GetEffectiveHandle(), affinity)
            : SetWindowDisplayAffinity(_view->GetHandle(), affinity);
    }

    void RestoreConfig() {
        Mic::Audio().WatchForCaptureSessions();
        HotkeyService::ClearHotkeys();
        HotkeyService::SetMultiPressWindow(_cfg.Core.MultiPressWindowMs);

#ifndef APP_NO_GLOBAL_HOOKS
        if (Bindings::Apply(_cfg.Bindings, _feedback)) {
            HotkeyService::Initialize();
        }
#endif // APP_NO_GLOBAL_HOOKS - Debug builds skip the desktop-wide hooks

        if (_cfg.Indicator.OnTopExclusive && UAC::IsElevated() && !_view->IsOvershadowed()) {
            _view->Hide();
            auto *shadowHwnd = UIAccessManager::GetOrCreateWindow(SHADOW_WINDOW_KEY, MainWindow::StyleEx,
                                                                  MainWindow::Style);
            _view->SetShadowHwnd(shadowHwnd);
            _view->RefreshPos(HWND_TOPMOST);
        }

        ApplyDisplayAffinity();
        Mic::Refresh();
    }

    /// Whether the mic pill may be on screen at all - config and live sessions, no mute state.
    bool MicAllowed() const {
        return Mic::HasDevice() && _cfg.Indicator.State != IndicatorState::Hidden
               && (!_cfg.Indicator.HideWhenInactive
                   || Mic::Audio().CaptureDevice()->GetActiveSessionsCount() > 0);
    }

    /// Muted is the only thing the pill shows by itself - the peak meter overrides it while talking.
    void RefreshMicBitmap() {
        _bitmapToDisplay = (MicAllowed() && Mic::Muted()) ? _icons->Muted() : nullptr;
    }

    /// The text pill must not shove the mic pill sideways, so it grows to the right - or to the
    /// left instead, when the right edge of the work area is in the way.
    LONG NotificationOriginX() const {
        const int overhang = _layout.totalWidth - _layout.height;
        if (overhang <= 0) {
            return _anchor.x;
        }

        MONITORINFO info{sizeof(MONITORINFO)};
        const HMONITOR monitor = MonitorFromPoint(_anchor, MONITOR_DEFAULTTONEAREST);
        const bool spillsOver = monitor && GetMonitorInfoW(monitor, &info)
                                && _anchor.x + _layout.totalWidth > info.rcWork.right;

        return spillsOver ? _anchor.x - overhang : _anchor.x;
    }

    /// The one place that decides whether the indicator is on screen and how wide it is.
    void UpdateIndicatorLayout() {
        if (!_layout.hasText) {
            _anchor = {_view->GetPositionX(), _view->GetPositionY()};
        }

        _layout = IndicatorLayout::Compute(_cfg.Indicator.Size, _bitmapToDisplay != nullptr,
                                           _notificationText);

        // "Muted or talking" keeps an empty window up on purpose: the peak meter only ticks while
        // the indicator is visible, and it is what discovers that the mic went live
        const bool waitsForPeak = _cfg.Indicator.State == IndicatorState::MutedOrTalk && MicAllowed();

        if (!_layout.hasText && !_layout.hasMic && !waitsForPeak) {
            KillPeakMeter();
            _view->Hide();
            return;
        }

        // Seeded from the anchor every time, so the work-area clamp inside RefreshPos stays
        // transient instead of walking the indicator across the screen notification by notification
        _view->SetPositionX(NotificationOriginX())
             ->SetPositionY(_anchor.y)
             ->SetWidth(_layout.totalWidth)
             ->SetHeight(_layout.height);

        _view->Show();
        _view->RefreshPos(HWND_TOPMOST);
        _view->Invalidate();
        SyncPeakMeter();
    }

    void OpenSettings() {
        if (_settingsWindow && _settingsWindow->GetHandle()) {
            SetForegroundWindow(_settingsWindow->GetHandle());
            return;
        }

        SuspendActivity();
        _bitmapToDisplay = _icons->Unmuted();
        UpdateIndicatorLayout();

        auto settings = std::make_unique<SettingsWindow>(_view->GetHInstance());
        settings->AttachViewModel<SettingsWindowViewModel>(_cfg, Mic::Audio());

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

    /// Everything the indicator and the tray show about the microphone, redrawn from what the
    /// module has just settled on.
    void OnMicStateChanged() {
        if (!Mic::HasDevice()) {
            _view->UpdateTrayTooltip(APP_NAME L" - No device");
            _bitmapToDisplay = nullptr;
            RefreshTrayIcon();
            UpdateIndicatorLayout();
            return;
        }

        RefreshMicBitmap();
        RefreshTrayIcon();
        UpdateIndicatorLayout();

        constexpr auto bufferSize = 255;
        wchar_t buffer[bufferSize];
        swprintf(buffer, bufferSize, APP_NAME L" - %ls [%d%%]",
                 Mic::Audio().CaptureDevice()->GetDeviceName(), _feedback.VolumePercent());
        _view->UpdateTrayTooltip(std::wstring(buffer));
    }

    /// Polling the peak meter costs a WASAPI call every tick, so it only runs while it can
    /// change something: visible indicator, live device, and not muted.
    void SyncPeakMeter() {
        const bool wanted = _view->IsVisible() && Mic::HasDevice() && !Mic::Muted()
                            && _cfg.Indicator.State == IndicatorState::MutedOrTalk;

        if (wanted == _isPeakMeterActive) {
            return;
        }

        if (!wanted) {
            KillPeakMeter();
            return;
        }

        _isPeakMeterActive = true;
        SetTimer(_view->GetHandle(), ID_PEAK_TIMER, PEAK_TIMER_INTERVAL_MS, nullptr);
    }

    void OnTimerTick(const UINT_PTR timerId) {
        if (timerId == ID_NOTIFICATION_TIMER) {
            KillNotificationTimer();
            UpdateIndicatorLayout();
            return;
        }

        if (timerId != ID_PEAK_TIMER) {
            return;
        }

        if (Mic::Audio().CaptureDevice()->GetPeak() > _cfg.Indicator.VolumeThreshold) {
            if (!_peakMeterPhase) {
                _bitmapToDisplay = _icons->Active();
                _peakMeterPhase = PEAK_METER_DEBOUNCE_PHASES;
                UpdateIndicatorLayout();
            }
        } else if (_peakMeterPhase && (--_peakMeterPhase) == 0) {
            RefreshMicBitmap();
            UpdateIndicatorLayout();
        }
    }

public:
    void Init() override {
        _icons = std::make_unique<IndicatorIcons>(_view->GetHInstance());
        _feedback.Bind(_view->GetHandle(), _view->GetHInstance(), &MainWindow::PostNotification);
        Mic::OnStateChanged += [this] { OnMicStateChanged(); };
        _view->CreateTrayIcon(nullptr, L"");

        _view->OnTrayMenu = [this](UINT_PTR commandId) { OnTrayMenuCommand(commandId); };
        _view->OnRender = [this](const RenderContext& context) {
            _layout.Render(context, _bitmapToDisplay, _notificationText);
        };
        _view->OnRelayout = [this] { UpdateIndicatorLayout(); };
        _view->OnTimer = [this](UINT_PTR timerId) { OnTimerTick(timerId); };

        _view->OnThemeChanged = [this] {
            _icons->RefreshTheme();
            RefreshTrayIcon();
        };

        _view->OnNotification = [this](std::wstring text) {
            _notificationText = std::move(text);

            // Same id, so a second action while the first is still up just restarts the countdown
            SetTimer(_view->GetHandle(), ID_NOTIFICATION_TIMER, NOTIFICATION_DURATION_MS, nullptr);
            UpdateIndicatorLayout();
        };

        RestoreConfig();
    }
};
