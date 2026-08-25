#ifndef EASYMIC_VOLUMENOTIFICATION_HPP
#define EASYMIC_VOLUMENOTIFICATION_HPP
#include <audiopolicy.h>
#include <endpointvolume.h>

#include "ComObject.hpp"

class VolumeEventsHandler final : public ComObject<IAudioEndpointVolumeCallback> {
public:
    std::function<void(PAUDIO_VOLUME_NOTIFICATION_DATA)> OnChange;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == __uuidof(IAudioEndpointVolumeCallback)) {
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
