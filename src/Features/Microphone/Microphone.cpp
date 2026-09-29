#include "Microphone.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>

#include "AppConfig.hpp"
#include "Wasapi/AudioManager.hpp"
#include "Core/ActionRegistry.hpp"
#include "Core/Dispatcher.hpp"
#include "Core/Feedback.hpp"
#include "Core/Lifecycle.hpp"
#include "Core/SettingsHost.hpp"
#include "Core/Tray.hpp"
#include "definitions.h"
#include "Logger.hpp"
#include "MicLayer.hpp"
#include "SoundCatalog.hpp"
#include "Str.hpp"
#include "Tokens.hpp"

namespace {

    Event<> _stateChanged;

    MicSettings* _settings = nullptr;
    AppConfig* _config = nullptr;
    HINSTANCE _instance = nullptr;

    /// What {volume}, {mic} and {bell} show. The device reports only from its callback, so an action publishes what it asked for.
    std::atomic<uint8_t> _shownVolume = 0;
    std::atomic<bool> _shownMuted = false;
    std::atomic<bool> _shownBell = true;

    constexpr char VolumeToken[] = "{volume}";
    constexpr char MicToken[] = "{mic}";
    constexpr char BellToken[] = "{bell}";

    std::string _expand(std::string text) {
        text = Str::Replace(std::move(text), VolumeToken, std::to_string(_shownVolume.load()));
        text = Str::Replace(std::move(text), MicToken, _shownMuted ? "off" : "on");
        return Str::Replace(std::move(text), BellToken, _shownBell ? "on" : "off");
    }

    std::atomic<bool> _muted = false;
    int8_t _prevBellVolume = 25;

    void _shiftVolume(const int delta) {
        const auto mic = Mic::Audio().CaptureDevice();
        if (!mic->IsInitialized()) {
            return;
        }
        const int target = std::clamp(mic->GetVolumePercent() + delta, 0, 100);
        // Posted ahead of the device's echo, which would restore the old kept level
        Dispatcher::ToUi([target] {
            if (_settings->KeepVolume && _settings->Volume != -1) {
                _settings->Volume = static_cast<int8_t>(target);
                _config->Save();
            }
        });
        mic->SetVolumePercent(static_cast<BYTE>(target));
        _shownVolume = static_cast<uint8_t>(target);
    }

    void _enforceKeptVolume() {
        if (!_settings->KeepVolume || _settings->Volume == -1) {
            return;
        }
        const auto mic = Mic::Audio().CaptureDevice();
        if (mic->GetVolumePercent() != _settings->Volume) {
            mic->SetVolumePercent(_settings->Volume);
        }
    }

    void _settle(const bool flipped) {
        // muted is taken now: a second event can land before this hop, and the chime would report the wrong direction
        Dispatcher::ToUi([flipped, muted = _muted.load()] {
            const auto mic = Mic::Audio().CaptureDevice();
            if (mic->IsInitialized()) {
                _shownVolume = mic->GetVolumePercent();
            }

            _stateChanged();

            if (flipped && mic->IsInitialized() && _settings->BellVolume > 0) {
                SoundCatalog::Play(_instance,
                                   muted ? _settings->MuteSound : _settings->UnmuteSound,
                                   static_cast<uint8_t>(_settings->BellVolume));
            }
        });
    }

    void _attachListeners() {
        Mic::Audio().OnCaptureStateChanged += [](const bool muted) {
            const bool flipped = _muted.exchange(muted) != muted;
            _shownMuted = muted;
            _settle(flipped);
            Dispatcher::ToUi(_enforceKeptVolume);
        };

        Mic::Audio().OnCaptureSessionsChanged += [] { Mic::Refresh(); };
        Mic::Audio().OnDefaultCaptureChanged += [] { Mic::Refresh(); };
    }

    void _toggleBell() {
        if (_settings->BellVolume > 0) {
            _prevBellVolume = _settings->BellVolume;
            _settings->BellVolume = 0;
        } else {
            _settings->BellVolume = _prevBellVolume > 0 ? _prevBellVolume : 25;
        }
        _shownBell = _settings->BellVolume > 0;
        _config->Save();
    }

    std::wstring _tooltip() {
        const auto mic = Mic::Audio().CaptureDevice();
        if (!mic->IsInitialized()) {
            return APP_NAME L" - No device";
        }

        constexpr auto bufferSize = 255;
        wchar_t buffer[bufferSize];
        swprintf(buffer, bufferSize, APP_NAME L" - %ls [%d%%]", mic->GetDeviceName(), _shownVolume.load());
        return buffer;
    }

    constexpr ActionDesc Actions[] = {
        {.Id = "mic.toggle_mute",
         .Title = "Toggle mute",
         .Group = "Microphone",
         .Flags = ActionFlags::NoSound,
         .DefaultNotification = "Mic {mic}",
         .Make = [](const ActionContext&) -> ActionFn {
             return [] {
                 // The device state lags by its echo: two quick presses from it would both mute
                 const bool muted = !_shownMuted;
                 Mic::Audio().CaptureDevice()->SetMute(muted);
                 _shownMuted = muted;
             };
         }},

        // No notification: on a held key it would flicker
        {.Id = "mic.push_to_talk",
         .Title = "Push to talk",
         .Group = "Microphone",
         .Flags = ActionFlags::NoSound,
         .Make = [](const ActionContext&) -> ActionFn {
             return [] {
                 Mic::Audio().CaptureDevice()->SetMute(false);
                 _shownMuted = false;
             };
         },
         .MakeRelease = [](const ActionContext&) -> ActionFn {
             return [] {
                 Mic::Audio().CaptureDevice()->SetMute(true);
                 _shownMuted = true;
             };
         }},

        {.Id = "mic.volume_up",
         .Title = "Mic volume up",
         .Group = "Microphone",
         .DefaultSound = "Tick",
         .DefaultNotification = "Mic {volume}%",
         .Make = [](const ActionContext&) -> ActionFn { return [] { _shiftVolume(10); }; }},

        {.Id = "mic.volume_down",
         .Title = "Mic volume down",
         .Group = "Microphone",
         .DefaultSound = "Tick",
         .DefaultNotification = "Mic {volume}%",
         .Make = [](const ActionContext&) -> ActionFn { return [] { _shiftVolume(-10); }; }},

        {.Id = "mic.toggle_bell",
         .Title = "Toggle bell sound",
         .Group = "Microphone",
         .DefaultSound = "Tick",
         .DefaultNotification = "Bell {bell}",
         .Make = [](const ActionContext&) -> ActionFn {
            return [] {
                _shownBell = !_shownBell;
                 Dispatcher::ToUi(_toggleBell);
             };
         }},
    };

    constexpr const wchar_t* PillModes[] = {L"Hidden", L"Muted", L"Muted or talking"};

    constexpr SettingsRow PageRows[] = {
        // -1 is never set: the slider opens on the device's level
        {.Kind = RowKind::Slider, .Label = L"Level",
         .Field = {.Get = [](const AppConfig& cfg) {
                       return cfg.Mic.Volume == -1 ? int{Mic::Audio().CaptureDevice()->GetVolumePercent()}
                                                   : int{cfg.Mic.Volume};
                   },
                   .Set = Bind<&AppConfig::Mic, &MicSettings::Volume>().Set}},
        {.Kind = RowKind::Check, .Label = L"Keep the device at this level",
         .Field = Bind<&AppConfig::Mic, &MicSettings::KeepVolume>()},
        {.Kind = RowKind::Slider, .Label = L"Chime volume", .Field = Bind<&AppConfig::Mic, &MicSettings::BellVolume>()},
        {.Kind = RowKind::SoundPicker, .Label = L"Mute sound", .Field = Bind<&AppConfig::Mic, &MicSettings::MuteSound>()},
        {.Kind = RowKind::SoundPicker, .Label = L"Unmute sound", .Field = Bind<&AppConfig::Mic, &MicSettings::UnmuteSound>()},
        {.Kind = RowKind::Combo, .Label = L"Show pill while",
         .Field = Bind<&AppConfig::Mic, &MicSettings::Pill>(), .Items = [] { return std::span<const wchar_t* const>{PillModes}; }},
        {.Kind = RowKind::Slider, .Label = L"Talking threshold (%)",
         // Rounded: 0.29f * 100 truncates to 28 and the sync would drag the slider back
         .Field = {.Get = [](const AppConfig& cfg) { return static_cast<int>(std::lround(cfg.Mic.VolumeThreshold * 100)); },
                   .Set = [](AppConfig& cfg, int value) { cfg.Mic.VolumeThreshold = static_cast<float>(value) / 100.0f; }}},
        {.Kind = RowKind::Check, .Label = L"Hide while nothing is recording",
         .Field = Bind<&AppConfig::Mic, &MicSettings::HideWhenInactive>()},
    };

} // anonymous namespace

namespace Mic {

    IEvent<>& OnStateChanged = _stateChanged;

    void Register(Host& host) {
        _settings = &host.Config.Mic;
        _config = &host.Config;
        _instance = host.Instance;
        host.Fb.AddResolver(&_expand);
        Tokens::Add({VolumeToken, "Microphone volume, 0-100", Tokens::Notification, false});
        Tokens::Add({MicToken, "Microphone, on or off", Tokens::Notification, false});
        Tokens::Add({BellToken, "Bell sound, on or off", Tokens::Notification, false});

        if (!Mic::Audio().Init()) {
            LOG_ERROR("AudioManager failed to initialize - continuing without microphone control");
        }

        for (const auto& desc : Actions) {
            ActionRegistry::Add(desc);
        }
        SettingsHost::AddPage({.Title = L"Microphone", .Rows = PageRows});

        MicLayer::Register(host.Instance, host.Config.Mic);

        Tray::Add({.Id = "mic.state",
                   .Title = L"Microphone state",
                   .Icon = &MicLayer::TrayIcon,
                   .Tooltip = &_tooltip,
                   .ThemeChanged = &MicLayer::RefreshTheme,
                   .MenuLabel = []() -> const wchar_t* {
                       return _settings->BellVolume > 0 ? L"Disable bell sound" : L"Enable bell sound";
                   },
                   .MenuInvoke = &_toggleBell});
        Mic::OnStateChanged += [] { Tray::Changed(); };

        // The watch resumes before the frame lays the overlay out, and the posted state event lands after it
        Lifecycle::Suspend += [] { Mic::Audio().StopWatchingForCaptureSessions(); };
        Lifecycle::Restore += [] {
            Mic::Audio().WatchForCaptureSessions();
            Mic::Refresh();
            MicLayer::Refresh();
        };
        _attachListeners();
    }

    AudioManager& Audio() {
        // Function-local: a namespace-scope one would fire callbacks into a freed config at teardown
        static AudioManager audio;
        return audio;
    }

    bool HasDevice() {
        return Mic::Audio().CaptureDevice()->IsInitialized();
    }

    bool Muted() {
        return _muted;
    }

    void Refresh() {
        const auto mic = Mic::Audio().CaptureDevice();
        _muted = mic->IsMuted();
        _shownMuted = !mic->IsInitialized() || _muted;

        // Posted first: the volume _settle publishes has to be the one already enforced
        Dispatcher::ToUi([] {
            _shownBell = _settings->BellVolume > 0;
            _enforceKeptVolume();
        });
        _settle(false);
    }

}
