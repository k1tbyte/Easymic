//
// Created by kitbyte on 24.10.2025.
//

#ifndef EASYMICTESTING_AUDIOMANAGER_H
#define EASYMICTESTING_AUDIOMANAGER_H


#include <future>
#include <typeinfo>
#include "EventHandlers/AudioDeviceEventsHandler.hpp"
#include "AudioDeviceController.hpp"

class AudioManager {

    std::shared_ptr<AudioDeviceController> _captureDevice = std::make_shared<AudioDeviceController>();
    std::shared_ptr<AudioDeviceController> _renderDevice = std::make_shared<AudioDeviceController>();


    ComPtr<IMMDeviceEnumerator> deviceEnumerator;
    ComPtr<AudioDeviceEventsHandler> deviceHandler;


    Event<> _defaultCaptureChanged;
    Event<> _defaultRenderChanged;
    Event<bool, float> _captureStateChanged;
    Event<bool, float> _renderStateChanged;
    Event<ComPtr<IAudioSessionControl>, EAudioSessionProperty> _captureSessionPropertyChanged;
    Event<ComPtr<IAudioSessionControl>, EAudioSessionProperty> _renderSessionPropertyChanged;

    std::atomic<bool> _captureReinitPending = false;
    std::atomic<bool> _renderReinitPending = false;
    std::future<void> _captureReinitTask;
    std::future<void> _renderReinitTask;

    bool _captureWatching = false;
    bool _renderWatching  = false;

public:

     IEvent<>& OnDefaultCaptureChanged = _defaultCaptureChanged;
     IEvent<>& OnDefaultRenderChanged = _defaultRenderChanged;
     IEvent<bool, float>& OnCaptureStateChanged = _captureStateChanged;
     IEvent<bool, float>& OnRenderStateChanged = _renderStateChanged;
     IEvent<ComPtr<IAudioSessionControl>, EAudioSessionProperty>& OnCaptureSessionPropertyChanged = _captureSessionPropertyChanged;
     IEvent<ComPtr<IAudioSessionControl>, EAudioSessionProperty>& OnRenderSessionPropertyChanged = _renderSessionPropertyChanged;



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
        _initRenderDeviceController();
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

    void WatchForRenderSessions()  {
        if (_renderDevice) {
            _renderDevice->WatchForSessions();
        }
        _renderWatching = true;
    }

    void StopWatchingForCaptureSessions()  {
        if (_captureDevice) {
            _captureDevice->StopWatchingForSessions();
        }
        _captureWatching = false;
    }

    void StopWatchingForRenderSessions()  {
        if (_renderDevice) {
            _renderDevice->StopWatchingForSessions();
        }
        _renderWatching = false;
    }

    bool IsInitialized() const {
        return deviceEnumerator != nullptr;
    }

    std::shared_ptr<AudioDeviceController> CaptureDevice() const {
        return _captureDevice;
    }

    std::shared_ptr<AudioDeviceController> RenderDevice() const {
        return _renderDevice;
    }


    ~AudioManager() {
        if (_captureReinitTask.valid()) {
            _captureReinitTask.wait();
        }
        if (_renderReinitTask.valid()) {
            _renderReinitTask.wait();
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

    void _initRenderDeviceController() {
        _renderDevice                           = std::make_shared<AudioDeviceController>();
        _renderDevice->OnDeviceStateChanged     = &this->_renderStateChanged;
        _renderDevice->OnSessionPropertyChanged = &this->_renderSessionPropertyChanged;

        _renderDevice->Init(deviceEnumerator, EDataFlow::eRender, ERole::eCommunications);

        if (_renderWatching) {
            _renderDevice->WatchForSessions();
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
            if (_renderReinitPending.exchange(true)) {
                return;
            }

            _renderReinitTask = std::async(std::launch::async, [this]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                _initRenderDeviceController();
                _defaultRenderChanged();
                _renderReinitPending = false;
            });
        }
    };

};

#endif //EASYMICTESTING_AUDIOMANAGER_H