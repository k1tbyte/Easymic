#pragma once

#include <cstdint>
#include <windows.h>
#include <functional>

namespace Keys {

    typedef enum {
        KEY_RELEASED = 0,
        KEY_PRESSED  = 1
    } State;

    typedef enum {
        MOD_LCTRL  = 0x04,
        MOD_LSHIFT = 0x08,
        MOD_LALT   = 0x10,
        MOD_RCTRL  = 0x20,
        MOD_RSHIFT = 0x40,
        MOD_RALT   = 0x80
    } Modifier;
}

/**
 * @brief The low-level hooks and the mask table they look combinations up in.
 *
 * Nothing here runs an action: the hook proc resolves a mask and hands the work to Dispatcher,
 * which owns the worker thread. Naming a combination belongs to KeyNames.
 */
namespace HotkeyService {

    /**
     * @brief Raised from inside the low-level hook proc while a combination is being bound.
     *
     * Everything it does counts against LowLevelHooksTimeout, so it gets the raw mask and nothing
     * else - formatting a name or touching a window here is what makes Windows drop the hook.
     */
    using BindingCallback = std::function<void(uint8_t lastCode, Keys::State lastState, uint64_t sequenceMask)>;

    struct HotkeyBinding {
        std::function<void()> onPress;
        std::function<void()> onRelease;
        /// Swallows the key instead of passing it down the hook chain. It applies to the whole
        /// combination, not to this count alone - a first press leaking through while the second
        /// was eaten is worse than either answer.
        bool block = false;
        /// Drops the release when another key went down while this one was held, so a key can act
        /// as a modifier for the next one and still do its own thing when tapped by itself.
        bool tapOnly = false;
    };

    /// Binds an action to a combination pressed `presses` times in a row. Two actions may share
    /// a combination as long as the count differs; only the pair has to be unique.
    bool RegisterHotkey(uint64_t keysMask, uint8_t presses, const HotkeyBinding& binding,
                        bool overwrite = false);
    /// How long a combination waits for another press. Only a combination with more than one
    /// bound count ever waits - everything else still fires on the press itself.
    void SetMultiPressWindow(uint16_t milliseconds);
    void BindStart(const BindingCallback& callback);
    void BindStop();
    /// False when the hooks are already up or the OS refused them - never throws, it is called
    /// from inside Win32 callbacks.
    bool Initialize();
    /// True while the low-level hooks are installed.
    bool IsHooked();
    void ClearHotkeys();
    void Dispose();

}
