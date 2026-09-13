#include "Microphone.hpp"

#include <algorithm>
#include <atomic>

#include "AppConfig.hpp"
#include "Wasapi/AudioManager.hpp"
#include "Core/ActionRegistry.hpp"
#include "Core/Dispatcher.hpp"
#include "Core/Feedback.hpp"
#include "Logger.hpp"
#include "MicLayer.hpp"
#include "SoundCatalog.hpp"

namespace {

    Event<> _stateChanged;

    MicSettings* _settings = nullptr;
    /// Only to persist a bell toggle - the module reads nothing outside its own section
    AppConfig* _config = nullptr;
    Feedback* _fb = nullptr;
    HINSTANCE _instance = nullptr;

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
        const int target = std::clamp(mic->GetVolumePercent() + delta, 0, 100);
        mic->SetVolumePercent(static_cast<BYTE>(target));
        _fb->PublishVolumePercent(static_cast<uint8_t>(target));
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
                _fb->PublishVolumePercent(Mic::Audio().CaptureDevice()->GetVolumePercent());
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
            _fb->PublishMicMuted(muted);
            _settle(silent);

            if (level != _deviceVolume) {
                _deviceVolume = level;
                // Posted after _settle so it keeps its old place in the order, and posted at all
                // because it reads the config
                Dispatcher::ToUi(_adjustVolume);
            }
        };

        Mic::Audio().OnCaptureSessionPropertyChanged += [](const ComPtr<IAudioSessionControl>&,
                                                           const EAudioSessionProperty property) {
            if (property == Disconnected || property == Connected || property == State) {
                Mic::Refresh();
            }
        };

        Mic::Audio().OnDefaultCaptureChanged += [] { Mic::Refresh(); };
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
                 _fb->PublishMicMuted(!_fb->MicMuted());
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
                 _fb->PublishMicMuted(false);
             };
         },
         .MakeRelease = [](const ActionContext&) -> ActionFn {
             return [] {
                 Mic::Audio().CaptureDevice()->SetMute(true);
                 _fb->PublishMicMuted(true);
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
                 _fb->PublishBellEnabled(!_fb->BellEnabled());
                 Dispatcher::ToUi([] { Mic::ToggleBell(); });
             };
         }},
    };

} // anonymous namespace

namespace Mic {

    IEvent<>& OnStateChanged = _stateChanged;

    void Register(Host& host) {
        _settings = &host.Config.Mic;
        _config = &host.Config;
        _fb = &host.Fb;
        _instance = host.Instance;

        if (!Mic::Audio().Init()) {
            LOG_ERROR("AudioManager failed to initialize - continuing without microphone control");
        }

        for (const auto& desc : Actions) {
            ActionRegistry::Add(desc);
        }

        MicLayer::Register(host.Instance, host.Config.Mic);
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
        _fb->PublishMicMuted(!_hasDevice || _muted);
        _deviceVolume = mic->GetVolumeLevel();

        // Both read the config, so both go the same way _settle does - and posted first, because
        // the volume _settle publishes has to be the one _adjustVolume has already set
        Dispatcher::ToUi([] {
            _fb->PublishBellEnabled(_settings->BellVolume > 0);
            _adjustVolume();
        });
        _settle(true);
    }

    void Suspend() {
        Mic::Audio().StopWatchingForCaptureSessions();
    }

    void Resume() {
        Mic::Audio().WatchForCaptureSessions();
    }

    HICON TrayIcon() {
        return MicLayer::TrayIcon();
    }

    void RefreshTheme() {
        MicLayer::RefreshTheme();
    }

    void ToggleBell() {
        if (_settings->BellVolume > 0) {
            _prevBellVolume = _settings->BellVolume;
            _settings->BellVolume = 0;
        } else {
            _settings->BellVolume = _prevBellVolume > 0 ? _prevBellVolume : 25;
        }
        _fb->PublishBellEnabled(_settings->BellVolume > 0);
        _config->Save();
    }
}
