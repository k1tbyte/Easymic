#ifndef EASYMIC_AUDIOMANAGER_HPP
#define EASYMIC_AUDIOMANAGER_HPP

#include <mmdeviceapi.h>
#include <stdexcept>
#include <endpointvolume.h>
#include <audiopolicy.h>
#include <windows.h>

// WASAPI manager
//
class AudioManager {

private:
    IMMDeviceEnumerator* deviceEnumerator = nullptr;

    IMMDevice* captureDevice = nullptr;
    IAudioEndpointVolume* micVolumeEndpoint = nullptr;

    IMMDevice* renderDevice = nullptr;
    IAudioSessionManager2* pSessionManager = nullptr;
    IAudioSessionControl* pSessionControl = nullptr;
    ISimpleAudioVolume* appVolume = nullptr;

public:

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

        // Getting OUT (render) device
        result = deviceEnumerator->GetDefaultAudioEndpoint(EDataFlow::eRender, ERole::eCommunications,
                                                           &renderDevice);
        if (result != S_OK) {
            return FALSE;
        }

        // Getting mic capture endpoint
        captureDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL,
                                   nullptr, (void**)&micVolumeEndpoint);

        // Getting render endpoint for app volume settings
        renderDevice->Activate(__uuidof(IAudioSessionManager2),
                               CLSCTX_ALL,
                               nullptr, (void**)&pSessionManager);
        pSessionManager->GetAudioSessionControl(nullptr, FALSE, &pSessionControl);
        pSessionControl->QueryInterface(IID_PPV_ARGS(&appVolume));

        deviceEnumerator->Release();
        captureDevice->Release();
        renderDevice->Release();
        pSessionControl->Release();
        pSessionManager->Release();

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

    BOOL IsMicMuted()
    {
        BOOL wasMuted;
        micVolumeEndpoint->GetMute(&wasMuted);
        return wasMuted;
    }

    void SetMicState(BOOL muted)
    {
        micVolumeEndpoint->SetMute(muted, nullptr);
    }

    ~AudioManager(){
        appVolume->Release();
        micVolumeEndpoint->Release();
    }
};

#endif //EASYMIC_AUDIOMANAGER_HPP
