#ifndef EASYMIC_VOLUMENOTIFICATION_HPP
#define EASYMIC_VOLUMENOTIFICATION_HPP
#include <audiopolicy.h>
#include <endpointvolume.h>

class VolumeEventsHandler final : public IAudioEndpointVolumeCallback {

    LONG rc;

public:

    std::function<void(PAUDIO_VOLUME_NOTIFICATION_DATA)> OnChange;

    ~VolumeEventsHandler() = default;

    explicit VolumeEventsHandler() :
        rc(1) {}

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

        if(__uuidof(IAudioEndpointVolumeCallback) == riid) {
            AddRef();
            *ppv = static_cast<IAudioEndpointVolumeCallback*>(this);
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    HRESULT OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA pNotify) override {
        if (OnChange) {
            OnChange(pNotify);
        }
        return S_OK;
    }
};

#endif //EASYMIC_VOLUMENOTIFICATION_HPP
