#ifndef EASYMIC_VOLUMENOTIFICATION_HPP
#define EASYMIC_VOLUMENOTIFICATION_HPP

#include <endpointvolume.h>
#include <cstdio>

class VolumeNotification final : public IAudioEndpointVolumeCallback {

    LONG rc;
    ~VolumeNotification() = default;
    const std::function<void(BOOL,float)> OnMicStateChanged;

public:
    explicit VolumeNotification(const std::function<void(BOOL,float)>& onChanged) :
        rc(1),
        OnMicStateChanged(onChanged) {}

    ULONG STDMETHODCALLTYPE AddRef() override {
        return InterlockedIncrement(&rc);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        ULONG rc = InterlockedDecrement(&this->rc);
        if(rc == 0) {
            delete this;
        }
        return rc;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if(IID_IUnknown == riid) {
            AddRef();
            *ppv = static_cast<IUnknown*>(this);
            return S_OK;
        }
        else if(__uuidof(IAudioEndpointVolumeCallback) == riid) {
            AddRef();
            *ppv = static_cast<IAudioEndpointVolumeCallback*>(this);
            return S_OK;
        }
        else {
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
    }

    HRESULT OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA pNotify) override {
        OnMicStateChanged(pNotify->bMuted, pNotify->fMasterVolume);
#ifdef DEBUG_AUDIO
        printf("Mic muted state: %i\n", pNotify->bMuted);
#endif
        return S_OK;
    }
};

#endif //EASYMIC_VOLUMENOTIFICATION_HPP
