//
// Created by kitbyte on 24.10.2025.
//

#ifndef EASYMICTESTING_CAPTUREDEVICECONTROLLER_H
#define EASYMICTESTING_CAPTUREDEVICECONTROLLER_H


#include <atomic>
#include <cmath>
#include <memory>
#include <optional>

#include "EventHandlers/SessionCreateEventsHandler.hpp"
#include "EventHandlers/VolumeEventsHandler.hpp"
#include "EventHandlers/SessionStateEventsHandler.hpp"
#include "Event.hpp"

#define LOG_SESSION(message, ...) \
    printf("[AUDIO SESSION] (Registered: %zu | Active: %d) -> " message "\n", \
        audioSessions.size(), \
        _activeSessionsCount.load(), \
        ##__VA_ARGS__ \
    );

#ifdef __GNUC__
__CRT_UUID_DECL(IAudioMeterInformation, 0xC02216F6, 0x8C67, 0x4B5B, 0x9D, 0x00, 0xD0, 0x08, 0xE7, 0x3E, 0x00, 0x64)

MIDL_INTERFACE("C02216F6-8C67-4B5B-9D00-D008E73E0064")
IAudioMeterInformation : IUnknown
{
    virtual ~IAudioMeterInformation() = default;
    virtual HRESULT STDMETHODCALLTYPE GetPeakValue(float *pfPeak) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMeteringChannelCount(UINT *pnChannelCount) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetChannelsPeakValues(UINT32 u32ChannelCount,float *afPeakValues) = 0;
    virtual HRESULT STDMETHODCALLTYPE QueryHardwareSupport(DWORD *pdwHardwareSupportMask) = 0;
};
#endif


class AudioDeviceController : public std::enable_shared_from_this<AudioDeviceController> {

    constexpr static GUID IDeviceFriendlyName =
        { 0xa45c254e, 0xdf1c, 0x4efd,
          { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 }
        };

    ComPtr<IMMDevice> device;
    ComPtr<IPropertyStore> propertyStore;
    ComPtr<IAudioEndpointVolume> volumeEndpoint;
    ComPtr<IAudioMeterInformation> meterInformation;
    ComPtr<IAudioSessionManager2> sessionManager;
    ComPtr<VolumeEventsHandler> volumeEventsHandler;
    ComPtr<SessionCreateEventsHandler> sessionCreateHandler;
    ComPtr<ISimpleAudioVolume> simpleAudioVolume;
    PROPVARIANT deviceNameProp{};


    std::unordered_map<IAudioSessionControl*, ComPtr<SessionStateEventsHandler>> audioSessions;

    mutable std::mutex audioSessionMutex;
    std::atomic<int> _activeSessionsCount{0};
    bool _isInitialized = false;
    BOOL _isMuted = false;
    float _volumeLevel = -1.0f;

public:
    Event<bool, float>* OnDeviceStateChanged;
    Event<ComPtr<IAudioSessionControl>, EAudioSessionProperty>* OnSessionPropertyChanged;

     void Init(const ComPtr<IMMDeviceEnumerator> &enumerator, const EDataFlow dataFlow, const ERole role) {

        if (!enumerator || _isInitialized) {
            return;
        }

        auto result = enumerator->GetDefaultAudioEndpoint(dataFlow, role,&device);

        // Any device not found
        if (result != S_OK) {
            return;
        }

        PROPERTYKEY key{};
        key.pid = 14;
        key.fmtid = IDeviceFriendlyName;

        result = device->OpenPropertyStore(STGM_READ, &propertyStore);
        CHECK_HR(result, "Failed to open property store for capture device");

        PropVariantInit(&deviceNameProp);

        result = propertyStore->GetValue(key, &deviceNameProp);
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

        volumeEventsHandler = new VolumeEventsHandler();

         std::weak_ptr weak_this = shared_from_this();
        volumeEventsHandler->OnChange = [weak_this](PAUDIO_VOLUME_NOTIFICATION_DATA pNotify) {
            const auto _this = weak_this.lock();
            if (_this == nullptr) {
                return;
            }

            _this->_isMuted = pNotify->bMuted;
            _this->_volumeLevel = pNotify->fMasterVolume;

            if (_this->OnDeviceStateChanged) {
                (*_this->OnDeviceStateChanged)(_this->_isMuted, _this->_volumeLevel);
            }

        };

        result = volumeEndpoint->RegisterControlChangeNotify(volumeEventsHandler.Get());
        CHECK_HR(result, "Failed to register volume change notify for capture device");

        volumeEndpoint->GetMute(&_isMuted);
        volumeEndpoint->GetMasterVolumeLevelScalar(&_volumeLevel);


        _isInitialized = true;
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

    void SetVolumeLevel(const float level) const {
        if (volumeEndpoint) {
            volumeEndpoint->SetMasterVolumeLevelScalar(level, nullptr);
        }
    }

    void SetVolumePercent(const BYTE level) const {
        this->SetVolumeLevel(static_cast<float>(level) / 100);
    }

    float GetVolumeLevel() const {
        return _volumeLevel;
    }

    bool IsMuted() const {
        return _isMuted;
    }

    int GetActiveSessionsCount() const {
        return _activeSessionsCount.load();
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

    void IterateSessions(const std::function<void(ComPtr<IAudioSessionControl>, int i)>& iterator) const {

         int sessionsCount{};
         ComPtr<IAudioSessionEnumerator> enumerator;


         auto result = sessionManager->GetSessionEnumerator(&enumerator);
         CHECK_HR(result, "Failed to get audio session enumerator for capture device");

         result = enumerator->GetCount(&sessionsCount);
         CHECK_HR(result, "Failed to get audio sessions count for capture device");

         for(int i = 0; i < sessionsCount; i++) {
             ComPtr<IAudioSessionControl> control;
             enumerator->GetSession(i, &control);
             iterator(control, i);
         }
     }

    int CountSessions(std::optional<AudioSessionState> filter = std::nullopt) const {
         int count = 0;

         IterateSessions([&count, filter](const ComPtr<IAudioSessionControl> &control, int i){
            if (!filter.has_value() || GetSessionState(control) == filter) {
                count++;
            }
         });

         return count;
     }

    void WatchForSessions() {
         std::lock_guard lock(audioSessionMutex);

         if (sessionCreateHandler || !sessionManager) {
             return;
         }

         IterateSessions([this](const ComPtr<IAudioSessionControl> &control, int i){
            if (GetSessionState(control) == AudioSessionStateActive) {
                ++_activeSessionsCount;
            }
            this->_watchForSessionStateChanges(control);
         });

         const std::weak_ptr weak_this = shared_from_this();
         // capture 'this' for logging purposes, in release compiler removes it
         sessionCreateHandler = new SessionCreateEventsHandler([weak_this, this](IAudioSessionControl* control){
                const auto _this = weak_this.lock();
                if (!_this) {
                    return;
                }

                LOG_SESSION("New session {%p} created", control);
                _this->_watchForSessionStateChanges(control);
         });

         sessionManager->RegisterSessionNotification(sessionCreateHandler.Get());
     }

    void StopWatchingForSessions() {
         if (!sessionCreateHandler || sessionManager) {
             return;
         }

         for (auto& [control, handler] : audioSessions) {
             control->UnregisterAudioSessionNotification(handler.Get());
             handler.Reset();
         }

         sessionManager->UnregisterSessionNotification(sessionCreateHandler.Get());
         sessionCreateHandler.Reset();

         audioSessions.clear();
         _activeSessionsCount = 0;
     }

    static AudioSessionState GetSessionState(const ComPtr<IAudioSessionControl> &control) {
         AudioSessionState state = AudioSessionStateExpired;
         if (control) {
             control->GetState(&state);
         }
         return state;
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
    void _watchForSessionStateChanges(const ComPtr<IAudioSessionControl> &sessionControl) {

        if (audioSessions.contains(sessionControl.Get())) {
            return;
        }

        std::weak_ptr weak_this = shared_from_this();

        const ComPtr<SessionStateEventsHandler> eventHandler = new SessionStateEventsHandler(sessionControl,
        [weak_this, this](
            const ComPtr<IAudioSessionControl>& control,
            const EAudioSessionProperty property) {

            const auto _this = weak_this.lock();
            if (!_this) {
                return;
            }

            std::lock_guard lock_(_this->audioSessionMutex);

            if (!_this->audioSessions.contains(control.Get())) {
                return;
            }

            if (property == Disconnected ||
                (property == State && GetSessionState(control) == AudioSessionStateExpired)) {
                // We dont need do decrement _activeSessionsCount here, because it was already done on State change to Inactive
                // Windows raise Disconnect event after Inactive state
                _this->audioSessions.erase(control.Get());
                LOG_SESSION("Session {%p} disconnected", control.Get());
            }
            else if (property == State) {
                _this->_activeSessionsCount.fetch_add(GetSessionState(control) == AudioSessionStateActive ? 1 : -1);
                LOG_SESSION("Session {%p} state changed", control.Get());
            }

            if (_this->OnSessionPropertyChanged) {
                (*_this->OnSessionPropertyChanged)(control, property);
            }

        });

        sessionControl->RegisterAudioSessionNotification(eventHandler.Get());
        audioSessions[sessionControl.Get()] = eventHandler;
        LOG_SESSION("Started watching session {%p}", sessionControl.Get());
    }

};

#endif //EASYMICTESTING_CAPTUREDEVICECONTROLLER_H