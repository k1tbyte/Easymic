#pragma once


#include <mutex>
#include "EventHandlers/AudioDeviceEventsHandler.hpp"
#include "AudioDeviceController.hpp"

/**
 * @brief The capture endpoint and everything that can replace it under us.
 *
 * Only capture: the bell plays through waveOut, so a second WASAPI endpoint would cost five COM
 * activations at startup and answer nothing anyone asks.
 */
class AudioManager {

    std::shared_ptr<AudioDeviceController> _device = std::make_shared<AudioDeviceController>();
    Event<> _defaultChanged;
    Event<bool, float> _stateChanged;
    Event<> _sessionsChanged;
    std::atomic<bool> _reinitPending = false;
    /// Orders a device swap against the watch turning on or off.
    std::mutex _watchMutex;
    bool _watching = false;
    PTP_WORK _reinit = CreateThreadpoolWork([](PTP_CALLBACK_INSTANCE, void* context, PTP_WORK) {
        auto* self = static_cast<AudioManager*>(context);
        // Windows announces the new default before it can actually be activated
        Sleep(100);
        self->_initDevice();
        self->_defaultChanged();
        self->_reinitPending = false;
    }, this, nullptr);

    ComPtr<IMMDeviceEnumerator> _deviceEnumerator;
    ComPtr<AudioDeviceEventsHandler> _deviceHandler;

    mutable std::mutex _deviceMutex;

public:

    IEvent<>& OnDefaultCaptureChanged = _defaultChanged;
    IEvent<bool, float>& OnCaptureStateChanged = _stateChanged;
    IEvent<>& OnCaptureSessionsChanged = _sessionsChanged;

    /// False when COM refused to hand out the enumerator - the app then runs without audio control.
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

        // Attach, not assign: the handler is born with one reference and ComPtr would AddRef a
        // second one that nothing ever releases
        _deviceHandler.Attach(new AudioDeviceEventsHandler());
        _deviceHandler->DefaultDeviceChanged = _handleDeviceChanged;
        result = _deviceEnumerator->RegisterEndpointNotificationCallback(_deviceHandler.Get());
        CHECK_HR(result, "Failed to register endpoint notification callback");

        _initDevice();
        return true;
    }

    void WatchForCaptureSessions() { _setWatching(true); }
    void StopWatchingForCaptureSessions() { _setWatching(false); }

    /// By value: the device-change task can swap the controller out at any moment, so a caller
    /// that keeps the pointer alive is the only one holding a valid one.
    std::shared_ptr<AudioDeviceController> CaptureDevice() const {
        std::lock_guard lock(_deviceMutex);
        return _device;
    }

    ~AudioManager() {
        if (_deviceEnumerator && _deviceHandler) {
            _deviceEnumerator->UnregisterEndpointNotificationCallback(_deviceHandler.Get());
        }
        // A reinit in flight still reaches the enumerator
        if (_reinit) {
            WaitForThreadpoolWorkCallbacks(_reinit, FALSE);
            CloseThreadpoolWork(_reinit);
        }
        // A session job may outlive us holding the device: stopped, it no longer raises our events
        _device->StopWatchingForSessions();
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
        // Someone may still hold it, and its session jobs would go on raising our events
        old->StopWatchingForSessions();
    }

    void _setWatching(const bool watching) {
        std::lock_guard watch(_watchMutex);
        _watching = watching;
        // Not under _deviceMutex: a session job takes it under the session lock
        const auto device = CaptureDevice();
        watching ? device->WatchForSessions() : device->StopWatchingForSessions();
    }

    const std::function<void(EDataFlow, ERole, LPCWSTR)> _handleDeviceChanged = [this](EDataFlow flow, ERole role, LPCWSTR) {
        if (role == ERole::eCommunications && flow == eCapture && _reinit && !_reinitPending.exchange(true)) {
            SubmitThreadpoolWork(_reinit);
        }
    };

};

