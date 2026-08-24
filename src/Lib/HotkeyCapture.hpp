#ifndef EASYMIC_HOTKEYCAPTURE_HPP
#define EASYMIC_HOTKEYCAPTURE_HPP

#include <functional>
#include <string>
#include <windows.h>

#include "HotkeyManager.hpp"

/**
 * @brief The single hotkey capture session shared by every binding UI.
 *
 * Teardown must never run inside the LL hook proc - unhooking and joining the action worker there
 * blows LowLevelHooksTimeout and Windows silently drops the hook. The hook callback therefore only
 * posts WM_CAPTURE_DONE to the owning window, which must route it to Finish().
 */
namespace HotkeyCapture {
    inline constexpr UINT WM_CAPTURE_DONE = WM_APP;

    /// Combination typed so far, live.
    using PreviewCallback = std::function<void(const std::string& hotkeyName)>;
    /// Final combination, 0 when cleared with ESC.
    using DoneCallback = std::function<void(uint64_t mask)>;

    namespace Detail {
        inline HWND _target = nullptr;
        inline PreviewCallback _preview;
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
            _preview = nullptr;
            _done = nullptr;
            HotkeyManager::BindStop();
            if (ownedHooks) {
                HotkeyManager::Dispose();
            }
        }
    }

    inline bool IsActive() { return Detail::_active; }

    inline bool Start(HWND target, PreviewCallback preview, DoneCallback done) {
        using namespace Detail;

        if (_active) {
            return false;
        }

        _ownsHooks = !HotkeyManager::IsHooked();
        if (_ownsHooks && !HotkeyManager::Initialize()) {
            _ownsHooks = false;
            return false;
        }

        _target = target;
        _preview = std::move(preview);
        _done = std::move(done);
        _captured = 0;
        _active = true;

        HotkeyManager::BindStart([](uint8_t vkCode, Keys::State state, uint64_t sequenceMask,
                                    const std::string& hotkeyName) {
            if (_pending) {
                return;
            }

            if (state != Keys::State::KEY_RELEASED) {
                _captured = sequenceMask;
                _preview(hotkeyName);
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

#endif //EASYMIC_HOTKEYCAPTURE_HPP
