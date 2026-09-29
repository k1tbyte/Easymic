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
    /// Only to save the bell toggle and a hotkey's kept level - the module reads nothing outside its own section
    AppConfig* _config = nullptr;
    HINSTANCE _instance = nullptr;

    /// What {volume}, {mic} and {bell} show. The device reports a change only from its callback, so an
    /// action publishes what it just asked for, or the text would show the state before the press.
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

    // Written from the WASAPI notification thread, read from the hotkey worker and the UI
    std::atomic<bool> _hasDevice = false;
    std::atomic<bool> _muted = false;
    std::atomic<float> _deviceVolume = -1.0f;
    /// UI thread only. What the chime was set to before it was silenced, so toggling it back
    /// restores the level.
    int8_t _prevBellVolume = 25;

    void _shiftVolume(const int delta) {
        // Keeps the controller alive for the call: the shared_ptr is returned by value, and the
        // device-change task can drop the last other reference at any moment
        const auto mic = Mic::Audio().CaptureDevice();
        if (!mic->IsInitialized()) {
            return;
        }
        const int target = std::clamp(mic->GetVolumePercent() + delta, 0, 100);
        // The kept level moves too, posted ahead of the device's echo that would restore the old one
        Dispatcher::ToUi([target] {
            if (_settings->KeepVolume && _settings->Volume != -1) {
                _settings->Volume = static_cast<int8_t>(target);
                _config->Save();
            }
        });
        mic->SetVolumePercent(static_cast<BYTE>(target));
        _shownVolume = static_cast<uint8_t>(target);
    }

    /// The user asked for one level and expects the device to keep it across reconnects.
    void _adjustVolume() {
        if (_settings->KeepVolume && _settings->Volume != -1) {
            Mic::Audio().CaptureDevice()->SetVolumePercent(_settings->Volume);
        }
    }

    /**
     * @brief Settles the state and tells everyone who draws it.
     *
     * @param silent true when the state did not actually flip. The chime fires for a mute from
     *        anywhere, not only from our own hotkey, so it is gated on a real change alone.
     */
    void _settle(const bool silent) {
        // The device speaks on its own COM thread, and everything below draws, plays or reads the
        // config - all three belong to the UI thread. A Refresh already on that thread pays one
        // message hop for the same rule, which is cheaper than two ways of getting here.
        //
        // The state is taken now rather than read inside the lambda: a second event can land
        // before the first hop runs, and then the chime would report the wrong direction.
        Dispatcher::ToUi([silent, muted = _muted.load(), hasDevice = _hasDevice.load()] {
            if (hasDevice) {
                // The device has spoken, so whatever an action optimistically published is stale
                _shownVolume = Mic::Audio().CaptureDevice()->GetVolumePercent();
            }

            _stateChanged();

            if (!silent && hasDevice && _settings->BellVolume > 0) {
                SoundCatalog::Play(_instance,
                                   muted ? _settings->MuteSound : _settings->UnmuteSound,
                                   static_cast<uint8_t>(_settings->BellVolume));
            }
        });
    }

    /// Subscribed once for the lifetime of the app - the module outlives every device.
    void _attachListeners() {
        Mic::Audio().OnCaptureStateChanged += [](const bool muted, const float level) {
            const bool silent = _muted == muted;
            _muted = muted;
            _shownMuted = muted;
            _settle(silent);

            if (level != _deviceVolume) {
                _deviceVolume = level;
                // Posted after _settle so it keeps its old place in the order, and posted at all
                // because it reads the config
                Dispatcher::ToUi(_adjustVolume);
            }
        };

        Mic::Audio().OnCaptureSessionsChanged += [] { Mic::Refresh(); };
        Mic::Audio().OnDefaultCaptureChanged += [] { Mic::Refresh(); };
    }

    /// Silences the mic state chime, or restores the level it had. UI thread: the tray menu and
    /// the mic.toggle_bell action both end up here.
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
        if (!_hasDevice) {
            return APP_NAME L" - No device";
        }

        constexpr auto bufferSize = 255;
        wchar_t buffer[bufferSize];
        swprintf(buffer, bufferSize, APP_NAME L" - %ls [%d%%]",
                 Mic::Audio().CaptureDevice()->GetDeviceName(), _shownVolume.load());
        return buffer;
    }

    constexpr ActionDesc Actions[] = {
        // Toggle mute reports the state rather than the press - the indicator shows it too, but
        // it can be turned off, and then this is the only thing that says which way it went
        {.Id = "mic.toggle_mute",
         .Title = "Toggle mute",
         .Group = "Microphone",
         .Flags = ActionFlags::NoSound,
         .DefaultNotification = "Mic {mic}",
         // The device only reports a new mute from its callback, so the action publishes what it
         // just asked for - the same trick {volume} uses, or {mic} would report the state the
         // microphone was in before the key was pressed
         .Make = [](const ActionContext&) -> ActionFn {
             return [] {
                 Mic::Audio().CaptureDevice()->ToggleMute();
                 _shownMuted = !_shownMuted;
             };
         }},

        // Says nothing on purpose: on a held key the text would flicker with every tap
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
         // The config belongs to the UI thread, so the worker publishes what it asked for - the
         // same trick {volume} uses - and hands the write itself over to that thread
         .Make = [](const ActionContext&) -> ActionFn {
             return [] {
                 _shownBell = !_shownBell;
                 Dispatcher::ToUi(_toggleBell);
             };
         }},
    };

    constexpr const wchar_t* PillModes[] = {L"Hidden", L"Muted", L"Muted or talking"};

    constexpr SettingsRow PageRows[] = {
        // -1 means "never set", so the slider opens on whatever the device is actually at
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
         // Rounded, not truncated: 0.29f * 100 truncates to 28, and the sync would drag the slider back
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

        // The session watch stops while settings hold the config, and resumes before the frame
        // relays the overlay out - the pill's "anything recording" reads the sessions it keeps
        Lifecycle::Suspend += [] { Mic::Audio().StopWatchingForCaptureSessions(); };
        Lifecycle::Restore += [] {
            Mic::Audio().WatchForCaptureSessions();
            Mic::Refresh();
            // The state event that repaints the pill is posted, and the frame lays out before it lands
            MicLayer::Refresh();
        };
        _attachListeners();
    }

    AudioManager& Audio() {
        // Function-local so it is built inside Register, after the config and the feedback its
        // callbacks reach into. A namespace-scope one is constructed before main and torn down
        // after them, and a device event arriving in that window fires into freed memory.
        static AudioManager audio;
        return audio;
    }

    bool HasDevice() {
        return _hasDevice;
    }

    bool Muted() {
        return _muted;
    }

    void Refresh() {
        const auto mic = Mic::Audio().CaptureDevice();
        _hasDevice = mic->IsInitialized();
        _muted = mic->IsMuted();
        // No device is not "live" either, so {mic} reads off rather than claiming an open mic
        _shownMuted = !_hasDevice || _muted;
        _deviceVolume = mic->GetVolumeLevel();

        // Both read the config, so both go the same way _settle does - and posted first, because
        // the volume _settle publishes has to be the one _adjustVolume has already set
        Dispatcher::ToUi([] {
            _shownBell = _settings->BellVolume > 0;
            _adjustVolume();
        });
        _settle(true);
    }

}
