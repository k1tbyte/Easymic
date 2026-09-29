#pragma once

#include <audiopolicy.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>

#include <atomic>
#include <functional>

#include "Event.hpp"
#include "definitions.h"

/**
 * @brief IUnknown for a WASAPI callback object, which implements exactly one interface.
 *
 * Derives from it so the IUnknown methods land in its vtable. Lifetime is the refcount: always
 * heap allocated, never deleted by hand.
 */
template <typename Interface>
class ComObject : public Interface {
    LONG _refCount = 1;

public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(Interface)) {
            AddRef();
            *ppv = static_cast<Interface*>(this);
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return InterlockedIncrement(&_refCount);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = InterlockedDecrement(&_refCount);
        if (remaining == 0) {
            delete this;
        }
        return remaining;
    }

protected:
    /// Virtual so the delete above runs the handler's destructor.
    virtual ~ComObject() = default;
};

/// The default endpoint changed; the rest of the notifications are noise.
class DefaultDeviceCallback final : public ComObject<IMMNotificationClient> {
public:
    std::function<void(EDataFlow flow, ERole role)> Changed;

    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(const EDataFlow flow, const ERole role, LPCWSTR) override {
        if (Changed) {
            Changed(flow, role);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }
};

/**
 * @brief Mute and level as the endpoint reports them.
 *
 * Keeps them itself, so the callback touches nothing but this object: locking the controller from
 * here could leave the callback holding its last reference.
 */
class VolumeCallback final : public ComObject<IAudioEndpointVolumeCallback> {
public:
    std::atomic<bool> Muted = false;
    std::atomic<float> Level = -1.0f;
    Event<bool, float>* Changed = nullptr;

    HRESULT STDMETHODCALLTYPE OnNotify(const PAUDIO_VOLUME_NOTIFICATION_DATA data) override {
        Muted = data->bMuted;
        Level = data->fMasterVolume;
        if (Changed) {
            (*Changed)(data->bMuted, data->fMasterVolume);
        }
        return S_OK;
    }
};

class SessionCreatedCallback final : public ComObject<IAudioSessionNotification> {
    const std::function<void(IAudioSessionControl*)> _created;

public:
    explicit SessionCreatedCallback(std::function<void(IAudioSessionControl*)> created)
        : _created(std::move(created)) {}

    HRESULT STDMETHODCALLTYPE OnSessionCreated(IAudioSessionControl* session) override {
        _created(session);
        return S_OK;
    }
};

/// Tells when a session starts or stops capturing, or leaves (`gone`).
class SessionStateCallback final : public ComObject<IAudioSessionEvents> {
    const std::function<void(IAudioSessionControl*, bool gone)> _changed;
    ComPtr<IAudioSessionControl> _session;

public:
    SessionStateCallback(IAudioSessionControl* session, std::function<void(IAudioSessionControl*, bool gone)> changed)
        : _changed(std::move(changed)), _session(session) {}

    HRESULT STDMETHODCALLTYPE OnDisplayNameChanged(LPCWSTR, LPCGUID) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnIconPathChanged(LPCWSTR, LPCGUID) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnSimpleVolumeChanged(float, BOOL, LPCGUID) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnChannelVolumeChanged(DWORD, float[], DWORD, LPCGUID) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnGroupingParamChanged(LPCGUID, LPCGUID) override { return S_OK; }

    HRESULT STDMETHODCALLTYPE OnStateChanged(AudioSessionState) override {
        _changed(_session.Get(), false);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnSessionDisconnected(AudioSessionDisconnectReason) override {
        _changed(_session.Get(), true);
        return S_OK;
    }
};
