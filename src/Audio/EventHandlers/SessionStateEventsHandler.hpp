//
// Created by kitbyte on 25.10.2025.
//



#ifndef EASYMIC_SESSIONSTATEEVENTSHANDLER_HPP
#define EASYMIC_SESSIONSTATEEVENTSHANDLER_HPP
#include <cstdint>
#include <definitions.h>

enum EAudioSessionProperty : uint32_t {
    None            = 0,
    DisplayName     = 1 << 0,  // 0b00000001
    IconPath        = 1 << 1,  // 0b00000010
    SimpleVolume    = 1 << 2,  // 0b00000100
    ChannelVolume   = 1 << 3,  // 0b00001000
    GroupingParam   = 1 << 4,  // 0b00010000
    State           = 1 << 5,  // 0b00100000
    Disconnected    = 1 << 6,  // 0b01000000
    Connected       = 1 << 7,  // 0b10000000

    Dispatch = Disconnected | Connected,
    Volume = SimpleVolume | ChannelVolume,
    All = DisplayName | IconPath | SimpleVolume | ChannelVolume |
          GroupingParam | State | Disconnected | Connected,
};

class SessionStateEventsHandler final : public IAudioSessionEvents {
private:
    LONG rc;
    const std::function<void(ComPtr<IAudioSessionControl>, EAudioSessionProperty)> OnPropertyChanged;
    ComPtr<IAudioSessionControl> sessionControl;
    ~SessionStateEventsHandler() = default;

public:

    SessionStateEventsHandler(
        const ComPtr<IAudioSessionControl> &control,
        const std::function<void(ComPtr<IAudioSessionControl>, EAudioSessionProperty)>& onPropertyChanged):
            rc(1),
            OnPropertyChanged(onPropertyChanged),
            sessionControl(control) { }

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

        if(__uuidof(IAudioSessionEvents) == riid) {
            AddRef();
            *ppv = static_cast<IAudioSessionEvents*>(this);
            return S_OK;
        }

        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE OnDisplayNameChanged(LPCWSTR NewDisplayName, LPCGUID EventContext) override {
        OnPropertyChanged(sessionControl, EAudioSessionProperty::DisplayName);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnIconPathChanged(LPCWSTR NewIconPath, LPCGUID EventContext) override {
        OnPropertyChanged(sessionControl, EAudioSessionProperty::IconPath);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnSimpleVolumeChanged(float NewVolume, BOOL NewMute, LPCGUID EventContext) override {
        OnPropertyChanged(sessionControl, EAudioSessionProperty::SimpleVolume);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnChannelVolumeChanged(DWORD ChannelCount,
                                                     float NewChannelVolumeArray[],
                                                     DWORD ChangedChannel,
                                                     LPCGUID EventContext) override {
        OnPropertyChanged(sessionControl, EAudioSessionProperty::ChannelVolume);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnGroupingParamChanged(LPCGUID NewGroupingParam, LPCGUID EventContext) override {
        OnPropertyChanged(sessionControl, EAudioSessionProperty::GroupingParam);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnStateChanged(AudioSessionState newState) override {
        OnPropertyChanged(sessionControl, EAudioSessionProperty::State);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnSessionDisconnected(AudioSessionDisconnectReason DisconnectReason) override {
        OnPropertyChanged(sessionControl, EAudioSessionProperty::Disconnected);
        return S_OK;
    }
};

#endif //EASYMIC_SESSIONSTATEEVENTSHANDLER_HPP