//
// Created by kitbyte on 31.10.2025.
//

#ifndef EASYMIC_MAINWINDOWVIEWMODEL_HPP
#define EASYMIC_MAINWINDOWVIEWMODEL_HPP



#include "HotkeyManager.hpp"
#include "RateLimiter.hpp"
#include "SettingsWindowViewModel.hpp"
#include "UACService.hpp"
#include "../Lib/UIAccess/UIAccessManager.hpp"
#include "ViewModel.hpp"
#include "MainWindow/MainWindow.hpp"
#include "View/Core/BaseWindow.hpp"

#include "SoundCatalog.hpp"
#include "CommandRunner.hpp"

class AudioManager;

using namespace Gdiplus;

class MainWindowViewModel final : public BaseViewModel<MainWindow> {
private:
    static constexpr UINT ID_PEAK_TIMER = WM_USER + 100;
    static constexpr int PEAK_TIMER_INTERVAL_MS = 150;  // milliseconds
    static constexpr int PEAK_METER_DEBOUNCE_PHASES = 2;
    static constexpr const char *SHADOW_WINDOW_KEY = "EasymicIndicator";
    
    // Window background color (RGBA)
    static constexpr BYTE WND_BG_R = 24;
    static constexpr BYTE WND_BG_G = 27;
    static constexpr BYTE WND_BG_B = 40;
    static constexpr BYTE WND_BG_ALPHA = 220;
    static constexpr float WND_CORNER_RADIUS = 10.0f;

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
    bool captureDeviceMuted = false;
    float captureDeviceVolume = -1.0f;
    int8_t _prevBellVolume = 25;

    bool isPeakMeterActive = false;
    int peakMeterPhase = 0;


    void ShiftMicVolume(const int delta) const {
        const auto &mic = *_audio.CaptureDevice();
        mic.SetVolumePercent(static_cast<BYTE>(std::clamp(mic.GetVolumePercent() + delta, 0, 100)));
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

    /// Wraps an action so it announces itself first. Runs on the hotkey worker, never in the hook.
    std::function<void()> WithSound(std::function<void()> handler, const std::string& sound) const {
        if (sound.empty()) {
            return handler;
        }

        return [handler = std::move(handler), sound, hInstance = _view->GetHInstance()] {
            SoundCatalog::Play(hInstance, sound);
            handler();
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
    void SuspendActivity() {
        KillPeakMeter();
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
            auto run = WithSound(handler->second, binding.Sound);

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

            auto run = WithSound([command = action.Command] { CommandRunner::Run(command); }, action.Sound);
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
                // DetachListeners();
                SuspendActivity();
                bitmapToDisplay = unmutedBitmap.get();
                _view->Invalidate();
                _view->Show();
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

    void OnRender(const RenderContext &ctx) {
        if (!bitmapToDisplay) {
            return;
        }

        GraphicsPath path;
        RectF rect(0, 0, static_cast<float>(ctx.width), static_cast<float>(ctx.height));

        GDIRenderer::CreateRoundedRectPath(path, rect, WND_CORNER_RADIUS);
        SolidBrush brush(Color(WND_BG_ALPHA, WND_BG_R, WND_BG_G, WND_BG_B));
        ctx.graphics->FillPath(&brush, &path);

        const auto iconSize = _cfg.IndicatorSize;

        ctx.graphics->DrawImage(bitmapToDisplay,
                                ctx.width / 2 - iconSize / 2,
                                ctx.height / 2 - iconSize / 2,
                                iconSize,
                                iconSize
        );
    }

    void CaptureDeviceStateChanged(bool silent) {
        const auto &mic = *_audio.CaptureDevice();

        if (!hasCaptureDevice) {
            _view->UpdateTrayTooltip(L"Easymic - No device");
            bitmapToDisplay = nullptr;
            RefreshTrayIcon();
            _view->Hide();
            return;
        }

        bitmapToDisplay = captureDeviceMuted ? mutedBitmap.get() : nullptr;
        RefreshTrayIcon();
        _view->Invalidate();
        SyncPeakMeter();

        constexpr auto bufferSize = 255;

        wchar_t buffer[bufferSize];
        swprintf(buffer, bufferSize, L"Easymic - %ls [%d%%]",
                 mic.GetDeviceName(),
                 mic.GetVolumePercent());
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

        AdjustMicVolume();
        AdjustAppVolume();

        CaptureDeviceStateChanged(true);
        const auto activeSessions = _audio.CaptureDevice()->GetActiveSessionsCount();

        if (!hasCaptureDevice || _cfg.IndicatorState == IndicatorState::Hidden || (_cfg.HideWhenInactive && activeSessions == 0)) {
            KillPeakMeter();
            _view->Hide();
            return;
        }

        _view->Show();
        SyncPeakMeter();
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

        _view->SetTimerCallback([this](UINT_PTR timerId) {
            const auto peak = _audio.CaptureDevice()->GetPeak();
            if (peak > _cfg.IndicatorVolumeThreshold) {
                if (!peakMeterPhase) {
                    OutputDebugStringA("The microphone has become active\n");
                    bitmapToDisplay = activeBitmap.get();
                    peakMeterPhase = PEAK_METER_DEBOUNCE_PHASES;
                    _view->Invalidate();
                }
            } else if (peakMeterPhase && (--peakMeterPhase) == 0) {
                OutputDebugStringA("The microphone has become inactive\n");
                bitmapToDisplay = captureDeviceMuted ? mutedBitmap.get() : nullptr;
                _view->Invalidate();
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
