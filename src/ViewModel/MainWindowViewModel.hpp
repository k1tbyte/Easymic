//
// Created by kitbyte on 31.10.2025.
//

#ifndef EASYMIC_MAINWINDOWVIEWMODEL_HPP
#define EASYMIC_MAINWINDOWVIEWMODEL_HPP

#include <atomic>
#include <algorithm>
#include "HotkeyManager.hpp"
#include "RateLimiter.hpp"
#include "SettingsWindowViewModel.hpp"
#include "UACService.hpp"
#include "../Lib/UIAccess/UIAccessManager.hpp"
#include "ViewModel.hpp"
#include "MainWindow/MainWindow.hpp"
#include "View/Components/IndicatorLayout.hpp"
#include "View/Core/BaseWindow.hpp"
#include "Str.hpp"
#include "NotificationTokens.hpp"
#include "SoundCatalog.hpp"
#include "CommandRunner.hpp"

class AudioManager;

using namespace Gdiplus;

class MainWindowViewModel final : public BaseViewModel<MainWindow> {
private:
    static constexpr UINT ID_PEAK_TIMER = WM_USER + 100;
    static constexpr UINT ID_NOTIFICATION_TIMER = WM_USER + 101;
    /// WM_APP, not WM_USER: the two above are timer ids and share nothing but the number line
    static constexpr UINT WM_SHOW_NOTIFICATION = WM_APP + 1;
    static constexpr int PEAK_TIMER_INTERVAL_MS = 150;  // milliseconds
    static constexpr int NOTIFICATION_DURATION_MS = 2000;
    static constexpr int PEAK_METER_DEBOUNCE_PHASES = 2;
    static constexpr const char *SHADOW_WINDOW_KEY = "EasymicIndicator";

    // Window background color (RGBA)
    static constexpr BYTE WND_BG_R = 24;
    static constexpr BYTE WND_BG_G = 27;
    static constexpr BYTE WND_BG_B = 40;
    static constexpr BYTE WND_BG_ALPHA = 220;

    int OnCaptureStateChangedId = -1;
    int OnCaptureSessionPropertyChangedId = -1;
    int OnDefaultCaptureChangedId = -1;

    std::shared_ptr<SettingsWindow> _settingsWindow;

    AudioManager &_audio;
    AppConfig &_cfg;
    // Shared icons from LoadIcon - never DestroyIcon'd
    HICON mutedIcon = nullptr;
    HICON unmutedIcon = nullptr;
    HICON activeIcon = nullptr;

    // Darkened copies for a light taskbar - owned, unlike the shared originals above.
    // The indicator itself always draws the bright ones: it has its own dark background.
    HICON mutedTrayIcon = nullptr;
    HICON unmutedTrayIcon = nullptr;
    bool isTaskbarLight = false;

    // The HICON -> Bitmap conversion is expensive, and only these three are ever drawn
    std::unique_ptr<Bitmap> mutedBitmap;
    std::unique_ptr<Bitmap> unmutedBitmap;
    std::unique_ptr<Bitmap> activeBitmap;
    Bitmap* bitmapToDisplay = nullptr;

    bool hasCaptureDevice = false;
    // Written from the WASAPI notification thread, read from the hotkey worker and the UI
    std::atomic<bool> captureDeviceMuted = false;
    float captureDeviceVolume = -1.0f;
    int8_t _prevBellVolume = 25;

    bool isPeakMeterActive = false;
    int peakMeterPhase = 0;

    IndicatorLayout _layout;
    /// UI thread only - the worker hands its text over through the message queue. Empty is "none".
    std::wstring _notificationText;
    /// Where the user left the indicator. Widening it for a notification must not move this.
    POINT _anchor{};
    /// The device only publishes a new level from its callback, so the action that changed it
    /// posts the value it just asked for - otherwise {volume} shows the previous one.
    std::atomic<uint8_t> _volumePercent = 0;

    void ShiftMicVolume(const int delta) {
        const auto &mic = *_audio.CaptureDevice();
        const int target = std::clamp(mic.GetVolumePercent() + delta, 0, 100);
        mic.SetVolumePercent(static_cast<BYTE>(target));
        _volumePercent = static_cast<uint8_t>(target);
    }

    /// What each built-in id actually does. BuiltInActions::All describes how it is configured.
    std::unordered_map<std::string, std::function<void()>> builtInHandlers = {
        {"Toggle mute",       [this] { _audio.CaptureDevice()->ToggleMute(); }},
        {"Push to talk",      [this] { _audio.CaptureDevice()->SetMute(false); }},
        {"Mic volume up",     [this] { ShiftMicVolume(10); }},
        {"Mic volume down",   [this] { ShiftMicVolume(-10); }},
        {"Toggle bell sound", [this] { ToggleBellSound(); }}
    };

    /// Push to talk is the one action that needs the key going up as well.
    const std::function<void()> releasePushToTalk = [this] { _audio.CaptureDevice()->SetMute(true); };

    /// What an action announces. An empty text means the table default, so a config saved before
    /// that default existed still gets one; the tokens that cannot differ between two presses are
    /// resolved right here, which leaves the default free of braces and ExpandState with no work.
    static std::string NotificationText(const std::string& text, const char* fallback,
                                        const std::string& name, const uint64_t hotkey) {
        const std::string source = text.empty() ? fallback : text;
        if (source.empty()) {
            return {};
        }

        auto resolved = Str::Replace(source, NotificationTokens::Name, name);
        return resolved.find(NotificationTokens::Key) == std::string::npos
                   ? resolved
                   : Str::Replace(std::move(resolved), NotificationTokens::Key,
                                  HotkeyManager::GetHotkeyName(hotkey));
    }

    /// Runs on the hotkey worker, after the action - a state token must read what it has just done.
    std::string ExpandState(const std::string& text) const {
        if (text.find('{') == std::string::npos) {
            return text;
        }

        auto expanded = Str::Replace(text, NotificationTokens::Volume,
                                     std::to_string(_volumePercent.load()));
        return Str::Replace(std::move(expanded), NotificationTokens::Bell,
                            _cfg.BellVolume > 0 ? "on" : "off");
    }

    /// The text travels with the message, so nothing is shared with the UI thread and nothing races.
    /// Takes no view model state: a captured command may answer long after this one is gone, and a
    /// window that no longer exists only costs a failed post.
    static void PostNotification(HWND target, const std::string& text) {
        if (text.empty()) {
            return;
        }

        auto* payload = new std::wstring(Str::Utf8ToWide(text));
        if (!PostMessageW(target, WM_SHOW_NOTIFICATION, 0, reinterpret_cast<LPARAM>(payload))) {
            delete payload;
        }
    }

    void ShowNotification(const std::string& text) const {
        if (text.empty() || !_cfg.NotificationsEnabled) {
            return;
        }

        PostNotification(_view->GetHandle(), ExpandState(text));
    }

    /// {stdout} in the text is what asks for the output, so it also picks how the command is run.
    std::function<void()> CustomActionRunner(const std::string& command, const std::string& text) const {
        if (!text.contains(NotificationTokens::Stdout)) {
            return [command] { CommandRunner::Run(command); };
        }

        return [this, command, text] {
            // Resolved before launching: only the view model can read the state tokens, and it is
            // not safe to touch from the command thread that answers later
            const std::string resolved = _cfg.NotificationsEnabled ? ExpandState(text) : std::string{};

            CommandRunner::RunCaptured(command,
                [target = _view->GetHandle(), resolved](const std::string& output) {
                    PostNotification(target, Str::Replace(resolved, NotificationTokens::Stdout, output));
                });
        };
    }

    /// Wraps an action so it announces itself. Runs on the hotkey worker, never in the hook.
    /// The sound comes first because it is the instant feedback, the text last because it reports.
    std::function<void()> WithActionFeedback(std::function<void()> handler, std::string sound,
                                             std::string notification) const {
        if (sound.empty() && notification.empty()) {
            return std::move(handler);
        }

        return [this, handler = std::move(handler), sound = std::move(sound),
                notification = std::move(notification), hInstance = _view->GetHInstance()] {
            if (!sound.empty()) {
                SoundCatalog::Play(hInstance, sound);
            }
            handler();
            ShowNotification(notification);
        };
    }

public:
    MainWindowViewModel(
        const std::shared_ptr<BaseWindow> &baseView,
        AppConfig &config,
        AudioManager &audioManager) : BaseViewModel(baseView),
                                      _audio(audioManager),
                                      _cfg(config) {
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
        HotkeyManager::Dispose(); // also drops every registered hotkey
    }

    /// Darkened copies are built once, on the first switch to a light taskbar.
    void ApplyTrayTheme() {
        isTaskbarLight = TrayIconTheme::IsLightTaskbar();

        if (isTaskbarLight && !mutedTrayIcon) {
            mutedTrayIcon = TrayIconTheme::Darken(mutedIcon);
            unmutedTrayIcon = TrayIconTheme::Darken(unmutedIcon);
        }
    }

    void RefreshTrayIcon() const {
        const bool muted = !hasCaptureDevice || captureDeviceMuted;
        HICON themed = isTaskbarLight ? (muted ? mutedTrayIcon : unmutedTrayIcon) : nullptr;

        _view->UpdateTrayIcon(themed ? themed : (muted ? mutedIcon : unmutedIcon));
    }

    void KillPeakMeter() {
        if (isPeakMeterActive) {
            KillTimer(_view->GetHandle(), ID_PEAK_TIMER);
            isPeakMeterActive = false;
            peakMeterPhase = 0;
        }
    }

    void AdjustMicVolume() const {
        if (_cfg.IsMicKeepVolume && _cfg.MicVolume != -1) {
            _audio.CaptureDevice()->SetVolumePercent(_cfg.MicVolume);
        }
    }

    void AdjustAppVolume() const {
        const WORD vol = static_cast<WORD>(_cfg.BellVolume * 0xFFFF / 100);
        waveOutSetVolume(nullptr, MAKELONG(vol, vol));
    }

    void ToggleBellSound() {
        if (_cfg.BellVolume > 0) {
            _prevBellVolume = _cfg.BellVolume;
            _cfg.BellVolume = 0;
        } else {
            _cfg.BellVolume = _prevBellVolume > 0 ? _prevBellVolume : 25;
        }
        AdjustAppVolume();
        _cfg.Save();
    }

    void RestoreConfig() {
        _audio.WatchForCaptureSessions();
        HotkeyManager::ClearHotkeys();

#ifndef EASYMIC_NO_GLOBAL_HOOKS
        // A configured hotkey is not a registered one - an action can carry no combination and a
        // built-in name no handler, and hooking the desktop for nothing is not free
        bool registered = false;

        for (const auto& builtIn : BuiltInActions::All) {
            const auto configured = _cfg.Actions.find(builtIn.Id);
            const auto handler = builtInHandlers.find(builtIn.Id);

            if (configured == _cfg.Actions.end() || !configured->second.Hotkey
                || handler == builtInHandlers.end()) {
                continue;
            }

            const ActionBinding& binding = configured->second;
            auto run = WithActionFeedback(handler->second, binding.Sound,
                binding.ShowNotification ? NotificationText(binding.Notification, builtIn.DefaultNotification,
                                                            builtIn.Title, binding.Hotkey)
                                         : std::string{});

            registered |= HotkeyManager::RegisterHotkey(binding.Hotkey,
                builtIn.HoldOnly ? HotkeyManager::HotkeyBinding{.onPress = std::move(run),
                                                                .onRelease = releasePushToTalk}
                : binding.OnRelease ? HotkeyManager::HotkeyBinding{.onRelease = std::move(run)}
                                    : HotkeyManager::HotkeyBinding{.onPress = std::move(run)});
        }

        for (const auto& action : _cfg.CustomActions) {
            if (!action.Hotkey || action.Command.empty()) {
                continue;
            }

            const std::string text = action.ShowNotification
                ? NotificationText(action.Notification, BuiltInActions::DefaultNotification,
                                   action.Name, action.Hotkey)
                : std::string{};

            // A captured command announces itself when it is done, so the wrapper must not do it too
            auto run = WithActionFeedback(CustomActionRunner(action.Command, text), action.Sound,
                                          text.contains(NotificationTokens::Stdout) ? std::string{} : text);
            registered |= HotkeyManager::RegisterHotkey(action.Hotkey, action.OnRelease
                ? HotkeyManager::HotkeyBinding{.onRelease = std::move(run)}
                : HotkeyManager::HotkeyBinding{.onPress = std::move(run)});
        }

        if (registered) {
            HotkeyManager::Initialize();
        }
#endif // EASYMIC_NO_GLOBAL_HOOKS - Debug builds skip the desktop-wide hooks

        if (_cfg.OnTopExclusive && UAC::IsElevated() && !_view->IsOvershadowed()) {
            // Hide original window
            _view->Hide();
            auto *shadowHwnd = UIAccessManager::GetOrCreateWindow(SHADOW_WINDOW_KEY, MainWindow::StyleEx,
                                                                  MainWindow::Style);
            _view->SetShadowHwnd(shadowHwnd);
            _view->RefreshPos(HWND_TOPMOST);
        }

        const auto affinity = _cfg.ExcludeFromCapture ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE;
        DWORD existingAffinity = 0;
        GetWindowDisplayAffinity(_view->GetEffectiveHandle(), &existingAffinity);

        if (existingAffinity != (_cfg.ExcludeFromCapture ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE)) {
            _view->IsOvershadowed()
                                    ? UIAccessManager::InjectDisplayAffinity(_view->GetEffectiveHandle(), affinity)
                                    : SetWindowDisplayAffinity(_view->GetHandle(), affinity);
        }

        UpdateDevice();
    }

    /// Whether the mic pill may be on screen at all - config and live sessions, no mute state.
    bool MicAllowed() const {
        return hasCaptureDevice && _cfg.IndicatorState != IndicatorState::Hidden
               && (!_cfg.HideWhenInactive || _audio.CaptureDevice()->GetActiveSessionsCount() > 0);
    }

    /// Muted is the only thing the pill shows by itself - the peak meter overrides it while talking.
    void RefreshMicBitmap() {
        bitmapToDisplay = (MicAllowed() && captureDeviceMuted) ? mutedBitmap.get() : nullptr;
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

        _layout = IndicatorLayout::Compute(_cfg.IndicatorSize, bitmapToDisplay != nullptr,
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

    void OnTrayMenuCommand(UINT_PTR commandId) {
        switch (commandId) {
            case ID_APP_EXIT:
                PostQuitMessage(0);
                break;
            case ID_APP_TOGGLE_BELL:
                ToggleBellSound();
                break;
            case ID_APP_SETTINGS:
                if (_settingsWindow) {
                    return;
                }
                SuspendActivity();
                bitmapToDisplay = unmutedBitmap.get();
                UpdateIndicatorLayout();
                _settingsWindow = std::make_shared<SettingsWindow>(_view->GetHInstance());
                _settingsWindow->AttachViewModel<SettingsWindowViewModel>(_cfg, _audio);

                if (!_settingsWindow->Initialize({.parentHwnd = _view->GetHandle()})) {
                    throw std::runtime_error("Failed to initialize settings window");
                }

                _settingsWindow->OnExit += [this] {
                    RestoreConfig();
                    _settingsWindow.reset();
                    AttachListeners();
                };
                _settingsWindow->Show();
                break;
        }
    }

    /// Draws what UpdateIndicatorLayout already measured - no geometry is decided here.
    void OnRender(const RenderContext &ctx) {
        if (!_layout.hasMic && !_layout.hasText) {
            return;
        }

        const auto pillHeight = static_cast<float>(_layout.height);
        SolidBrush brush(Color(WND_BG_ALPHA, WND_BG_R, WND_BG_G, WND_BG_B));

        if (_layout.hasMic) {
            GraphicsPath micPath;
            GDIRenderer::CreateRoundedRectPath(micPath, RectF(0, 0, pillHeight, pillHeight),
                                               IndicatorLayout::CornerRadius);
            ctx.graphics->FillPath(&brush, &micPath);

            const int inset = (_layout.height - _layout.iconSize) / 2;
            ctx.graphics->DrawImage(bitmapToDisplay, inset, inset, _layout.iconSize, _layout.iconSize);
        }

        if (!_layout.hasText) {
            return;
        }

        const RectF textRect(static_cast<float>(_layout.textX), 0,
                             static_cast<float>(_layout.textWidth), pillHeight);

        GraphicsPath textPath;
        GDIRenderer::CreateRoundedRectPath(textPath, textRect, IndicatorLayout::CornerRadius);
        ctx.graphics->FillPath(&brush, &textPath);

        StringFormat format;
        format.SetAlignment(StringAlignmentCenter);
        format.SetLineAlignment(StringAlignmentCenter);

        const auto font = _layout.MakeFont();
        SolidBrush textBrush(Color(255, 240, 240, 240));
        ctx.graphics->SetTextRenderingHint(TextRenderingHintAntiAlias);
        ctx.graphics->DrawString(_notificationText.c_str(), -1, &font, textRect, &format, &textBrush);
    }

    void CaptureDeviceStateChanged(bool silent) {
        const auto &mic = *_audio.CaptureDevice();

        if (!hasCaptureDevice) {
            _view->UpdateTrayTooltip(L"Easymic - No device");
            bitmapToDisplay = nullptr;
            RefreshTrayIcon();
            UpdateIndicatorLayout();
            return;
        }

        RefreshMicBitmap();
        RefreshTrayIcon();
        UpdateIndicatorLayout();

        // The device has spoken, so whatever an action optimistically published is now stale
        _volumePercent = mic.GetVolumePercent();

        constexpr auto bufferSize = 255;

        wchar_t buffer[bufferSize];
        swprintf(buffer, bufferSize, L"Easymic - %ls [%d%%]",
                 mic.GetDeviceName(),
                 _volumePercent.load());
        _view->UpdateTrayTooltip(std::wstring(buffer));

        // Mic state feedback: fires for a mute from anywhere, not just from our own hotkey
        if (!silent && _cfg.BellVolume > 0) {
            SoundCatalog::Play(_view->GetHInstance(),
                               captureDeviceMuted ? _cfg.MuteSoundSource : _cfg.UnmuteSoundSource);
        }
    }

    void UpdateDevice() {
        const auto &mic = *_audio.CaptureDevice();
        hasCaptureDevice = mic.IsInitialized();
        captureDeviceMuted = mic.IsMuted();
        captureDeviceVolume = mic.GetVolumeLevel();
        _volumePercent = mic.GetVolumePercent();

        AdjustMicVolume();
        AdjustAppVolume();

        CaptureDeviceStateChanged(true);
    }

    /// Polling the peak meter costs a WASAPI call every tick, so it only runs while it can
    /// change something: visible indicator, live device, and not muted.
    void SyncPeakMeter() {
        const bool wanted = _view->IsVisible() && hasCaptureDevice && !captureDeviceMuted
                            && _cfg.IndicatorState == IndicatorState::MutedOrTalk;

        if (wanted == isPeakMeterActive) {
            return;
        }

        if (!wanted) {
            KillPeakMeter();
            return;
        }

        isPeakMeterActive = true;
        SetTimer(_view->GetHandle(), ID_PEAK_TIMER, PEAK_TIMER_INTERVAL_MS, nullptr);
    }

    void AttachListeners() {
        OnCaptureStateChangedId = _audio.OnCaptureStateChanged += [this](bool muted, float level) {
            bool silent = captureDeviceMuted == muted;
            captureDeviceMuted = muted;
            this->CaptureDeviceStateChanged(silent);

            if (level != captureDeviceVolume) {
                captureDeviceVolume = level;
                this->AdjustMicVolume();
            }
        };

        OnCaptureSessionPropertyChangedId =
                _audio.OnCaptureSessionPropertyChanged += [this](const ComPtr<IAudioSessionControl> &control,
                                                                 EAudioSessionProperty property) {
                    if (property == Disconnected || property == Connected || property == State) {
                        this->UpdateDevice();
                    }
                };

        OnDefaultCaptureChangedId = _audio.OnDefaultCaptureChanged += [this] {
            this->UpdateDevice();
        };
    }

    void DetachListeners() const {
        _audio.OnCaptureStateChanged -= OnCaptureStateChangedId;
        _audio.OnCaptureSessionPropertyChanged -= OnCaptureSessionPropertyChangedId;
        _audio.OnDefaultCaptureChanged -= OnDefaultCaptureChangedId;
    }

public:
    void Init() override {
        auto *hInst = _view->GetHInstance();

        mutedIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_MIC_MUTED));
        unmutedIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_MIC_UNMUTED));
        activeIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_MIC_ACTIVE));

        mutedBitmap = std::make_unique<Bitmap>(mutedIcon);
        unmutedBitmap = std::make_unique<Bitmap>(unmutedIcon);
        activeBitmap = std::make_unique<Bitmap>(activeIcon);

        ApplyTrayTheme();

        AttachListeners();
        _view->CreateTrayIcon(nullptr, L"");

        _view->SetOnTrayMenu([this](UINT_PTR commandId) {
            OnTrayMenuCommand(commandId);
        });

        _view->SetRenderCallback([this](const RenderContext &context) {
            this->OnRender(context);
        });

        _view->SetOnThemeChanged([this] {
            ApplyTrayTheme();
            RefreshTrayIcon();
        });

        _view->SetOnRelayout([this] { UpdateIndicatorLayout(); });

        _view->RegisterMessageHandler(WM_SHOW_NOTIFICATION, [this](WPARAM, LPARAM lParam) {
            const std::unique_ptr<std::wstring> payload(reinterpret_cast<std::wstring*>(lParam));
            _notificationText = std::move(*payload);

            // Same id, so a second action while the first is still up just restarts the countdown
            SetTimer(_view->GetHandle(), ID_NOTIFICATION_TIMER, NOTIFICATION_DURATION_MS, nullptr);
            UpdateIndicatorLayout();
            return 0;
        });

        _view->SetTimerCallback([this](UINT_PTR timerId) {
            if (timerId == ID_NOTIFICATION_TIMER) {
                KillNotificationTimer();
                UpdateIndicatorLayout();
                return;
            }

            if (timerId == ID_PEAK_TIMER) {
                const auto peak = _audio.CaptureDevice()->GetPeak();
                if (peak > _cfg.IndicatorVolumeThreshold) {
                    if (!peakMeterPhase) {
                        OutputDebugStringW(L"The microphone has become active\n");
                        bitmapToDisplay = activeBitmap.get();
                        peakMeterPhase = PEAK_METER_DEBOUNCE_PHASES;
                        UpdateIndicatorLayout();
                    }
                } else if (peakMeterPhase && (--peakMeterPhase) == 0) {
                    OutputDebugStringW(L"The microphone has become inactive\n");
                    RefreshMicBitmap();
                    UpdateIndicatorLayout();
                }
            }
        });

        RestoreConfig();
    }

    ~MainWindowViewModel() {
        // Only the darkened copies are ours - the originals come from LoadIcon and are shared
        if (mutedTrayIcon) {
            DestroyIcon(mutedTrayIcon);
        }
        if (unmutedTrayIcon) {
            DestroyIcon(unmutedTrayIcon);
        }
    }
};
#endif //EASYMIC_MAINWINDOWVIEWMODEL_HPP
