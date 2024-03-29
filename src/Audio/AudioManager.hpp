#ifndef EASYMIC_AUDIOMANAGER_HPP
#define EASYMIC_AUDIOMANAGER_HPP

#include <mmdeviceapi.h>
#include <stdexcept>
#include <endpointvolume.h>
#include <audiopolicy.h>
#include <windows.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>
#include <cmath>
#include "SessionNotification.hpp"
#include "VolumeNotification.hpp"

__CRT_UUID_DECL(IAudioMeterInformation, 0xC02216F6, 0x8C67, 0x4B5B, 0x9D, 0x00, 0xD0, 0x08, 0xE7, 0x3E, 0x00, 0x64)
MIDL_INTERFACE("C02216F6-8C67-4B5B-9D00-D008E73E0064")
IAudioMeterInformation : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE GetPeakValue(float *pfPeak) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMeteringChannelCount(UINT *pnChannelCount) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetChannelsPeakValues(UINT32 u32ChannelCount,float *afPeakValues) = 0;
    virtual HRESULT STDMETHODCALLTYPE QueryHardwareSupport(DWORD *pdwHardwareSupportMask) = 0;
};


// WASAPI manager
//
class AudioManager final {

private:
    constexpr static const GUID IDeviceFriendlyName =
            { 0xa45c254e, 0xdf1c, 0x4efd,
              { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 }
            };
    PROPERTYKEY key{};

    IMMDeviceEnumerator* deviceEnumerator = nullptr;

    IMMDevice* captureDevice = nullptr;
    IAudioEndpointVolume* micVolumeEndpoint = nullptr;
    IAudioMeterInformation* micMeter = nullptr;
    IAudioSessionManager2* micSessionManager = nullptr;
    SessionNotification* micSessionsNotifier = nullptr;
    IAudioSessionEnumerator* micSessions = nullptr;
    std::vector<AudioSession> audioSessionEvents{};
    PROPVARIANT micProperties{};

    IMMDevice* renderDevice = nullptr;
    IAudioSessionManager2* pSessionManager = nullptr;
    IAudioSessionControl* pSessionControl = nullptr;
    ISimpleAudioVolume* appVolume = nullptr;

    BOOL isMicMuted = false;
    float micVolumeLevel = .0f;

public:

    std::function<void()> OnMicStateChanged = nullptr;


    AudioManager()
    {
        key.pid = 14;
        key.fmtid = IDeviceFriendlyName;
    }

    BOOL Init()
    {
        HRESULT result = CoCreateInstance(
                __uuidof(MMDeviceEnumerator), nullptr,
                CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),
                (void**)&deviceEnumerator
        );

        if(result != S_OK) {
            throw std::runtime_error("Bad CoCreateInstance result");
        }

        // Getting IN (capture) device
        deviceEnumerator->GetDefaultAudioEndpoint(EDataFlow::eCapture, ERole::eCommunications,
                                                  &captureDevice);

        // Getting mic properties
        IPropertyStore* pProperties;
        captureDevice->OpenPropertyStore(STGM_READ, &pProperties);
        PropVariantInit(&micProperties);
        pProperties->GetValue(key,&micProperties);

        // Getting OUT (render) device
        result = deviceEnumerator->GetDefaultAudioEndpoint(EDataFlow::eRender, ERole::eCommunications,
                                                           &renderDevice);
        if (result != S_OK) {
            return FALSE;
        }

        // Getting mic capture endpoint
        captureDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL,
                                   nullptr, (void**)&micVolumeEndpoint);

        // Getting mic peak volume meter
        captureDevice->Activate(__uuidof(IAudioMeterInformation),CLSCTX_ALL,
                                nullptr, (void**)&micMeter);

        // Getting mic session manager
        captureDevice->Activate(__uuidof(IAudioSessionManager2),CLSCTX_ALL,
                                nullptr, (void**)&micSessionManager);

        // Getting render endpoint for app volume settings
        renderDevice->Activate(__uuidof(IAudioSessionManager2),
                               CLSCTX_ALL,
                               nullptr, (void**)&pSessionManager);

        pSessionManager->GetAudioSessionControl(nullptr, FALSE, &pSessionControl);
        pSessionControl->QueryInterface(IID_PPV_ARGS(&appVolume));

        micVolumeEndpoint->GetMute(&isMicMuted);
        micVolumeEndpoint->GetMasterVolumeLevelScalar(&micVolumeLevel);
        micVolumeEndpoint->RegisterControlChangeNotify(new VolumeNotification([this] (BOOL isMuted, float volume) {
            this->isMicMuted = isMuted;
            this->micVolumeLevel = volume;
            if(this->OnMicStateChanged) {
                this->OnMicStateChanged();
            }
        }));

        deviceEnumerator->Release();
        captureDevice->Release();
        renderDevice->Release();
        pSessionControl->Release();
        pSessionManager->Release();
        pProperties->Release();

        return TRUE;
    }

    /// From 0.0 to 1.0
    void SetAppVolume(float volume)
    {
        appVolume->SetMasterVolume(volume, nullptr);
    }

    /// From 0 to 100
    void SetAppVolume(BYTE volume)
    {
        this->SetAppVolume((float)volume/100);
    }

    [[nodiscard]] BOOL IsMicMuted() const
    {
        return isMicMuted;
    }

    void SetMicState(BOOL muted)
    {
        micVolumeEndpoint->SetMute(muted, nullptr);
    }

    [[nodiscard]] LPWSTR GetDefaultMicName() const
    {
        return micProperties.pwszVal;
    }

    [[nodiscard]] BYTE GetMicVolume() const
    {
        return std::ceil(micVolumeLevel * 100) ;
    }

    [[nodiscard]] float GetMicPeak() const
    {
        float peak;
        micMeter->GetPeakValue(&peak);
        return peak;
    }

    void SetMicVolume(BYTE level) const
    {
        micVolumeEndpoint->SetMasterVolumeLevelScalar((float)level/100, nullptr);
    }

    void StartMicSessionsWatcher(const std::function<void(int)>& onActiveCountChanged) {
        if(micSessionsNotifier) {
            return;
        }

        static const auto& disconnectedEvent = [this](int id){
            this->audioSessionEvents[id].audioControl = nullptr;
        };

        static const auto& stateChangedEvent = [this, onActiveCountChanged](int id, AudioSessionState state) {
            int activeSessions = this->GetActiveMicSessionsCount();
#ifdef DEBUG_AUDIO
            printf("Active sessions count: %i\n", activeSessions);
#endif
            onActiveCountChanged(activeSessions);
        };

        int sessionsCount{};
        micSessionsNotifier = new SessionNotification([this](IAudioSessionControl* control){
            const auto& eventHandler = new SessionEvents((int)audioSessionEvents.size(),disconnectedEvent, stateChangedEvent);
            control->RegisterAudioSessionNotification(eventHandler);
            this->audioSessionEvents.push_back(AudioSession { eventHandler, control });
        });

        micSessionManager->RegisterSessionNotification(micSessionsNotifier);
        micSessionManager->GetSessionEnumerator(&micSessions);
        micSessions->GetCount(&sessionsCount);

        for(int i = 0; i < sessionsCount; i++) {
            IAudioSessionControl* control;
            micSessions->GetSession(i, &control);
            const auto& eventHandler = new SessionEvents(i,disconnectedEvent, stateChangedEvent);
            control->RegisterAudioSessionNotification(eventHandler);
            audioSessionEvents.push_back(AudioSession { eventHandler, control });
        }

        int activeSessions = GetActiveMicSessionsCount();
        if(activeSessions > 0) {
            onActiveCountChanged(activeSessions);
        }
    }

    int GetActiveMicSessionsCount() {
        if(micSessions == nullptr){
            return -1;
        }

        int sessionsCount{};
        int activeSessions{};
        micSessions->Release();
        micSessionManager->GetSessionEnumerator(&micSessions);
        micSessions->GetCount(&sessionsCount);
        for(int i = 0; i < sessionsCount; i++) {
            IAudioSessionControl* control;
            AudioSessionState state;
            micSessions->GetSession(i, &control);
            control->GetState(&state);
            if(state == AudioSessionState::AudioSessionStateActive) {
                activeSessions++;
            }
        }
        return activeSessions;
    }

    void DisposeMicSessionsWatcher() {
        if(micSessionsNotifier) {
            micSessionManager->UnregisterSessionNotification(micSessionsNotifier);
            micSessionsNotifier = nullptr;
        }

        if(micSessions) {
            micSessions->Release();
            micSessions = nullptr;
        }

        for(const auto& session : audioSessionEvents) {
            if(session.audioControl) {
                session.audioControl->UnregisterAudioSessionNotification(session.eventHandler);
            }
        }

        audioSessionEvents.clear();
    }

    ~AudioManager(){
        appVolume->Release();
        micVolumeEndpoint->Release();
        micMeter->Release();
        DisposeMicSessionsWatcher();
    }
};


#endif //EASYMIC_AUDIOMANAGER_HPP
