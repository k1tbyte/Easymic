#ifndef EASYMIC_SESSIONEVENTS_HPP
#define EASYMIC_SESSIONEVENTS_HPP

#include <windows.h>
#include <audiopolicy.h>
#include <cstdio>
#include <functional>

class SessionEvents final : public IAudioSessionEvents {

private:
    LONG rc;
    ~SessionEvents() = default;

public:
    const int id{};
    const std::function<void(int)> OnDisconnectedEvent;
    const std::function<void(int, AudioSessionState)> OnStateChangedEvent;

    SessionEvents(int id, const std::function<void(int)>& onDisconnected,
                  const std::function<void(int, AudioSessionState)>& onStateChanged)
                    : id(id), rc(1), OnDisconnectedEvent(onDisconnected), OnStateChangedEvent(onStateChanged) {

    }

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
        else if(__uuidof(IAudioSessionEvents) == riid) {
            AddRef();
            *ppv = static_cast<IAudioSessionEvents*>(this);
            return S_OK;
        }
        else {
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
    }

    HRESULT STDMETHODCALLTYPE OnDisplayNameChanged(LPCWSTR NewDisplayName, LPCGUID EventContext) override {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnIconPathChanged(LPCWSTR NewIconPath, LPCGUID EventContext) override {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnSimpleVolumeChanged(float NewVolume, BOOL NewMute, LPCGUID EventContext) override {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnChannelVolumeChanged(DWORD ChannelCount,
                                                     float NewChannelVolumeArray[],
                                                     DWORD ChangedChannel,
                                                     LPCGUID EventContext) override {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnGroupingParamChanged(LPCGUID NewGroupingParam, LPCGUID EventContext) override {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnStateChanged(AudioSessionState newState) override {
#ifdef DEBUG_AUDIO
        switch(newState) {
            case AudioSessionState::AudioSessionStateActive:
                printf("Session active, id: %i\n", id);
                break;
            case AudioSessionState::AudioSessionStateInactive:
                printf("Session inactive, id: %i\n", id);
                break;
            case AudioSessionState::AudioSessionStateExpired:
                printf("Session expired, id: %i\n", id);
                break;
        }
#endif
        OnStateChangedEvent(id,newState);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnSessionDisconnected(AudioSessionDisconnectReason DisconnectReason) override {
#ifdef DEBUG_AUDIO
        printf("Session disconnected, id: %i\n", id);
#endif
        OnDisconnectedEvent(id);
        return S_OK;
    }
};

#endif //EASYMIC_SESSIONEVENTS_HPP
