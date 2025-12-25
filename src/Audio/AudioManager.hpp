//
// Created by kitbyte on 24.10.2025.
//

#ifndef EASYMIC_AUDIOMANAGER_HPP
#define EASYMIC_AUDIOMANAGER_HPP


#include <future>
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

    bool _captureWatching = false;
    bool _playbackWatching  = false;

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

    void WatchForCaptureSessions()  {
        if (_captureDevice) {
            _captureDevice->WatchForSessions();
        }
        _captureWatching = true;
    }

    void WatchForPlaybackSessions()  {
        if (_playbackDevice) {
            _playbackDevice->WatchForSessions();
        }
        _playbackWatching = true;
    }

    void StopWatchingForCaptureSessions()  {
        if (_captureDevice) {
            _captureDevice->StopWatchingForSessions();
        }
        _captureWatching = false;
    }

    void StopWatchingForPlaybackSessions()  {
        if (_playbackDevice) {
            _playbackDevice->StopWatchingForSessions();
        }
        _playbackWatching = false;
    }

    bool IsInitialized() const {
        return deviceEnumerator != nullptr;
    }

    std::shared_ptr<AudioDeviceController> CaptureDevice() const {
        return _captureDevice;
    }

    std::shared_ptr<AudioDeviceController> PlaybackDevice() const {
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
        _captureDevice                           = std::make_shared<AudioDeviceController>();
        _captureDevice->OnDeviceStateChanged     = &this->_captureStateChanged;
        _captureDevice->OnSessionPropertyChanged = &this->_captureSessionPropertyChanged;
        _captureDevice->Init(deviceEnumerator, EDataFlow::eCapture, ERole::eCommunications);

        if (_captureWatching) {
            _captureDevice->WatchForSessions();
        }
    }

    void _initPlaybackDeviceController() {
        _playbackDevice                           = std::make_shared<AudioDeviceController>();
        _playbackDevice->OnDeviceStateChanged     = &this->_playbackStateChanged;
        _playbackDevice->OnSessionPropertyChanged = &this->_playbackSessionPropertyChanged;

        _playbackDevice->Init(deviceEnumerator, EDataFlow::eRender, ERole::eCommunications);

        if (_playbackWatching) {
            _playbackDevice->WatchForSessions();
        }
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
                _initCaptureDeviceController();
                _defaultCaptureChanged();
                _captureReinitPending = false;
            });
        } else if (flow == EDataFlow::eRender) {
            if (_playbackReinitPending.exchange(true)) {
                return;
            }

            _playbackReinitTask = std::async(std::launch::async, [this]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                _initPlaybackDeviceController();
                _defaultPlaybackChanged();
                _playbackReinitPending = false;
            });
        }
    };

};

#endif //EASYMIC_AUDIOMANAGER_HPP