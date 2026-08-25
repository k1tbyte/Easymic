#ifndef AUDIO_DEVICE_NOTIFICATION_HPP
#define AUDIO_DEVICE_NOTIFICATION_HPP
#include <mmdeviceapi.h>
#include <functional>

#include "ComObject.hpp"

class AudioDeviceEventsHandler final : public ComObject<IMMNotificationClient> {
public:
    std::function<void(EDataFlow flow, ERole role, LPCWSTR pwstrDeviceId)> DefaultDeviceChanged;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (riid == IID_IUnknown || riid == __uuidof(IMMNotificationClient)) {
            AddRef();
            *ppvObject = static_cast<IMMNotificationClient*>(this);
            return S_OK;
        }

        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDeviceId) override {
        if (DefaultDeviceChanged) {
            DefaultDeviceChanged(flow, role, pwstrDeviceId);
        }
        return S_OK;
    }

    // The endpoint we care about is the default one, so the rest of the notifications are noise
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR pwstrDeviceId) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR pwstrDeviceId) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key) override { return S_OK; }
};

#endif //AUDIO_DEVICE_NOTIFICATION_HPP
