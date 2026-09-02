//
// Created by kitbyte on 24.10.2025.
//

#pragma once


#include <future>
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
    Event<ComPtr<IAudioSessionControl>, EAudioSessionProperty> _sessionPropertyChanged;
    std::atomic<bool> _reinitPending = false;
    std::atomic<bool> _watching = false;
    std::future<void> _reinitTask;

    ComPtr<IMMDeviceEnumerator> _deviceEnumerator;
    ComPtr<AudioDeviceEventsHandler> _deviceHandler;

    mutable std::mutex _deviceMutex;

public:

    IEvent<>& OnDefaultCaptureChanged = _defaultChanged;
    IEvent<bool, float>& OnCaptureStateChanged = _stateChanged;
    IEvent<ComPtr<IAudioSessionControl>, EAudioSessionProperty>& OnCaptureSessionPropertyChanged = _sessionPropertyChanged;

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

    void Cleanup() {
        if (_deviceEnumerator && _deviceHandler) {
            _deviceEnumerator->UnregisterEndpointNotificationCallback(_deviceHandler.Get());
        }

        _deviceHandler.Reset();
        _deviceEnumerator.Reset();
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
        if (_reinitTask.valid()) {
            _reinitTask.wait();
        }
        Cleanup();
    }

private:

    void _initDevice() {
        auto newDevice = std::make_shared<AudioDeviceController>();
        newDevice->OnDeviceStateChanged     = &_stateChanged;
        newDevice->OnSessionPropertyChanged = &_sessionPropertyChanged;
        newDevice->Init(_deviceEnumerator, eCapture, ERole::eCommunications);

        if (_watching) {
            newDevice->WatchForSessions();
        }

        std::lock_guard lock(_deviceMutex);
        _device = std::move(newDevice);
    }

    void _setWatching(const bool watching) {
        _watching = watching;
        std::lock_guard lock(_deviceMutex);

        if (!_device) {
            return;
        }

        watching ? _device->WatchForSessions() : _device->StopWatchingForSessions();
    }

    const std::function<void(EDataFlow, ERole, LPCWSTR)> _handleDeviceChanged = [this](EDataFlow flow, ERole role, LPCWSTR) {
        if (role != ERole::eCommunications || flow != eCapture || _reinitPending.exchange(true)) {
            return;
        }

        _reinitTask = std::async(std::launch::async, [this] {
            // Windows announces the new default before it can actually be activated
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            _initDevice();
            _defaultChanged();
            _reinitPending = false;
        });
    };

};

