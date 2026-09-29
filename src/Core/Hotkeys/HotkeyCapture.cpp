#include "HotkeyCapture.hpp"

#include <atomic>
#include <string_view>

#include "Input/Input.hpp"
#include "KeyChord.hpp"

namespace HotkeyCapture {

namespace {

    constexpr std::string_view StageId = "hotkeys.capture";

    // UI thread
    DoneCallback _done;
    bool _active = false;

    // Shared with the input thread
    std::atomic<HWND> _target = nullptr;
    std::atomic<uint64_t> _captured = 0;
    std::atomic<bool> _pending = false;

    // Input thread
    KeyChord _chord;

    /// Only a PostMessage runs in the hook: formatting or WM_SETTEXT here could overrun LowLevelHooksTimeout.
    Input::Verdict _onKey(const Input::KeyEvent& event) {
        const HWND target = _target;
        if (!target || _pending || event.Repeat) {
            return Input::Verdict::Deliver;
        }

        if (event.Down) {
            _chord.Press(event.Vk);
            _captured = _chord.Mask;
            PostMessageW(target, WM_CAPTURE_PREVIEW, 0, 0);
            return Input::Verdict::Deliver;
        }

        _chord.Release(event.Vk);
        if (event.Vk == VK_ESCAPE && _chord.Mask == 0) {
            _captured = 0;
        }
        _pending = true;
        PostMessageW(target, WM_CAPTURE_DONE, 0, 0);
        return Input::Verdict::Deliver;
    }

    void _teardown() {
        _active = false;
        _target = nullptr;
        _pending = false;
        _done = nullptr;
        Input::Disable(StageId);
    }

} // anonymous namespace

void Register() {
    Input::Add({.Id = StageId, .Order = 0, .OnKey = &_onKey, .OnReset = [] { _chord = {}; }});
}

bool IsActive() {
    return _active;
}

uint64_t CapturedMask() {
    return _captured;
}

bool Start(const HWND target, DoneCallback done) {
    if (_active) {
        return false;
    }

    _done = std::move(done);
    _captured = 0;
    _pending = false;
    _target = target;
    _active = true;
    // Enabling starts the stage over, so a key held from before never ends the capture
    Input::Enable(StageId, Input::WantKeys | Input::WantButtons);
    return true;
}

void Finish() {
    if (!_pending) {
        return;
    }

    const uint64_t mask = _captured;
    const auto done = std::move(_done);
    _teardown();
    // A release racing a Cancel can still post one WM_CAPTURE_DONE after it
    if (done) {
        done(mask);
    }
}

void Cancel() {
    if (_active) {
        _teardown();
    }
}

}
