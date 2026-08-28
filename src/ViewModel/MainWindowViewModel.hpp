//
// Created by kitbyte on 31.10.2025.
//

#pragma once

#include <algorithm>
#include <atomic>
#include <memory>

#include "ActionFeedback.hpp"
#include "Core/Dispatcher.hpp"
#include "Core/HotkeyService.hpp"
#include "../Lib/InputLanguage.hpp"
#include "SettingsWindowViewModel.hpp"
#include "UACService.hpp"
#include "../Lib/UIAccess/UIAccessManager.hpp"
#include "ViewModel.hpp"
#include "MainWindow/MainWindow.hpp"
#include "View/Components/IndicatorIcons.hpp"
#include "View/Components/IndicatorLayout.hpp"
#include "View/Core/BaseWindow.hpp"

class AudioManager;

using namespace Gdiplus;

class MainWindowViewModel final : public BaseViewModel<MainWindow> {
private:
    static constexpr UINT ID_PEAK_TIMER = WM_USER + 100;
    static constexpr UINT ID_NOTIFICATION_TIMER = WM_USER + 101;
    static constexpr int PEAK_TIMER_INTERVAL_MS = 150;
    static constexpr int NOTIFICATION_DURATION_MS = 2000;
    static constexpr int PEAK_METER_DEBOUNCE_PHASES = 2;
    static constexpr const char *SHADOW_WINDOW_KEY = "EasyLauncherIndicator";

    std::unique_ptr<SettingsWindow> _settingsWindow;

    AudioManager &_audio;
    AppConfig &_cfg;
    ActionFeedback _feedback;

    std::unique_ptr<IndicatorIcons> _icons;
    Bitmap* _bitmapToDisplay = nullptr;

    bool _hasCaptureDevice = false;
    // Written from the WASAPI notification thread, read from the hotkey worker and the UI
    std::atomic<bool> _captureDeviceMuted = false;
    float _captureDeviceVolume = -1.0f;
    int8_t _prevBellVolume = 25;

    bool _isPeakMeterActive = false;
    int _peakMeterPhase = 0;

    IndicatorLayout _layout;
    /// UI thread only - the worker hands its text over through the message queue. Empty is "none".
    std::wstring _notificationText;
    /// Where the user left the indicator. Widening it for a notification must not move this.
    POINT _anchor{};

    void ShiftMicVolume(const int delta) {
        // Keeps the controller alive for the call: the shared_ptr is returned by value, and the
        // device-change task can drop the last other reference at any moment
        const auto mic = _audio.CaptureDevice();
        const int target = std::clamp(mic->GetVolumePercent() + delta, 0, 100);
        mic->SetVolumePercent(static_cast<BYTE>(target));
        _feedback.PublishVolumePercent(static_cast<uint8_t>(target));
    }

    /// What each built-in id actually does. No default label: adding an id without a case here
    /// is a warning rather than an action that silently never fires. The argument is whatever
    /// the user typed in the action's own field, and only the ones with a label read it.
    std::function<void()> MakeBuiltInHandler(const BuiltInId id, const std::string& args) {
        switch (id) {
            case BuiltInId::ToggleMute:
                // The device only reports a new mute from its callback, so the action publishes
                // what it just asked for - the same trick {volume} uses, or {mic} would report
                // the state the microphone was in before the key was pressed
                return [this] {
                    _audio.CaptureDevice()->ToggleMute();
                    _feedback.PublishMicMuted(!_feedback.MicMuted());
                };
            case BuiltInId::PushToTalk:
                return [this] {
                    _audio.CaptureDevice()->SetMute(false);
                    _feedback.PublishMicMuted(false);
                };
            case BuiltInId::MicVolumeUp:
                return [this] { ShiftMicVolume(10); };
            case BuiltInId::MicVolumeDown:
                return [this] { ShiftMicVolume(-10); };
            case BuiltInId::ToggleBellSound:
                // The config belongs to the UI thread, so the worker publishes what it asked for
                // - the same trick {volume} uses - and hands the write itself over to that thread
                return [this] {
                    _feedback.PublishBellEnabled(!_feedback.BellEnabled());
                    Dispatcher::ToUi([this] { ToggleBellSound(); });
                };
            case BuiltInId::SwitchLanguage:
                return [args] { InputLanguage::SwitchNext(args); };
            case BuiltInId::Count:
                break;
        }
        return {};
    }

    /// Push to talk is the one action that needs the key going up as well.
    const std::function<void()> _releasePushToTalk = [this] {
        _audio.CaptureDevice()->SetMute(true);
        _feedback.PublishMicMuted(true);
    };

public:
    MainWindowViewModel(BaseWindow* baseView, AppConfig& config, AudioManager& audioManager)
        : BaseViewModel(baseView), _audio(audioManager), _cfg(config), _feedback(config) {
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
        _audio.StopWatchingForCaptureSessions();
        HotkeyService::Dispose(); // also drops every registered hotkey
    }

    void RefreshTrayIcon() const {
        _view->UpdateTrayIcon(_icons->Tray(!_hasCaptureDevice || _captureDeviceMuted));
    }

    void KillPeakMeter() {
        if (_isPeakMeterActive) {
            KillTimer(_view->GetHandle(), ID_PEAK_TIMER);
            _isPeakMeterActive = false;
            _peakMeterPhase = 0;
        }
    }

    void AdjustMicVolume() const {
        if (_cfg.IsMicKeepVolume && _cfg.MicVolume != -1) {
            _audio.CaptureDevice()->SetVolumePercent(_cfg.MicVolume);
        }
    }

    /// UI thread only - the hotkey worker asks for this through the command queue.
    void ToggleBellSound() {
        if (_cfg.BellVolume > 0) {
            _prevBellVolume = _cfg.BellVolume;
            _cfg.BellVolume = 0;
        } else {
            _cfg.BellVolume = _prevBellVolume > 0 ? _prevBellVolume : 25;
        }
        _feedback.PublishBellEnabled(_cfg.BellVolume > 0);
        _cfg.Save();
    }

    /// Everything an action needs to fire: its combination, how many presses of it, its feedback,
    /// which edge it runs on, whether the key reaches anything else and whether a tap is required.
    bool RegisterAction(const uint64_t hotkey, const uint8_t presses, std::function<void()> run,
                        const bool onRelease, const bool holdOnly, const bool block, const bool tapOnly) {
        return HotkeyService::RegisterHotkey(hotkey, presses ? presses : 1,
            holdOnly ? HotkeyService::HotkeyBinding{.onPress = std::move(run),
                                                    .onRelease = _releasePushToTalk, .block = block}
            : onRelease ? HotkeyService::HotkeyBinding{.onRelease = std::move(run), .block = block,
                                                       .tapOnly = tapOnly}
                        : HotkeyService::HotkeyBinding{.onPress = std::move(run), .block = block});
    }

    /// A configured hotkey is not a registered one - an action can carry no combination, and
    /// hooking the desktop for nothing is not free.
    bool RegisterConfiguredActions() {
        bool registered = false;

        for (const auto& action : _cfg.Actions) {
            const BuiltInAction* builtIn = action.BuiltIn.empty()
                                               ? nullptr : BuiltInActions::Find(action.BuiltIn);
            if (!action.Hotkey || (!builtIn && action.Command.empty())) {
                continue;
            }

            const std::string text = action.ShowNotification
                ? ActionFeedback::Compose(action.Notification,
                                          builtIn ? builtIn->DefaultNotification
                                                  : BuiltInActions::DefaultNotification,
                                          action.Name, action.Hotkey)
                : std::string{};

            std::function<void()> run;
            if (builtIn) {
                auto handler = MakeBuiltInHandler(builtIn->Id, action.Args);
                if (!handler) {
                    // Only reachable when a new id was added to the table without a case for it
                    LOG_ERROR("Built-in action '%s' has no handler", builtIn->Key);
                    continue;
                }
                run = _feedback.Wrap(std::move(handler), action.Sound, action.SoundVolume, text);
            } else {
                // A captured command announces itself when it is done, so the wrapper must not
                // do it too
                run = _feedback.Wrap(_feedback.ForCommand(action.Command, text), action.Sound,
                                     action.SoundVolume,
                                     text.contains(Tokens::Stdout) ? std::string{} : text);
            }

            registered |= RegisterAction(action.Hotkey, action.Presses, std::move(run),
                                         action.OnRelease, builtIn && builtIn->HoldOnly,
                                         action.Block, action.TapOnly);
        }

        return registered;
    }

    void ApplyDisplayAffinity() const {
        const auto affinity = _cfg.ExcludeFromCapture ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE;
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
        _audio.WatchForCaptureSessions();
        HotkeyService::ClearHotkeys();
        HotkeyService::SetMultiPressWindow(_cfg.MultiPressWindowMs);
        _feedback.PublishBellEnabled(_cfg.BellVolume > 0);

#ifndef APP_NO_GLOBAL_HOOKS
        if (RegisterConfiguredActions()) {
            HotkeyService::Initialize();
        }
#endif // APP_NO_GLOBAL_HOOKS - Debug builds skip the desktop-wide hooks

        if (_cfg.OnTopExclusive && UAC::IsElevated() && !_view->IsOvershadowed()) {
            _view->Hide();
            auto *shadowHwnd = UIAccessManager::GetOrCreateWindow(SHADOW_WINDOW_KEY, MainWindow::StyleEx,
                                                                  MainWindow::Style);
            _view->SetShadowHwnd(shadowHwnd);
            _view->RefreshPos(HWND_TOPMOST);
        }

        ApplyDisplayAffinity();
        UpdateDevice();
    }

    /// Whether the mic pill may be on screen at all - config and live sessions, no mute state.
    bool MicAllowed() const {
        return _hasCaptureDevice && _cfg.IndicatorState != IndicatorState::Hidden
               && (!_cfg.HideWhenInactive || _audio.CaptureDevice()->GetActiveSessionsCount() > 0);
    }

    /// Muted is the only thing the pill shows by itself - the peak meter overrides it while talking.
    void RefreshMicBitmap() {
        _bitmapToDisplay = (MicAllowed() && _captureDeviceMuted) ? _icons->Muted() : nullptr;
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

        _layout = IndicatorLayout::Compute(_cfg.IndicatorSize, _bitmapToDisplay != nullptr,
                                           _notificationText);

        // "Muted or talking" keeps an empty window up on purpose: the peak meter only ticks while
        // the indicator is visible, and it is what discovers that the mic went live
        const bool waitsForPeak = _cfg.IndicatorState == IndicatorState::MutedOrTalk && MicAllowed();

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
        settings->AttachViewModel<SettingsWindowViewModel>(_cfg, _audio);

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
                ToggleBellSound();
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

    void CaptureDeviceStateChanged(const bool silent) {
        if (!_hasCaptureDevice) {
            _view->UpdateTrayTooltip(APP_NAME L" - No device");
            _bitmapToDisplay = nullptr;
            RefreshTrayIcon();
            UpdateIndicatorLayout();
            return;
        }

        RefreshMicBitmap();
        RefreshTrayIcon();
        UpdateIndicatorLayout();

        const auto mic = _audio.CaptureDevice();
        // The device has spoken, so whatever an action optimistically published is now stale
        _feedback.PublishVolumePercent(mic->GetVolumePercent());

        constexpr auto bufferSize = 255;
        wchar_t buffer[bufferSize];
        swprintf(buffer, bufferSize, APP_NAME L" - %ls [%d%%]",
                 mic->GetDeviceName(), _feedback.VolumePercent());
        _view->UpdateTrayTooltip(std::wstring(buffer));

        // Mic state feedback: fires for a mute from anywhere, not just from our own hotkey
        if (!silent && _cfg.BellVolume > 0) {
            SoundCatalog::Play(_view->GetHInstance(),
                               _captureDeviceMuted ? _cfg.MuteSoundSource : _cfg.UnmuteSoundSource,
                               static_cast<uint8_t>(_cfg.BellVolume));
        }
    }

    void UpdateDevice() {
        const auto mic = _audio.CaptureDevice();
        _hasCaptureDevice = mic->IsInitialized();
        _captureDeviceMuted = mic->IsMuted();
        // No device is not "live" either, so {mic} reads off rather than claiming an open mic
        _feedback.PublishMicMuted(!_hasCaptureDevice || _captureDeviceMuted);
        _captureDeviceVolume = mic->GetVolumeLevel();
        _feedback.PublishVolumePercent(mic->GetVolumePercent());

        AdjustMicVolume();

        CaptureDeviceStateChanged(true);
    }

    /// Polling the peak meter costs a WASAPI call every tick, so it only runs while it can
    /// change something: visible indicator, live device, and not muted.
    void SyncPeakMeter() {
        const bool wanted = _view->IsVisible() && _hasCaptureDevice && !_captureDeviceMuted
                            && _cfg.IndicatorState == IndicatorState::MutedOrTalk;

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

    /// Subscribed once for the lifetime of the app - the view model outlives every device.
    void AttachListeners() {
        _audio.OnCaptureStateChanged += [this](bool muted, float level) {
            const bool silent = _captureDeviceMuted == muted;
            _captureDeviceMuted = muted;
            _feedback.PublishMicMuted(muted);
            CaptureDeviceStateChanged(silent);

            if (level != _captureDeviceVolume) {
                _captureDeviceVolume = level;
                AdjustMicVolume();
            }
        };

        _audio.OnCaptureSessionPropertyChanged += [this](const ComPtr<IAudioSessionControl>&,
                                                         EAudioSessionProperty property) {
            if (property == Disconnected || property == Connected || property == State) {
                UpdateDevice();
            }
        };

        _audio.OnDefaultCaptureChanged += [this] { UpdateDevice(); };
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

        if (_audio.CaptureDevice()->GetPeak() > _cfg.IndicatorVolumeThreshold) {
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
        _feedback.Bind(_view->GetHandle(), _view->GetHInstance());
        AttachListeners();
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
