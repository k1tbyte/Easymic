//
// Created by kitbyte on 24.10.2025.
//

#ifndef EASYMIC_AUDIOMANAGER_HPP
#define EASYMIC_AUDIOMANAGER_HPP


#include <future>
#include <mutex>
#include <typeinfo>
#include "EventHandlers/AudioDeviceEventsHandler.hpp"
#include "AudioDeviceController.hpp"

class AudioManager {

    std::shared_ptr<AudioDeviceController> _captureDevice = std::make_shared<AudioDeviceController>();
    std::shared_ptr<AudioDeviceController> _playbackDevice = std::make_shared<AudioDeviceController>();


    ComPtr<IMMDeviceEnumerator> deviceEnumerator;
    ComPtr<AudioDeviceEventsHandler> deviceHandler;


    Event<> _defaultCaptureChanged;
    Event<> _defaultPlaybackChanged;
    Event<bool, float> _captureStateChanged;
    Event<bool, float> _playbackStateChanged;
    Event<ComPtr<IAudioSessionControl>, EAudioSessionProperty> _captureSessionPropertyChanged;
    Event<ComPtr<IAudioSessionControl>, EAudioSessionProperty> _playbackSessionPropertyChanged;

    std::atomic<bool> _captureReinitPending = false;
    std::atomic<bool> _playbackReinitPending = false;
    std::future<void> _captureReinitTask;
    std::future<void> _playbackReinitTask;

    mutable std::mutex _deviceMutex;
    std::atomic<bool> _captureWatching = false;
    std::atomic<bool> _playbackWatching = false;

public:

     IEvent<>& OnDefaultCaptureChanged = _defaultCaptureChanged;
     IEvent<>& OnDefaultPlaybackChanged = _defaultPlaybackChanged;
     IEvent<bool, float>& OnCaptureStateChanged = _captureStateChanged;
     IEvent<bool, float>& OnPlaybackStateChanged = _playbackStateChanged;
     IEvent<ComPtr<IAudioSessionControl>, EAudioSessionProperty>& OnCaptureSessionPropertyChanged = _captureSessionPropertyChanged;
     IEvent<ComPtr<IAudioSessionControl>, EAudioSessionProperty>& OnPlaybackSessionPropertyChanged = _playbackSessionPropertyChanged;



    void Init() {
        if (deviceEnumerator) {
            return;
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

        _initCaptureDeviceController();
        _initPlaybackDeviceController();
    }

    void Cleanup() {
        if (deviceEnumerator && deviceHandler) {
            deviceEnumerator->UnregisterEndpointNotificationCallback(deviceHandler.Get());
        }

        deviceHandler.Reset();
        deviceEnumerator.Reset();
    }

    void WatchForCaptureSessions() {
        _captureWatching = true;
        std::lock_guard lock(_deviceMutex);
        if (_captureDevice) { _captureDevice->WatchForSessions(); }
    }

    void WatchForPlaybackSessions() {
        _playbackWatching = true;
        std::lock_guard lock(_deviceMutex);
        if (_playbackDevice) { _playbackDevice->WatchForSessions(); }
    }

    void StopWatchingForCaptureSessions() {
        _captureWatching = false;
        std::lock_guard lock(_deviceMutex);
        if (_captureDevice) { _captureDevice->StopWatchingForSessions(); }
    }

    void StopWatchingForPlaybackSessions() {
        _playbackWatching = false;
        std::lock_guard lock(_deviceMutex);
        if (_playbackDevice) { _playbackDevice->StopWatchingForSessions(); }
    }

    bool IsInitialized() const {
        return deviceEnumerator != nullptr;
    }

    std::shared_ptr<AudioDeviceController> CaptureDevice() const {
        std::lock_guard lock(_deviceMutex);
        return _captureDevice;
    }

    std::shared_ptr<AudioDeviceController> PlaybackDevice() const {
        std::lock_guard lock(_deviceMutex);
        return _playbackDevice;
    }


    ~AudioManager() {
        if (_captureReinitTask.valid()) {
            _captureReinitTask.wait();
        }
        if (_playbackReinitTask.valid()) {
            _playbackReinitTask.wait();
        }
        Cleanup();
    }

private:

    void _initCaptureDeviceController() {
        auto newDevice = std::make_shared<AudioDeviceController>();
        newDevice->OnDeviceStateChanged     = &this->_captureStateChanged;
        newDevice->OnSessionPropertyChanged = &this->_captureSessionPropertyChanged;
        newDevice->Init(deviceEnumerator, EDataFlow::eCapture, ERole::eCommunications);

        if (_captureWatching) {
            newDevice->WatchForSessions();
        }

        std::lock_guard lock(_deviceMutex);
        _captureDevice = std::move(newDevice);
    }

    void _initPlaybackDeviceController() {
        auto newDevice = std::make_shared<AudioDeviceController>();
        newDevice->OnDeviceStateChanged     = &this->_playbackStateChanged;
        newDevice->OnSessionPropertyChanged = &this->_playbackSessionPropertyChanged;
        newDevice->Init(deviceEnumerator, EDataFlow::eRender, ERole::eCommunications);

        if (_playbackWatching) {
            newDevice->WatchForSessions();
        }

        std::lock_guard lock(_deviceMutex);
        _playbackDevice = std::move(newDevice);
    }

    const std::function<void(EDataFlow, ERole, LPCWSTR)> _handleDeviceChanged = [this](EDataFlow flow, ERole role, LPCWSTR) {
        if (role != ERole::eCommunications) {
            return;
        }

        if (flow == EDataFlow::eCapture) {
            if (_captureReinitPending.exchange(true)) {
                return;
            }

            _captureReinitTask = std::async(std::launch::async, [this]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                try {
                    _initCaptureDeviceController();
                    _defaultCaptureChanged();
                } catch (const std::exception& e) {
                    LOG_ERROR("Capture device reinit failed: %s", e.what());
                }
                _captureReinitPending = false;
            });
        } else if (flow == EDataFlow::eRender) {
            if (_playbackReinitPending.exchange(true)) {
                return;
            }

            _playbackReinitTask = std::async(std::launch::async, [this]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                try {
                    _initPlaybackDeviceController();
                    _defaultPlaybackChanged();
                } catch (const std::exception& e) {
                    LOG_ERROR("Playback device reinit failed: %s", e.what());
                }
                _playbackReinitPending = false;
            });
        }
    };

};

#endif //EASYMIC_AUDIOMANAGER_HPP