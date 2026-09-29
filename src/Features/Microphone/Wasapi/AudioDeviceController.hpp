#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>

#include "Callbacks.hpp"
#include "Event.hpp"

/// One capture endpoint: mute, level, peak and the sessions recording from it.
class AudioDeviceController : public std::enable_shared_from_this<AudioDeviceController> {
public:
    Event<bool, float>* OnDeviceStateChanged = nullptr;
    /// A session came, went, started or stopped capturing. Threadpool.
    Event<>* OnSessionsChanged = nullptr;

    ~AudioDeviceController();

    /// False when the endpoint is missing or a COM call failed - the controller then stays idle
    /// and the app runs on without that device.
    bool Init(const ComPtr<IMMDeviceEnumerator>& enumerator, EDataFlow dataFlow, ERole role);

    bool IsInitialized() const { return _isInitialized; }
    float GetPeak() const;
    /// 0-1; -1 with no device.
    float GetVolumeLevel() const { return _volume ? _volume->Level.load() : -1.0f; }
    /// 0-100; 0 with no device.
    BYTE GetVolumePercent() const;
    void SetVolumePercent(BYTE level) const;
    bool IsMuted() const { return _volume && _volume->Muted; }
    void SetMute(bool mute) const;
    void ToggleMute() const { SetMute(!IsMuted()); }
    [[nodiscard]] LPWSTR GetDeviceName() const { return _name.pwszVal; }

    /// Anything capturing from the device right now.
    bool AnySessionActive() const;
    void WatchForSessions();
    void StopWatchingForSessions();
    /// Replaced or orphaned: raises no manager event any more, though a holder may still use it.
    void Detach();

private:
    struct Watched {
        ComPtr<SessionStateCallback> Handler;
        bool Active = false;
    };

    static AudioSessionState _state(IAudioSessionControl* control);
    static void _later(std::weak_ptr<AudioDeviceController> weak, IAudioSessionControl* control, bool gone);
    void _update(IAudioSessionControl* control, bool gone);
    void _watch(IAudioSessionControl* control);

    ComPtr<IMMDevice> _device;
    ComPtr<IAudioEndpointVolume> _endpoint;
    ComPtr<IAudioMeterInformation> _meter;
    ComPtr<IAudioSessionManager2> _sessionManager;
    ComPtr<VolumeCallback> _volume;
    ComPtr<SessionCreatedCallback> _sessionCreated;
    PROPVARIANT _name{};
    bool _isInitialized = false;

    std::unordered_map<IAudioSessionControl*, Watched> _sessions;
    mutable std::mutex _sessionMutex;
};
