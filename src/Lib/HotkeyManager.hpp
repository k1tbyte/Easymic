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
        MOD_NONE   = 0x00,
        MOD_LCTRL  = 0x04,
        MOD_LSHIFT = 0x08,
        MOD_LALT   = 0x10,
        MOD_RCTRL  = 0x20,
        MOD_RSHIFT = 0x40,
        MOD_RALT   = 0x80,

        MOD_L_CTRL_ALT_SHIFT = MOD_LCTRL | MOD_LSHIFT | MOD_LALT,
        MOD_L_CTRL_SHIFT     = MOD_LCTRL | MOD_LSHIFT,
        MOD_L_CTRL_ALT       = MOD_LCTRL | MOD_LALT,
        MOD_L_ALT_SHIFT      = MOD_LALT | MOD_LSHIFT,

        MOD_R_CTRL_ALT_SHIFT = MOD_RCTRL | MOD_RSHIFT | MOD_RALT,
        MOD_R_CTRL_SHIFT     = MOD_RCTRL | MOD_RSHIFT,
        MOD_R_CTRL_ALT       = MOD_RCTRL | MOD_RALT,
        MOD_R_ALT_SHIFT      = MOD_RALT | MOD_RSHIFT,

        MOD_LR_CTRL  = MOD_LCTRL | MOD_RCTRL,
        MOD_LR_SHIFT = MOD_LSHIFT | MOD_RSHIFT,
        MOD_LR_ALT   = MOD_LALT | MOD_RALT,

        MOD_LR_CTRL_ALT_SHIFT = MOD_LR_CTRL | MOD_LR_SHIFT | MOD_LR_ALT,
        MOD_LR_CTRL_SHIFT     = MOD_LR_CTRL | MOD_LR_SHIFT,
        MOD_LR_CTRL_ALT       = MOD_LR_CTRL | MOD_LR_ALT,
        MOD_LR_ALT_SHIFT      = MOD_LR_ALT | MOD_LR_SHIFT
    } Modifier;

    constexpr uint64_t make(const uint8_t mod, const uint8_t k1) {
        return static_cast<uint64_t>(mod) | (static_cast<uint64_t>(k1) << 8);
    }

    template<typename... vk_codes>
    constexpr uint64_t make(uint8_t mod, uint8_t k1, vk_codes... codes) {
        return make(mod, codes...) | (static_cast<uint64_t>(k1) << (8 * (1 + sizeof...(codes))));
    }
}


namespace HotkeyManager {

    enum class InputType {
        KEYBOARD = 1,
        MOUSE = 2
    };

    using BindingCallback = std::function<void(uint8_t lastCode, Keys::State lastState, uint64_t sequenceMask, const std::string& hotkeyName)>;



    struct HotkeyBinding {
        std::function<void()> onPress;
        std::function<void()> onRelease;
    };

    std::string GetHotkeyName(uint64_t keysMask);
    bool RegisterHotkey(uint64_t keysMask, const HotkeyBinding& binding, bool overwrite = false);
    bool RegisterHotkey(uint64_t keysMask, const std::function<void()>& onPress, bool overwrite = false);
    bool UnregisterHotkey(uint64_t keysMask);
    void BindStart(const BindingCallback& callback);
    void BindStop();
    void Initialize();
    void ClearHotkeys();
    void Dispose();

}

#endif //EASYMIC_HOTKEYMANAGER_H