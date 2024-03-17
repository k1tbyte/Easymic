#ifndef EASYMIC_SESSIONNOTIFICATION_H
#define EASYMIC_SESSIONNOTIFICATION_H

#include <windows.h>
#include <audiopolicy.h>
#include <functional>
#include "SessionEvents.h"

struct AudioSession final {
    SessionEvents* eventHandler;
    IAudioSessionControl* audioControl;
};

class SessionNotification : public IAudioSessionNotification {

private:
    LONG rc;
    std::function<void(IAudioSessionControl*)> OnSessionRegistered;
    ~SessionNotification() {}

public:
    SessionNotification(const std::function<void(IAudioSessionControl*)>& onRegistered) : rc(1) {
        OnSessionRegistered = onRegistered;
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
        else if(__uuidof(IAudioSessionNotification) == riid) {
            AddRef();
            *ppv = static_cast<IAudioSessionNotification*>(this);
            return S_OK;
        }
        else {
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
    }

    HRESULT OnSessionCreated(IAudioSessionControl *newSession) override {
        if(newSession) {
            newSession->AddRef();
#ifdef DEBUG_AUDIO
            printf("New session created\n");
#endif
            OnSessionRegistered(newSession);
        }
        return S_OK;
    }
};

#endif //EASYMIC_SESSIONNOTIFICATION_H