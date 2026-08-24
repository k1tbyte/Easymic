//
// Created by kitbyte on 24.10.2025.
//

#ifndef EASYMIC_AUDIOMANAGER_HPP
#define EASYMIC_AUDIOMANAGER_HPP


#include <future>
#include <mutex>
#include "EventHandlers/AudioDeviceEventsHandler.hpp"
#include "AudioDeviceController.hpp"

class AudioManager {

    /// Capture and playback differ only by data flow - everything else is the same machinery.
    struct Endpoint {
        explicit Endpoint(const EDataFlow dataFlow) : flow(dataFlow) {}

        EDataFlow flow;
        std::shared_ptr<AudioDeviceController> device = std::make_shared<AudioDeviceController>();
        Event<> defaultChanged;
        Event<bool, float> stateChanged;
        Event<ComPtr<IAudioSessionControl>, EAudioSessionProperty> sessionPropertyChanged;
        std::atomic<bool> reinitPending = false;
        std::atomic<bool> watching = false;
        std::future<void> reinitTask;
    };

    Endpoint _capture{eCapture};
    Endpoint _playback{eRender};

    ComPtr<IMMDeviceEnumerator> deviceEnumerator;
    ComPtr<AudioDeviceEventsHandler> deviceHandler;

    mutable std::mutex _deviceMutex;

public:

    IEvent<>& OnDefaultCaptureChanged = _capture.defaultChanged;
    IEvent<>& OnDefaultPlaybackChanged = _playback.defaultChanged;
    IEvent<bool, float>& OnCaptureStateChanged = _capture.stateChanged;
    IEvent<bool, float>& OnPlaybackStateChanged = _playback.stateChanged;
    IEvent<ComPtr<IAudioSessionControl>, EAudioSessionProperty>& OnCaptureSessionPropertyChanged = _capture.sessionPropertyChanged;
    IEvent<ComPtr<IAudioSessionControl>, EAudioSessionProperty>& OnPlaybackSessionPropertyChanged = _playback.sessionPropertyChanged;

    /// False when COM refused to hand out the enumerator - the app then runs without audio control.
    bool Init() {
        if (deviceEnumerator) {
            return true;
        }

        auto result = CoCreateInstance(
           __uuidof(MMDeviceEnumerator), nullptr,
           CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),
            &deviceEnumerator
        );
        CHECK_HR(result, "Failed to create IMMDeviceEnumerator instance");

        // ReSharper disable once CppDFAMemoryLeak (COM is self-destructing if Release Ref Count == 0)
        deviceHandler = new AudioDeviceEventsHandler();
        deviceHandler->DefaultDeviceChanged = _handleDeviceChanged;
        result = deviceEnumerator->RegisterEndpointNotificationCallback(deviceHandler.Get());
        CHECK_HR(result, "Failed to register endpoint notification callback");

        _initEndpoint(_capture);
        _initEndpoint(_playback);
        return true;
    }

    void Cleanup() {
        if (deviceEnumerator && deviceHandler) {
            deviceEnumerator->UnregisterEndpointNotificationCallback(deviceHandler.Get());
        }

        deviceHandler.Reset();
        deviceEnumerator.Reset();
    }

    void WatchForCaptureSessions() { _setWatching(_capture, true); }
    void WatchForPlaybackSessions() { _setWatching(_playback, true); }
    void StopWatchingForCaptureSessions() { _setWatching(_capture, false); }
    void StopWatchingForPlaybackSessions() { _setWatching(_playback, false); }

    bool IsInitialized() const {
        return deviceEnumerator != nullptr;
    }

    std::shared_ptr<AudioDeviceController> CaptureDevice() const {
        std::lock_guard lock(_deviceMutex);
        return _capture.device;
    }

    std::shared_ptr<AudioDeviceController> PlaybackDevice() const {
        std::lock_guard lock(_deviceMutex);
        return _playback.device;
    }

    ~AudioManager() {
        for (Endpoint* endpoint : {&_capture, &_playback}) {
            if (endpoint->reinitTask.valid()) {
                endpoint->reinitTask.wait();
            }
        }
        Cleanup();
    }

private:

    void _initEndpoint(Endpoint& endpoint) {
        auto newDevice = std::make_shared<AudioDeviceController>();
        newDevice->OnDeviceStateChanged     = &endpoint.stateChanged;
        newDevice->OnSessionPropertyChanged = &endpoint.sessionPropertyChanged;
        newDevice->Init(deviceEnumerator, endpoint.flow, ERole::eCommunications);

        if (endpoint.watching) {
            newDevice->WatchForSessions();
        }

        std::lock_guard lock(_deviceMutex);
        endpoint.device = std::move(newDevice);
    }

    void _setWatching(Endpoint& endpoint, const bool watching) {
        endpoint.watching = watching;
        std::lock_guard lock(_deviceMutex);

        if (!endpoint.device) {
            return;
        }

        watching ? endpoint.device->WatchForSessions() : endpoint.device->StopWatchingForSessions();
    }

    const std::function<void(EDataFlow, ERole, LPCWSTR)> _handleDeviceChanged = [this](EDataFlow flow, ERole role, LPCWSTR) {
        if (role != ERole::eCommunications) {
            return;
        }

        Endpoint* endpoint = flow == eCapture ? &_capture : flow == eRender ? &_playback : nullptr;
        if (!endpoint || endpoint->reinitPending.exchange(true)) {
            return;
        }

        endpoint->reinitTask = std::async(std::launch::async, [this, endpoint] {
            // Windows announces the new default before it can actually be activated
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            _initEndpoint(*endpoint);
            endpoint->defaultChanged();
            endpoint->reinitPending = false;
        });
    };

};

#endif //EASYMIC_AUDIOMANAGER_HPP
