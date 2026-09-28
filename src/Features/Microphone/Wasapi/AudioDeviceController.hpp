#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "EventHandlers/SessionCreateEventsHandler.hpp"
#include "EventHandlers/VolumeEventsHandler.hpp"
#include "EventHandlers/SessionStateEventsHandler.hpp"
#include "Event.hpp"

class AudioDeviceController : public std::enable_shared_from_this<AudioDeviceController> {
    /// PKEY_Device_FriendlyName, spelled out: the SDK header only declares it.
    static constexpr PROPERTYKEY FriendlyNameKey{{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

    ComPtr<IMMDevice> device;
    ComPtr<IPropertyStore> propertyStore;
    ComPtr<IAudioEndpointVolume> volumeEndpoint;
    ComPtr<IAudioMeterInformation> meterInformation;
    ComPtr<IAudioSessionManager2> sessionManager;
    ComPtr<VolumeEventsHandler> volumeEventsHandler;
    ComPtr<SessionCreateEventsHandler> sessionCreateHandler;
    PROPVARIANT deviceNameProp{};

    struct Watched {
        ComPtr<SessionStateEventsHandler> Handler;
        bool Active = false;
    };
    std::unordered_map<IAudioSessionControl *, Watched> audioSessions;

    mutable std::mutex audioSessionMutex;
    bool _isInitialized = false;
    // Written by the WASAPI callback, read by actions on other threads
    std::atomic<bool> _isMuted = false;
    std::atomic<float> _volumeLevel = -1.0f;

public:
    Event<bool, float> *OnDeviceStateChanged;
    /// A session came, went, started or stopped capturing. Threadpool.
    Event<> *OnSessionsChanged;

    /// False when the endpoint is missing or a COM call failed - the controller then stays idle
    /// and the app runs on without that device.
    bool Init(const ComPtr<IMMDeviceEnumerator> &enumerator, const EDataFlow dataFlow, const ERole role) {
        if (!enumerator || _isInitialized) {
            return false;
        }

        auto result = enumerator->GetDefaultAudioEndpoint(dataFlow, role, &device);

        // Any device not found
        if (result != S_OK) {
            return false;
        }

        result = device->OpenPropertyStore(STGM_READ, &propertyStore);

        CHECK_HR(result, "Failed to open property store for capture device");
        PropVariantInit(&deviceNameProp);

        result = propertyStore->GetValue(FriendlyNameKey, &deviceNameProp);
        CHECK_HR(result, "Failed to get device friendly name property");

        result = device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL,
                                  nullptr, &volumeEndpoint);
        CHECK_HR(result, "Failed to activate IAudioEndpointVolume for capture device");


        result = device->Activate(__uuidof(IAudioMeterInformation),CLSCTX_ALL,
                                  nullptr, &meterInformation);
        CHECK_HR(result, "Failed to activate IAudioMeterInformation for capture device");

        result = device->Activate(__uuidof(IAudioSessionManager2),CLSCTX_ALL,
                                  nullptr, &sessionManager);
        CHECK_HR(result, "Failed to activate IAudioSessionManager2 for capture device");

        // Attach, not assign: the handler is born with one reference and ComPtr would AddRef a
        // second one that nothing ever releases
        volumeEventsHandler.Attach(new VolumeEventsHandler());

        std::weak_ptr weak_this = shared_from_this();
        volumeEventsHandler->OnChange = [weak_this](PAUDIO_VOLUME_NOTIFICATION_DATA pNotify) {
            const auto _this = weak_this.lock();
            if (_this == nullptr) {
                return;
            }

            _this->_isMuted = pNotify->bMuted;
            _this->_volumeLevel = pNotify->fMasterVolume;

            if (_this->OnDeviceStateChanged) {
                (*_this->OnDeviceStateChanged)(pNotify->bMuted, pNotify->fMasterVolume);
            }
        };

        result = volumeEndpoint->RegisterControlChangeNotify(volumeEventsHandler.Get());
        CHECK_HR(result, "Failed to register volume change notify for capture device");

        BOOL muted = FALSE;
        float level = -1.0f;
        volumeEndpoint->GetMute(&muted);
        volumeEndpoint->GetMasterVolumeLevelScalar(&level);
        _isMuted = muted;
        _volumeLevel = level;

        _isInitialized = true;
        return true;
    }

    bool IsInitialized() const {
        return _isInitialized;
    }

    float GetPeak() const {
        float peak = 0.0f;
        if (meterInformation) {
            meterInformation->GetPeakValue(&peak);
        }
        return peak;
    }

    void SetVolumePercent(const BYTE level) const {
        if (volumeEndpoint) {
            volumeEndpoint->SetMasterVolumeLevelScalar(static_cast<float>(level) / 100, nullptr);
        }
    }

    float GetVolumeLevel() const {
        return _volumeLevel;
    }

    bool IsMuted() const {
        return _isMuted;
    }

    void ToggleMute() const {
        SetMute(!_isMuted);
    }

    /// Anything capturing from the device right now.
    bool AnySessionActive() const {
        std::lock_guard lock(audioSessionMutex);
        return std::ranges::any_of(audioSessions, [](const auto &session) { return session.second.Active; });
    }

    void SetMute(const bool mute) const {
        if (volumeEndpoint) {
            volumeEndpoint->SetMute(mute, nullptr);
        }
    }

    [[nodiscard]] LPWSTR GetDeviceName() const {
        return deviceNameProp.pwszVal;
    }

    BYTE GetVolumePercent() const {
        return std::ceil(_volumeLevel * 100);
    }

    void WatchForSessions() {
        std::lock_guard lock(audioSessionMutex);

        ComPtr<IAudioSessionEnumerator> sessions;
        int count = 0;
        if (sessionCreateHandler || !sessionManager || FAILED(sessionManager->GetSessionEnumerator(&sessions))
            || FAILED(sessions->GetCount(&count))) {
            return;
        }
        for (int i = 0; i < count; i++) {
            ComPtr<IAudioSessionControl> control;
            if (SUCCEEDED(sessions->GetSession(i, &control))) {
                _watch(control.Get());
            }
        }

        sessionCreateHandler.Attach(new SessionCreateEventsHandler(
            [weak = weak_from_this()](IAudioSessionControl *control) { _later(weak, control, false); }));
        sessionManager->RegisterSessionNotification(sessionCreateHandler.Get());
    }

    void StopWatchingForSessions() {
        std::lock_guard lock(audioSessionMutex);

        if (!sessionCreateHandler || !sessionManager) {
            return;
        }

        for (auto &[control, watched]: audioSessions) {
            control->UnregisterAudioSessionNotification(watched.Handler.Get());
        }
        audioSessions.clear();

        sessionManager->UnregisterSessionNotification(sessionCreateHandler.Get());
        sessionCreateHandler.Reset();
    }

    void Cleanup() {
        if (volumeEndpoint && volumeEventsHandler) {
            volumeEndpoint->UnregisterControlChangeNotify(volumeEventsHandler.Get());
        }

        StopWatchingForSessions();
        volumeEventsHandler.Reset();
        device.Reset();
        propertyStore.Reset();
        volumeEndpoint.Reset();
        meterInformation.Reset();
        sessionManager.Reset();
        PropVariantClear(&deviceNameProp);
    }

    ~AudioDeviceController() {
        Cleanup();
    }

private:
    static AudioSessionState _state(IAudioSessionControl *control) {
        AudioSessionState state = AudioSessionStateExpired;
        control->GetState(&state);
        return state;
    }

    /// Off the WASAPI callback, which may not register, unregister, wait or drop a last reference.
    static void _later(std::weak_ptr<AudioDeviceController> weak, IAudioSessionControl *control, const bool gone) {
        struct Job {
            std::weak_ptr<AudioDeviceController> Owner;
            ComPtr<IAudioSessionControl> Control;
            bool Gone;
        };
        auto *job = new Job{std::move(weak), control, gone};
        if (!TrySubmitThreadpoolCallback([](PTP_CALLBACK_INSTANCE, void *context) {
                const std::unique_ptr<Job> owned(static_cast<Job *>(context));
                if (const auto owner = owned->Owner.lock()) {
                    owner->_update(owned->Control.Get(), owned->Gone);
                }
            }, job, nullptr)) {
            delete job;
        }
    }

    /// Reads the state afresh, so jobs landing out of order still leave the last one. Raises under the
    /// lock: once the watch stops, no job reaches an owner being torn down.
    void _update(IAudioSessionControl *control, const bool gone) {
        std::lock_guard lock(audioSessionMutex);
        if (!sessionCreateHandler) {
            return;
        }
        const AudioSessionState state = _state(control);
        const auto it = audioSessions.find(control);
        if (gone || state == AudioSessionStateExpired) {
            if (it == audioSessions.end()) {
                return;
            }
            control->UnregisterAudioSessionNotification(it->second.Handler.Get());
            audioSessions.erase(it);
        } else if (it == audioSessions.end()) {
            _watch(control);
        } else {
            it->second.Active = state == AudioSessionStateActive;
        }
        if (OnSessionsChanged) {
            (*OnSessionsChanged)();
        }
    }

    /// Under the lock.
    void _watch(IAudioSessionControl *control) {
        ComPtr<SessionStateEventsHandler> handler;
        handler.Attach(new SessionStateEventsHandler(control,
            [weak = weak_from_this()](IAudioSessionControl *session, const bool gone) { _later(weak, session, gone); }));
        if (SUCCEEDED(control->RegisterAudioSessionNotification(handler.Get()))) {
            audioSessions[control] = {handler, _state(control) == AudioSessionStateActive};
        }
    }
};
