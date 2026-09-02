#include "Microphone.hpp"

#include <algorithm>
#include <atomic>

#include "AppConfig.hpp"
#include "AudioManager.hpp"
#include "Core/ActionRegistry.hpp"
#include "Core/Dispatcher.hpp"
#include "Core/Feedback.hpp"
#include "Logger.hpp"
#include "SoundCatalog.hpp"

namespace {

    AudioManager _audio;
    Event<> _stateChanged;

    AppConfig* _cfg = nullptr;
    Feedback* _fb = nullptr;
    HINSTANCE _instance = nullptr;

    bool _hasDevice = false;
    /// Written from the WASAPI notification thread, read from the hotkey worker and the UI
    std::atomic<bool> _muted = false;
    float _deviceVolume = -1.0f;
    /// What the chime was set to before it was silenced, so toggling it back restores the level
    int8_t _prevBellVolume = 25;

    void _shiftVolume(const int delta) {
        // Keeps the controller alive for the call: the shared_ptr is returned by value, and the
        // device-change task can drop the last other reference at any moment
        const auto mic = _audio.CaptureDevice();
        const int target = std::clamp(mic->GetVolumePercent() + delta, 0, 100);
        mic->SetVolumePercent(static_cast<BYTE>(target));
        _fb->PublishVolumePercent(static_cast<uint8_t>(target));
    }

    /// The user asked for one level and expects the device to keep it across reconnects.
    void _adjustVolume() {
        if (_cfg->IsMicKeepVolume && _cfg->MicVolume != -1) {
            _audio.CaptureDevice()->SetVolumePercent(_cfg->MicVolume);
        }
    }

    /**
     * @brief Settles the state and tells everyone who draws it.
     *
     * @param silent true when the state did not actually flip. The chime fires for a mute from
     *        anywhere, not only from our own hotkey, so it is gated on a real change alone.
     */
    void _settle(const bool silent) {
        if (_hasDevice) {
            // The device has spoken, so whatever an action optimistically published is now stale
            _fb->PublishVolumePercent(_audio.CaptureDevice()->GetVolumePercent());
        }

        _stateChanged();

        if (!silent && _hasDevice && _cfg->BellVolume > 0) {
            SoundCatalog::Play(_instance,
                               _muted ? _cfg->MuteSoundSource : _cfg->UnmuteSoundSource,
                               static_cast<uint8_t>(_cfg->BellVolume));
        }
    }

    /// Subscribed once for the lifetime of the app - the module outlives every device.
    void _attachListeners() {
        _audio.OnCaptureStateChanged += [](const bool muted, const float level) {
            const bool silent = _muted == muted;
            _muted = muted;
            _fb->PublishMicMuted(muted);
            _settle(silent);

            if (level != _deviceVolume) {
                _deviceVolume = level;
                _adjustVolume();
            }
        };

        _audio.OnCaptureSessionPropertyChanged += [](const ComPtr<IAudioSessionControl>&,
                                                     const EAudioSessionProperty property) {
            if (property == Disconnected || property == Connected || property == State) {
                Mic::Refresh();
            }
        };

        _audio.OnDefaultCaptureChanged += [] { Mic::Refresh(); };
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
                 _audio.CaptureDevice()->ToggleMute();
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
                 _audio.CaptureDevice()->SetMute(false);
                 _fb->PublishMicMuted(false);
             };
         },
         .MakeRelease = [](const ActionContext&) -> ActionFn {
             return [] {
                 _audio.CaptureDevice()->SetMute(true);
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
        _cfg = &host.Config;
        _fb = &host.Fb;
        _instance = host.Instance;

        if (!_audio.Init()) {
            LOG_ERROR("AudioManager failed to initialize - continuing without microphone control");
        }

        for (const auto& desc : Actions) {
            ActionRegistry::Add(desc);
        }

        _attachListeners();
    }

    AudioManager& Audio() {
        return _audio;
    }

    bool HasDevice() {
        return _hasDevice;
    }

    bool Muted() {
        return _muted;
    }

    void Refresh() {
        const auto mic = _audio.CaptureDevice();
        _hasDevice = mic->IsInitialized();
        _muted = mic->IsMuted();
        // No device is not "live" either, so {mic} reads off rather than claiming an open mic
        _fb->PublishMicMuted(!_hasDevice || _muted);
        _deviceVolume = mic->GetVolumeLevel();
        _fb->PublishBellEnabled(_cfg->BellVolume > 0);

        _adjustVolume();
        _settle(true);
    }

    void ToggleBell() {
        if (_cfg->BellVolume > 0) {
            _prevBellVolume = _cfg->BellVolume;
            _cfg->BellVolume = 0;
        } else {
            _cfg->BellVolume = _prevBellVolume > 0 ? _prevBellVolume : 25;
        }
        _fb->PublishBellEnabled(_cfg->BellVolume > 0);
        _cfg->Save();
    }
}
