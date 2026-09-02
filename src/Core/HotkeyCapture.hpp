#pragma once

#include <functional>
#include <windows.h>

#include "Core/HotkeyService.hpp"

/**
 * @brief The single hotkey capture session shared by every binding UI.
 *
 * Nothing but a PostMessage happens inside the LL hook proc. Formatting the combination name or
 * writing it into a control there would run string allocation and a synchronous WM_SETTEXT
 * against LowLevelHooksTimeout, and Windows answers a slow proc by silently dropping the hook -
 * which is also why teardown is deferred to the owning window rather than done in the callback.
 */
namespace HotkeyCapture {
    /// The combination changed; read CapturedMask() and render it.
    inline constexpr UINT WM_CAPTURE_PREVIEW = WM_APP;
    /// The user let go; the owning window must route this to Finish().
    inline constexpr UINT WM_CAPTURE_DONE = WM_APP + 1;

    /// Final combination, 0 when cleared with ESC.
    using DoneCallback = std::function<void(uint64_t mask)>;

    namespace Detail {
        inline HWND _target = nullptr;
        inline DoneCallback _done;
        inline uint64_t _captured = 0;
        inline bool _active = false;
        inline bool _pending = false;
        /// Hooks that were already up belong to whoever installed them - Dispose would drop
        /// every registered hotkey with them
        inline bool _ownsHooks = false;

        inline void _teardown() {
            const bool ownedHooks = _ownsHooks;
            _active = false;
            _pending = false;
            _ownsHooks = false;
            _captured = 0;
            _target = nullptr;
            _done = nullptr;
            HotkeyService::BindStop();
            if (ownedHooks) {
                HotkeyService::Dispose();
            }
        }
    }

    inline bool IsActive() { return Detail::_active; }

    /// What has been typed so far, for the window handling WM_CAPTURE_PREVIEW.
    inline uint64_t CapturedMask() { return Detail::_captured; }

    inline bool Start(HWND target, DoneCallback done) {
        using namespace Detail;

        if (_active) {
            return false;
        }

        _ownsHooks = !HotkeyService::IsHooked();
        if (_ownsHooks && !HotkeyService::Initialize()) {
            _ownsHooks = false;
            return false;
        }

        _target = target;
        _done = std::move(done);
        _captured = 0;
        _active = true;

        HotkeyService::BindStart([](uint8_t vkCode, Keys::State state, uint64_t sequenceMask) {
            if (_pending) {
                return;
            }

            if (state != Keys::State::KEY_RELEASED) {
                _captured = sequenceMask;
                PostMessageW(_target, WM_CAPTURE_PREVIEW, 0, 0);
                return;
            }

            // The key that opened the binding UI was pressed before the hooks existed - only its
            // release arrives here, and it must not end the capture before anything was typed
            if (_captured == 0 && vkCode != VK_ESCAPE) {
                return;
            }

            if (vkCode == VK_ESCAPE && sequenceMask == 0) {
                _captured = 0;
            }

            _pending = true;
            PostMessageW(_target, WM_CAPTURE_DONE, 0, 0);
        });

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
        done(mask);
    }

    /// Drops a capture still running when its window goes away.
    inline void Cancel() {
        if (Detail::_active) {
            Detail::_teardown();
        }
    }
}

