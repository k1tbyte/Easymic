//
// Created by kitbyte on 25.10.2025.
//

#ifndef EASYMICTESTING_SESSIONCREATEEVENTSHANDLER_H
#define EASYMICTESTING_SESSIONCREATEEVENTSHANDLER_H
#include <audiopolicy.h>

class SessionCreateEventsHandler final : public IAudioSessionNotification {

private:
    LONG rc;
    std::function<void(IAudioSessionControl*)> OnSessionRegistered;
    ~SessionCreateEventsHandler() = default;

public:
    explicit SessionCreateEventsHandler(const std::function<void(IAudioSessionControl*)>& onRegistered)
    : rc(1), OnSessionRegistered(onRegistered) {}

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

        if(__uuidof(IAudioSessionNotification) == riid) {
            AddRef();
            *ppv = static_cast<IAudioSessionNotification*>(this);
            return S_OK;
        }

        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    HRESULT OnSessionCreated(IAudioSessionControl *newSession) override {
        OnSessionRegistered(newSession);
        return S_OK;
    }
};

#endif //EASYMICTESTING_SESSIONCREATEEVENTSHANDLER_H