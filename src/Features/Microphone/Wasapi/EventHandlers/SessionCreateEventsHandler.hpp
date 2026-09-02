//
// Created by kitbyte on 25.10.2025.
//

#pragma once
#include <audiopolicy.h>

#include "ComObject.hpp"

class SessionCreateEventsHandler final : public ComObject<IAudioSessionNotification> {
    std::function<void(IAudioSessionControl*)> OnSessionRegistered;

public:
    explicit SessionCreateEventsHandler(const std::function<void(IAudioSessionControl*)>& onRegistered)
        : OnSessionRegistered(onRegistered) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == __uuidof(IAudioSessionNotification)) {
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

