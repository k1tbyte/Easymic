#ifndef AUDIO_DEVICE_NOTIFICATION_HPP
#define AUDIO_DEVICE_NOTIFICATION_HPP
#include <mmdeviceapi.h>
#include <functional>

class AudioDeviceEventsHandler : public IMMNotificationClient {
private:
    LONG m_refCount;

public:
    std::function<void(EDataFlow flow, ERole role, LPCWSTR pwstrDeviceId)> DefaultDeviceChanged;

    virtual ~AudioDeviceEventsHandler() = default;

    AudioDeviceEventsHandler() :
        m_refCount(1) {}

    ULONG STDMETHODCALLTYPE AddRef() override {
        return InterlockedIncrement(&m_refCount);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        ULONG ulRef = InterlockedDecrement(&m_refCount);
        if (ulRef == 0) {
            delete this;
        }
        return ulRef;
    }

    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDeviceId) override {
        if (DefaultDeviceChanged) {
            DefaultDeviceChanged(flow, role, pwstrDeviceId);
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR pwstrDeviceId) override {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR pwstrDeviceId) override {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR pwstrDeviceId,DWORD dwNewState) override {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR pwstrDeviceId,const PROPERTYKEY key) override {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (riid == IID_IUnknown || riid == __uuidof(IMMNotificationClient)) {
            *ppvObject = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }
};

#endif //AUDIO_DEVICE_NOTIFICATION_HPP