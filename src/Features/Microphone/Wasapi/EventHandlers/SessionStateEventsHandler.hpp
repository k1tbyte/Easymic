#pragma once
#include "definitions.h"

#include "ComObject.hpp"

/// Tells when a session starts or stops capturing, or leaves (`gone`).
class SessionStateEventsHandler final : public ComObject<IAudioSessionEvents> {
    const std::function<void(IAudioSessionControl *, bool gone)> OnChanged;
    ComPtr<IAudioSessionControl> sessionControl;

public:
    SessionStateEventsHandler(IAudioSessionControl *control,
                              const std::function<void(IAudioSessionControl *, bool gone)> &onChanged)
        : OnChanged(onChanged), sessionControl(control) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == __uuidof(IAudioSessionEvents)) {
            AddRef();
            *ppv = static_cast<IAudioSessionEvents*>(this);
            return S_OK;
        }

        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE OnDisplayNameChanged(LPCWSTR, LPCGUID) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnIconPathChanged(LPCWSTR, LPCGUID) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnSimpleVolumeChanged(float, BOOL, LPCGUID) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnChannelVolumeChanged(DWORD, float[], DWORD, LPCGUID) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnGroupingParamChanged(LPCGUID, LPCGUID) override { return S_OK; }

    HRESULT STDMETHODCALLTYPE OnStateChanged(AudioSessionState) override {
        OnChanged(sessionControl.Get(), false);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnSessionDisconnected(AudioSessionDisconnectReason) override {
        OnChanged(sessionControl.Get(), true);
        return S_OK;
    }
};
