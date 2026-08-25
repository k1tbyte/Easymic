//
// Created by kitbyte on 04.11.2025.
//

#ifndef EASYMIC_HOTKEYMANAGER_H
#define EASYMIC_HOTKEYMANAGER_H

#include <cstdint>
#include <windows.h>
#include <string>
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


namespace HotkeyManager {

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
    };

    std::string GetHotkeyName(uint64_t keysMask);
    bool RegisterHotkey(uint64_t keysMask, const HotkeyBinding& binding, bool overwrite = false);
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

#endif //EASYMIC_HOTKEYMANAGER_H
