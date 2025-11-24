//
// Created by kitbyte on 31.10.2025.
//

#ifndef EASYMIC_MAINWINDOWVIEWMODEL_HPP
#define EASYMIC_MAINWINDOWVIEWMODEL_HPP


#include <filesystem>

#include "RateLimiter.hpp"
#include "SettingsWindowViewModel.hpp"
#include "UACService.hpp"
#include "../Lib/UIAccess/UIAccessManager.hpp"
#include "ViewModel.hpp"
#include "MainWindow/MainWindow.hpp"
#include "View/Core/BaseWindow.hpp"

#include "Utils.hpp"

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
    HICON mutedIcon = nullptr;
    HICON unmutedIcon = nullptr;
    HICON activeIcon = nullptr;

    HICON iconToDisplay = nullptr;

    Resource muteSound;
    Resource unmuteSound;

    bool hasCaptureDevice = false;
    bool captureDeviceMuted = false;
    float captureDeviceVolume = -1.0f;

    bool isPeakMeterActive = false;
    int peakMeterPhase = 0;


    const std::function<void()> HotkeyToggleMute = [this] {
        _audio.CaptureDevice()->ToggleMute();
    };

    const std::function<void()> HotkeyMutePushToTalk = [this] {
        _audio.CaptureDevice()->SetMute(true);
    };

    const std::function<void()> HotkeyUnmutePushToTalk = [this] {
        _audio.CaptureDevice()->SetMute(false);
    };

    const std::function<void()> HotkeyVolumeUp = [this] {
        const auto &mic = *_audio.CaptureDevice();
        BYTE currentVolume = mic.GetVolumePercent();
        mic.SetVolumePercent(std::min<BYTE>(currentVolume + 10, 100));
    };

    const std::function<void()> HotkeyVolumeDown = [this] {
        const auto &mic = *_audio.CaptureDevice();
        BYTE currentVolume = mic.GetVolumePercent();
        mic.SetVolumePercent(std::max<BYTE>(currentVolume - 10, 0));
    };

    std::unordered_map<std::string, HotkeyManager::HotkeyBinding> hotkeyHandlers = {
        {
            HotkeyTitles.ToggleMute, {HotkeyToggleMute}
        },
        {
            HotkeyTitles.PushToTalk, {
                .onPress = HotkeyUnmutePushToTalk,
                .onRelease = HotkeyMutePushToTalk
            }
        },
        {HotkeyTitles.MicVolumeUp, {HotkeyVolumeUp}},
        {HotkeyTitles.MicVolumeDown, {HotkeyVolumeDown}}
    };

    std::unordered_map<uint64_t, HotkeyManager::HotkeyBinding> activeHotkeys;

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
        HotkeyManager::Dispose();
        HotkeyManager::ClearHotkeys();
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
        _audio.PlaybackDevice()->SetSimpleVolumePercent(_cfg.BellVolume);
    }

    void RestoreConfig() {
        _audio.WatchForCaptureSessions();
        HotkeyManager::ClearHotkeys();

#ifdef NDEBUG
        if (!_cfg.Hotkeys.empty()) {
            for (const auto& [actionTitle, mask] : _cfg.Hotkeys) {
                const auto handlerIt = hotkeyHandlers.find(actionTitle);
                if (handlerIt != hotkeyHandlers.end()) {
                    HotkeyManager::RegisterHotkey(
                        mask,
                        handlerIt->second
                    );
                }
            }
            HotkeyManager::Initialize();
        }
#endif

        auto *hInst = _view->GetHInstance();

        unmuteSound = !_cfg.UnmuteSoundSource.empty() && std::filesystem::exists(_cfg.UnmuteSoundSource)
                          ? Utils::LoadFileAsResource(_cfg.UnmuteSoundSource)
                          : Utils::LoadResource(hInst, MAKEINTRESOURCE(IDR_UNMUTE), "WAVE");

        muteSound = !_cfg.MuteSoundSource.empty() && std::filesystem::exists(_cfg.MuteSoundSource)
                        ? Utils::LoadFileAsResource(_cfg.MuteSoundSource)
                        : Utils::LoadResource(hInst, MAKEINTRESOURCE(IDR_MUTE), "WAVE");

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
            case ID_APP_SETTINGS:

                if (_settingsWindow) {
                    return;
                }
                // DetachListeners();
                SuspendActivity();
                iconToDisplay = unmutedIcon;
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
        if (!iconToDisplay) {
            return;
        }

        GraphicsPath path;
        RectF rect(0, 0, static_cast<float>(ctx.width), static_cast<float>(ctx.height));

        GDIRenderer::CreateRoundedRectPath(path, rect, WND_CORNER_RADIUS);
        SolidBrush brush(Color(WND_BG_ALPHA, WND_BG_R, WND_BG_G, WND_BG_B));
        ctx.graphics->FillPath(&brush, &path);

        Bitmap iconBitmap(iconToDisplay);
        const auto iconSize = _cfg.IndicatorSize;

        ctx.graphics->DrawImage(&iconBitmap,
                                ctx.width / 2 - iconSize / 2,
                                ctx.width / 2 - iconSize / 2,
                                iconSize,
                                iconSize
        );
    }

    void CaptureDeviceStateChanged(bool silent) {
        const auto &mic = *_audio.CaptureDevice();

        if (!hasCaptureDevice) {
            _view->UpdateTrayTooltip(L"Easymic - No device");
            iconToDisplay = nullptr;
            _view->UpdateTrayIcon(mutedIcon);
            _view->Hide();
            return;
        }

        iconToDisplay = captureDeviceMuted ? mutedIcon : nullptr;
        _view->UpdateTrayIcon(captureDeviceMuted ? mutedIcon : unmutedIcon);
        _view->Invalidate();

        constexpr auto bufferSize = 255;

        wchar_t buffer[bufferSize];
        swprintf(buffer, bufferSize, L"Easymic - %ls [%d%%]",
                 mic.GetDeviceName(),
                 mic.GetVolumePercent());
        _view->UpdateTrayTooltip(std::wstring(buffer));

        if (!silent && _cfg.BellVolume > 0) {
            const auto &soundResource = captureDeviceMuted ? muteSound : unmuteSound;
            if (!soundResource.empty()) {
                PlaySoundA(
                    (LPCSTR) soundResource.buffer(),
                    nullptr,
                    SND_ASYNC | SND_MEMORY
                );
            }
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

        if (_cfg.IndicatorState == IndicatorState::MutedOrTalk && !isPeakMeterActive) {
            isPeakMeterActive = true;
            SetTimer(_view->GetHandle(), ID_PEAK_TIMER, PEAK_TIMER_INTERVAL_MS, nullptr);
        }
        _view->Show();
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


        AttachListeners();
        _view->CreateTrayIcon(nullptr, L"");

        _view->SetOnTrayMenu([this](UINT_PTR commandId) {
            OnTrayMenuCommand(commandId);
        });

        _view->SetRenderCallback([this](const RenderContext &context) {
            this->OnRender(context);
        });

        _view->SetTimerCallback([this](UINT_PTR timerId) {
            const auto peak = _audio.CaptureDevice()->GetPeak();
            if (peak > _cfg.IndicatorVolumeThreshold) {
                if (!peakMeterPhase) {
                    OutputDebugStringA("The microphone has become active\n");
                    iconToDisplay = activeIcon;
                    peakMeterPhase = PEAK_METER_DEBOUNCE_PHASES;
                    _view->Invalidate();
                }
            } else if (peakMeterPhase && (--peakMeterPhase) == 0) {
                OutputDebugStringA("The microphone has become inactive\n");
                iconToDisplay = captureDeviceMuted ? mutedIcon : nullptr;
                _view->Invalidate();
            }
        });

        RestoreConfig();
    }

    ~MainWindowViewModel() {
        if (mutedIcon) {
            DestroyIcon(mutedIcon);
        }
        if (unmutedIcon) {
            DestroyIcon(unmutedIcon);
        }
    }
};
#endif //EASYMIC_MAINWINDOWVIEWMODEL_HPP
