#ifndef EASYMIC_AUDIOMANAGER_HPP
#define EASYMIC_AUDIOMANAGER_HPP

#include <mmdeviceapi.h>
#include <stdexcept>
#include <endpointvolume.h>
#include <audiopolicy.h>
#include <windows.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>

// WASAPI manager
//
class AudioManager {

private:
    constexpr static const GUID IDeviceFriendlyName =
            { 0xa45c254e, 0xdf1c, 0x4efd,
              { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 }
            };
    PROPERTYKEY key{};

    IMMDeviceEnumerator* deviceEnumerator = nullptr;

    IMMDevice* captureDevice = nullptr;
    IAudioEndpointVolume* micVolumeEndpoint = nullptr;
    PROPVARIANT micProperties{};

    IMMDevice* renderDevice = nullptr;
    IAudioSessionManager2* pSessionManager = nullptr;
    IAudioSessionControl* pSessionControl = nullptr;
    ISimpleAudioVolume* appVolume = nullptr;

public:

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

    LPWSTR GetDefaultMicName() const
    {
        return micProperties.pwszVal;
    }

    ~AudioManager(){
        appVolume->Release();
        micVolumeEndpoint->Release();
    }
};

#endif //EASYMIC_AUDIOMANAGER_HPP
