#pragma once

#include <atomic>
#include <functional>
#include <string_view>
#include <windows.h>

#include "KeyChord.hpp"
#include "Input/Input.hpp"

/**
 * @brief The single hotkey capture session shared by every binding UI, as the first stage of the
 * input pipeline.
 *
 * Nothing but a PostMessage happens inside the hook. Formatting the combination name or writing it
 * into a control there would run string allocation and a synchronous WM_SETTEXT against
 * LowLevelHooksTimeout, and Windows answers a slow proc by silently dropping the hook - which is
 * also why teardown is deferred to the owning window rather than done in the callback. Every key
 * is delivered on, so no hotkey fires while one is being typed and the dialog still sees it.
 */
namespace HotkeyCapture {
    /// The combination changed; read CapturedMask() and render it.
    inline constexpr UINT WM_CAPTURE_PREVIEW = WM_APP;
    /// The user let go; the owning window must route this to Finish().
    inline constexpr UINT WM_CAPTURE_DONE = WM_APP + 1;

    /// Final combination, 0 when cleared with ESC.
    using DoneCallback = std::function<void(uint64_t mask)>;

    namespace Detail {
        inline constexpr std::string_view StageId = "hotkeys.capture";

        // UI thread
        inline DoneCallback _done;
        inline bool _active = false;

        // Shared with the input thread
        inline std::atomic<HWND> _target = nullptr;
        inline std::atomic<uint64_t> _captured = 0;
        inline std::atomic<bool> _pending = false;

        // Input thread
        inline KeyChord _chord;

        inline Input::Verdict _onKey(const Input::KeyEvent& event) {
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

        inline void _teardown() {
            _active = false;
            _target = nullptr;
            _pending = false;
            _done = nullptr;
            Input::Disable(StageId);
        }
    }

    /// Before Input::Start.
    inline void Register() {
        Input::Add({.Id = Detail::StageId, .Order = 0, .OnKey = &Detail::_onKey,
                    .OnReset = [] { Detail::_chord = {}; }});
    }

    inline bool IsActive() { return Detail::_active; }

    /// What has been typed so far, for the window handling WM_CAPTURE_PREVIEW.
    inline uint64_t CapturedMask() { return Detail::_captured; }

    inline bool Start(HWND target, DoneCallback done) {
        using namespace Detail;

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

    /// WM_CAPTURE_DONE handler - the only place a capture may be torn down while it is running.
    inline void Finish() {
        if (!Detail::_pending) {
            return;
        }

        const uint64_t mask = Detail::_captured;
        auto done = std::move(Detail::_done);
        Detail::_teardown();
        // A release racing a Cancel can still post one WM_CAPTURE_DONE after it
        if (done) {
            done(mask);
        }
    }

    /// Drops a capture still running when its window goes away.
    inline void Cancel() {
        if (Detail::_active) {
            Detail::_teardown();
        }
    }
}
