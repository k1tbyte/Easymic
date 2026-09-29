#include "AudioDeviceController.hpp"

#include <algorithm>
#include <cmath>

namespace {
    // PKEY_Device_FriendlyName: the SDK header only declares it
    constexpr PROPERTYKEY FriendlyNameKey{{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};
}

bool AudioDeviceController::Init(const ComPtr<IMMDeviceEnumerator>& enumerator, const EDataFlow dataFlow,
                                 const ERole role) {
    if (!enumerator || _isInitialized) {
        return false;
    }
    if (enumerator->GetDefaultAudioEndpoint(dataFlow, role, &_device) != S_OK) {
        return false;
    }

    ComPtr<IPropertyStore> properties;
    auto result = _device->OpenPropertyStore(STGM_READ, &properties);
    CHECK_HR(result, "Failed to open property store for capture device");
    result = properties->GetValue(FriendlyNameKey, &_name);
    CHECK_HR(result, "Failed to get device friendly name property");

    result = _device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, &_endpoint);
    CHECK_HR(result, "Failed to activate IAudioEndpointVolume for capture device");
    result = _device->Activate(__uuidof(IAudioMeterInformation), CLSCTX_ALL, nullptr, &_meter);
    CHECK_HR(result, "Failed to activate IAudioMeterInformation for capture device");
    result = _device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, &_sessionManager);
    CHECK_HR(result, "Failed to activate IAudioSessionManager2 for capture device");

    // Attach: the handler is born with one reference, and assigning would leak a second
    _volume.Attach(new VolumeCallback());
    _volume->Changed = OnDeviceStateChanged;
    result = _endpoint->RegisterControlChangeNotify(_volume.Get());
    CHECK_HR(result, "Failed to register volume change notify for capture device");

    BOOL muted = FALSE;
    float level = -1.0f;
    _endpoint->GetMute(&muted);
    _endpoint->GetMasterVolumeLevelScalar(&level);
    _volume->Muted = muted;
    _volume->Level = level;

    _isInitialized = true;
    return true;
}

AudioDeviceController::~AudioDeviceController() {
    if (_endpoint && _volume) {
        _endpoint->UnregisterControlChangeNotify(_volume.Get());
    }
    StopWatchingForSessions();
    PropVariantClear(&_name);
}

float AudioDeviceController::GetPeak() const {
    float peak = 0.0f;
    if (_meter) {
        _meter->GetPeakValue(&peak);
    }
    return peak;
}

BYTE AudioDeviceController::GetVolumePercent() const {
    return static_cast<BYTE>(std::clamp(std::lround(GetVolumeLevel() * 100), 0L, 100L));
}

void AudioDeviceController::SetVolumePercent(const BYTE level) const {
    if (_endpoint) {
        _endpoint->SetMasterVolumeLevelScalar(static_cast<float>(level) / 100, nullptr);
    }
}

void AudioDeviceController::SetMute(const bool mute) const {
    if (_endpoint) {
        _endpoint->SetMute(mute, nullptr);
    }
}

bool AudioDeviceController::AnySessionActive() const {
    std::lock_guard lock(_sessionMutex);
    return std::ranges::any_of(_sessions, [](const auto& session) { return session.second.Active; });
}

void AudioDeviceController::WatchForSessions() {
    std::lock_guard lock(_sessionMutex);
    if (_sessionCreated || !_sessionManager) {
        return;
    }

    // Registered before the enumeration, so a session created in between is not missed
    _sessionCreated.Attach(new SessionCreatedCallback(
        [weak = weak_from_this()](IAudioSessionControl* control) { _later(weak, control, false); }));
    _sessionManager->RegisterSessionNotification(_sessionCreated.Get());

    ComPtr<IAudioSessionEnumerator> sessions;
    int count = 0;
    if (FAILED(_sessionManager->GetSessionEnumerator(&sessions)) || FAILED(sessions->GetCount(&count))) {
        return;
    }
    for (int i = 0; i < count; i++) {
        ComPtr<IAudioSessionControl> control;
        if (SUCCEEDED(sessions->GetSession(i, &control))) {
            _watch(control.Get());
        }
    }
}

void AudioDeviceController::Detach() {
    StopWatchingForSessions();
    if (_volume) {
        _volume->Changed = nullptr;
    }
}

void AudioDeviceController::StopWatchingForSessions() {
    std::lock_guard lock(_sessionMutex);

    if (!_sessionCreated || !_sessionManager) {
        return;
    }
    for (auto& [control, watched] : _sessions) {
        control->UnregisterAudioSessionNotification(watched.Handler.Get());
    }
    _sessions.clear();

    _sessionManager->UnregisterSessionNotification(_sessionCreated.Get());
    _sessionCreated.Reset();
}

AudioSessionState AudioDeviceController::_state(IAudioSessionControl* control) {
    AudioSessionState state = AudioSessionStateExpired;
    control->GetState(&state);
    return state;
}

/// Off the WASAPI callback: it may not register, unregister, wait or drop a last reference.
void AudioDeviceController::_later(std::weak_ptr<AudioDeviceController> weak, IAudioSessionControl* control,
                                   const bool gone) {
    struct Job {
        std::weak_ptr<AudioDeviceController> Owner;
        ComPtr<IAudioSessionControl> Control;
        bool Gone;
    };
    auto* job = new Job{std::move(weak), control, gone};
    if (!TrySubmitThreadpoolCallback([](PTP_CALLBACK_INSTANCE, void* context) {
            const std::unique_ptr<Job> owned(static_cast<Job*>(context));
            if (const auto owner = owned->Owner.lock()) {
                owner->_update(owned->Control.Get(), owned->Gone);
            }
        }, job, nullptr)) {
        delete job;
    }
}

/// Reads the state afresh, so out-of-order jobs leave the last; raises under the lock, so none reaches a torn-down owner.
void AudioDeviceController::_update(IAudioSessionControl* control, const bool gone) {
    std::lock_guard lock(_sessionMutex);
    if (!_sessionCreated) {
        return;
    }
    const AudioSessionState state = _state(control);
    const auto it = _sessions.find(control);
    if (gone || state == AudioSessionStateExpired) {
        if (it == _sessions.end()) {
            return;
        }
        control->UnregisterAudioSessionNotification(it->second.Handler.Get());
        _sessions.erase(it);
    } else if (it == _sessions.end()) {
        _watch(control);
    } else {
        it->second.Active = state == AudioSessionStateActive;
    }
    if (OnSessionsChanged) {
        (*OnSessionsChanged)();
    }
}

/// Under _sessionMutex.
void AudioDeviceController::_watch(IAudioSessionControl* control) {
    ComPtr<SessionStateCallback> handler;
    handler.Attach(new SessionStateCallback(control,
        [weak = weak_from_this()](IAudioSessionControl* session, const bool gone) { _later(weak, session, gone); }));
    if (SUCCEEDED(control->RegisterAudioSessionNotification(handler.Get()))) {
        _sessions[control] = {handler, _state(control) == AudioSessionStateActive};
    }
}
