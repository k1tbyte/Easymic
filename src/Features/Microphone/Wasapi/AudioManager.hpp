#pragma once

#include <mutex>
#include <utility>

#include "AudioDeviceController.hpp"

class AudioManager {

    std::shared_ptr<AudioDeviceController> _device = std::make_shared<AudioDeviceController>();
    Event<> _defaultChanged;
    Event<bool> _stateChanged;
    Event<> _sessionsChanged;
    std::atomic<bool> _reinitPending = false;
    /// Orders a device swap against the watch turning on or off.
    std::mutex _watchMutex;
    bool _watching = false;
    PTP_WORK _reinit = CreateThreadpoolWork([](PTP_CALLBACK_INSTANCE, void* context, PTP_WORK) {
        auto* self = static_cast<AudioManager*>(context);
        // Windows announces the new default before it can actually be activated
        Sleep(100);
        // Cleared first: a default that changes during the swap must queue another one
        self->_reinitPending = false;
        self->_initDevice();
        self->_defaultChanged();
    }, this, nullptr);

    ComPtr<IMMDeviceEnumerator> _deviceEnumerator;
    ComPtr<DefaultDeviceCallback> _deviceHandler;

    mutable std::mutex _deviceMutex;

public:

    IEvent<>& OnDefaultCaptureChanged = _defaultChanged;
    IEvent<bool>& OnCaptureStateChanged = _stateChanged;
    IEvent<>& OnCaptureSessionsChanged = _sessionsChanged;

    bool Init() {
        if (_deviceEnumerator) {
            return true;
        }

        auto result = CoCreateInstance(
           __uuidof(MMDeviceEnumerator), nullptr,
           CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),
            &_deviceEnumerator
        );
        CHECK_HR(result, "Failed to create IMMDeviceEnumerator instance");

        _deviceHandler.Attach(new DefaultDeviceCallback());
        _deviceHandler->Changed = _handleDeviceChanged;
        result = _deviceEnumerator->RegisterEndpointNotificationCallback(_deviceHandler.Get());
        CHECK_HR(result, "Failed to register endpoint notification callback");

        _initDevice();
        return true;
    }

    void WatchForCaptureSessions() { _setWatching(true); }
    void StopWatchingForCaptureSessions() { _setWatching(false); }

    std::shared_ptr<AudioDeviceController> CaptureDevice() const {
        std::lock_guard lock(_deviceMutex);
        return _device;
    }

    ~AudioManager() {
        if (_deviceEnumerator && _deviceHandler) {
            _deviceEnumerator->UnregisterEndpointNotificationCallback(_deviceHandler.Get());
        }
        if (_reinit) {
            WaitForThreadpoolWorkCallbacks(_reinit, FALSE);
            CloseThreadpoolWork(_reinit);
        }
        _device->Detach();
    }

private:

    void _initDevice() {
        auto newDevice = std::make_shared<AudioDeviceController>();
        newDevice->OnDeviceStateChanged     = &_stateChanged;
        newDevice->OnSessionsChanged        = &_sessionsChanged;
        newDevice->Init(_deviceEnumerator, eCapture, ERole::eCommunications);

        std::shared_ptr<AudioDeviceController> old;
        {
            std::lock_guard watch(_watchMutex);
            if (_watching) {
                newDevice->WatchForSessions();
            }
            std::lock_guard lock(_deviceMutex);
            old = std::exchange(_device, std::move(newDevice));
        }
        old->Detach();
    }

    void _setWatching(const bool watching) {
        std::lock_guard watch(_watchMutex);
        _watching = watching;
        // Not under _deviceMutex: a session job takes it under the session lock
        const auto device = CaptureDevice();
        watching ? device->WatchForSessions() : device->StopWatchingForSessions();
    }

    const std::function<void(EDataFlow, ERole)> _handleDeviceChanged = [this](const EDataFlow flow, const ERole role) {
        if (role == ERole::eCommunications && flow == eCapture && _reinit && !_reinitPending.exchange(true)) {
            SubmitThreadpoolWork(_reinit);
        }
    };

};

