//
// Created by kitbyte on 04.11.2025.
//
#include "HotkeyManager.hpp"
#include <unordered_map>
#include <unordered_set>
#include "Win32Hook.hpp"

#pragma region [PRIVATE] Hotkeys name table

constexpr struct {
    uint8_t bit;
    const char* name;
} ModifiersOrderedList[] = {
    { 0x04, "CTRL" },
    { 0x20, "RCTRL" },
    { 0x08, "SHIFT" },
    { 0x40, "RSHIFT" },
    { 0x10, "ALT" },
    { 0x80, "RALT" }
};

constexpr const char* KeysNameTable[255] = {
    nullptr,             // 0x00
    "MouseLeft",         // VK_LBUTTON 	0x01
    "MouseRight",        // VK_RBUTTON 	0x02
    "Cancel",            // VK_CANCEL 	0x03
    "MouseMiddle",       // VK_MBUTTON 	0x04
    "MouseX1",           // VK_XBUTTON1 	0x05
    "MouseX2",           // VK_XBUTTON2 	0x06
    nullptr,             // 0x07
    "Back",              // VK_BACK 	0x08
    "Tab",               // VK_TAB 	0x09
    nullptr,             // 0x0A
    nullptr,             // 0x0B
    "Clear",             // VK_CLEAR 	0x0C
    "Enter",             // VK_RETURN 	0x0D
    nullptr,             // 0x0E
    nullptr,             // 0x0F
    "Shift",             // VK_SHIFT 	0x10
    "Ctrl",              // VK_CONTROL 	0x11
    "Alt",               // VK_MENU 	0x12
    "Pause",             // VK_PAUSE 	0x13
    "CapsLock",          // VK_CAPITAL 	0x14
    nullptr,             // VK_KANA/VK_HANGUEL/VK_HANGUL 	0x15
    nullptr,             // 0x16
    nullptr,             // VK_JUNJA 	0x17
    nullptr,             // VK_FINAL 	0x18
    nullptr,             // VK_HANJA/VK_KANJI 	0x19
    nullptr,             // 0x1A
    "Escape",            // VK_ESCAPE 	0x1B
    "Convert",           // VK_CONVERT 	0x1C
    "NonConvert",        // VK_NONCONVERT 	0x1D
    "Accept",            // VK_ACCEPT 	0x1E
    "ModeChange",        // VK_MODECHANGE 	0x1F
    "Space",             // VK_SPACE 	0x20
    "PageUp",            // VK_PRIOR 	0x21
    "PageDown",          // VK_NEXT 	0x22
    "End",               // VK_END 	0x23
    "Home",              // VK_HOME 	0x24
    "Left",              // VK_LEFT 	0x25
    "Up",                // VK_UP 	0x26
    "Right",             // VK_RIGHT 	0x27
    "Down",              // VK_DOWN 	0x28
    "Select",            // VK_SELECT 	0x29
    "Print",             // VK_PRINT 	0x2A
    "Execute",           // VK_EXECUTE 	0x2B
    "PrintScreen",       // VK_SNAPSHOT 	0x2C
    "Insert",            // VK_INSERT 	0x2D
    "Delete",            // VK_DELETE 	0x2E
    "Help",              // VK_HELP 	0x2F
    "0",                 // '0' 	0x30
    "1",                 // '1' 	0x31
    "2",                 // '2' 	0x32
    "3",                 // '3' 	0x33
    "4",                 // '4' 	0x34
    "5",                 // '5' 	0x35
    "6",                 // '6' 	0x36
    "7",                 // '7' 	0x37
    "8",                 // '8' 	0x38
    "9",                 // '9' 	0x39
    nullptr,             // 0x3A
    nullptr,             // 0x3B
    nullptr,             // 0x3C
    nullptr,             // 0x3D
    nullptr,             // 0x3E
    nullptr,             // 0x3F
    nullptr,             // 0x40
    "A",                 // 'A' 	0x41
    "B",                 // 'B' 	0x42
    "C",                 // 'C' 	0x43
    "D",                 // 'D' 	0x44
    "E",                 // 'E' 	0x45
    "F",                 // 'F' 	0x46
    "G",                 // 'G' 	0x47
    "H",                 // 'H' 	0x48
    "I",                 // 'I' 	0x49
    "J",                 // 'J' 	0x4A
    "K",                 // 'K' 	0x4B
    "L",                 // 'L' 	0x4C
    "M",                 // 'M' 	0x4D
    "N",                 // 'N' 	0x4E
    "O",                 // 'O' 	0x4F
    "P",                 // 'P' 	0x50
    "Q",                 // 'Q' 	0x51
    "R",                 // 'R' 	0x52
    "S",                 // 'S' 	0x53
    "T",                 // 'T' 	0x54
    "U",                 // 'U' 	0x55
    "V",                 // 'V' 	0x56
    "W",                 // 'W' 	0x57
    "X",                 // 'X' 	0x58
    "Y",                 // 'Y' 	0x59
    "Z",                 // 'Z' 	0x5A
    "LWin",              // VK_LWIN 	0x5B
    "RWin",              // VK_RWIN 	0x5C
    "Apps",              // VK_APPS 	0x5D
    nullptr,             // 0x5E
    "Sleep",             // VK_SLEEP 	0x5F
    "NumPad0",           // VK_NUMPAD0 	0x60
    "NumPad1",           // VK_NUMPAD1 	0x61
    "NumPad2",           // VK_NUMPAD2 	0x62
    "NumPad3",           // VK_NUMPAD3 	0x63
    "NumPad4",           // VK_NUMPAD4 	0x64
    "NumPad5",           // VK_NUMPAD5 	0x65
    "NumPad6",           // VK_NUMPAD6 	0x66
    "NumPad7",           // VK_NUMPAD7 	0x67
    "NumPad8",           // VK_NUMPAD8 	0x68
    "NumPad9",           // VK_NUMPAD9 	0x69
    "Multiply",          // VK_MULTIPLY 	0x6A
    "Add",               // VK_ADD 	0x6B
    "Separator",         // VK_SEPARATOR 	0x6C
    "Subtract",          // VK_SUBTRACT 	0x6D
    "Decimal",           // VK_DECIMAL 	0x6E
    "Divide",            // VK_DIVIDE 	0x6F
    "F1",                // VK_F1 	0x70
    "F2",                // VK_F2 	0x71
    "F3",                // VK_F3 	0x72
    "F4",                // VK_F4 	0x73
    "F5",                // VK_F5 	0x74
    "F6",                // VK_F6 	0x75
    "F7",                // VK_F7 	0x76
    "F8",                // VK_F8 	0x77
    "F9",                // VK_F9 	0x78
    "F10",               // VK_F10 	0x79
    "F11",               // VK_F11 	0x7A
    "F12",               // VK_F12 	0x7B
    "F13",               // VK_F13 	0x7C
    "F14",               // VK_F14 	0x7D
    "F15",               // VK_F15 	0x7E
    "F16",               // VK_F16 	0x7F
    "F17",               // VK_F17 	0x80
    "F18",               // VK_F18 	0x81
    "F19",               // VK_F19 	0x82
    "F20",               // VK_F20 	0x83
    "F21",               // VK_F21 	0x84
    "F22",               // VK_F22 	0x85
    "F23",               // VK_F23 	0x86
    "F24",               // VK_F24 	0x87
    nullptr,             // 0x88
    nullptr,             // 0x89
    nullptr,             // 0x8A
    nullptr,             // 0x8B
    nullptr,             // 0x8C
    nullptr,             // 0x8D
    nullptr,             // 0x8E
    nullptr,             // 0x8F
    "NumLock",           // VK_NUMLOCK 	0x90
    "ScrollLock",        // VK_SCROLL 	0x91
    nullptr,             // 0x92
    nullptr,             // 0x93
    nullptr,             // 0x94
    nullptr,             // 0x95
    nullptr,             // 0x96
    nullptr,             // 0x97
    nullptr,             // 0x98
    nullptr,             // 0x99
    nullptr,             // 0x9A
    nullptr,             // 0x9B
    nullptr,             // 0x9C
    nullptr,             // 0x9D
    nullptr,             // 0x9E
    nullptr,             // 0x9F
    "LSHIFT",            // VK_LSHIFT 	0xA0
    "RSHIFT",            // VK_RSHIFT 	0xA1
    "LCTRL",             // VK_LCONTROL 	0xA2
    "RCTRL",             // VK_RCONTROL 	0xA3
    "LALT",              // VK_LMENU 	0xA4
    "RALT",              // VK_RMENU 	0xA5
    "BrowserBack",       // VK_BROWSER_BACK 	    0xA6
    "BrowserForward",    // VK_BROWSER_FORWARD 	0xA7
    "BrowserRefresh",    // VK_BROWSER_REFRESH 	0xA8
    "BrowserStop",       // VK_BROWSER_STOP 	    0xA9
    "BrowserSearch",     // VK_BROWSER_SEARCH 	0xAA
    "BrowserFavorites",  // VK_BROWSER_FAVORITES 	0xAB
    "BrowserHome",       // VK_BROWSER_HOME 	    0xAC
    "VolumeMute",        // VK_VOLUME_MUTE 	    0xAD
    "VolumeDown",        // VK_VOLUME_DOWN 	    0xAE
    "VolumeUp",          // VK_VOLUME_UP 	        0xAF
    "MediaNextTrack",    // VK_MEDIA_NEXT_TRACK 	0xB0
    "MediaPrevTrack",    // VK_MEDIA_PREV_TRACK 	0xB1
    "MediaStop",         // VK_MEDIA_STOP 	    0xB2
    "MediaPlayPause",    // VK_MEDIA_PLAY_PAUSE 	0xB3
    "LaunchMail",        // VK_LAUNCH_MAIL 	    0xB4
    "LaunchMediaSelect", // VK_LAUNCH_MEDIA_SELECT 0xB5
    "LaunchApp1",        // VK_LAUNCH_APP1 	    0xB6
    "LaunchApp2",        // VK_LAUNCH_APP2 	    0xB7
    nullptr,             // 0xB8
    nullptr,             // 0xB9
    ":",                 // VK_OEM_1 	            0xBA
    "+",                 // VK_OEM_PLUS 	        0xBB
    ",",                 // VK_OEM_COMMA 	        0xBC
    "-",                 // VK_OEM_MINUS 	        0xBD
    ".",                 // VK_OEM_PERIOD 	    0xBE
    "/?",                // VK_OEM_2   0xBF
    "~",                 // VK_OEM_3   0xC0
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    "[",                 // VK_OEM_4 	0xDB
    "\\",                // VK_OEM_5 	0xDC
    "]",                 // VK_OEM_6 	0xDD
    "\"",                // VK_OEM_7 	0xDE
    "OEM_8",             // VK_OEM_8 	0xDF
    nullptr,
    nullptr,
    "<",                 // VK_OEM_102 0xE2
};
#pragma endregion

namespace  HotkeyManager {
    uint64_t _sequenceMask = 0;
    std::unordered_map <uint64_t, HotkeyBinding> _hotkeys;
    BindingCallback _onBindingCallback = nullptr;


    uint8_t _keys[255];
    std::unique_ptr<Win32Hook> _keyboardHook = nullptr;
    std::unique_ptr<Win32Hook> _mouseHook = nullptr;


    constexpr uint64_t _getModifierBit(uint8_t vkCode) {
        switch (vkCode) {
            case VK_LCONTROL: return Keys::Modifier::MOD_LCTRL;  // Bit 2
            case VK_LSHIFT:   return Keys::Modifier::MOD_LSHIFT; // Bit 3
            case VK_LMENU:    return Keys::Modifier::MOD_LALT;   // Bit 4
            case VK_RCONTROL: return Keys::Modifier::MOD_RCTRL;  // Bit 5
            case VK_RSHIFT:   return Keys::Modifier::MOD_RSHIFT; // Bit 6
            case VK_RMENU:    return Keys::Modifier::MOD_RALT;   // Bit 7
            default:          return Keys::Modifier::MOD_NONE;
        }
    }


    void _onKeyRelease(const uint8_t vkCode) {
        if (const auto modifierBit = _getModifierBit(vkCode)) {
            _sequenceMask &= ~modifierBit;
            return;
        }

        const uint8_t modifiers = _sequenceMask & 0xFF;
        const uint8_t lastKeyPressed = (_sequenceMask >> 8) & 0xFF;

        // If the last pressed key is released, delete it and move the other keys to the right.\
        // 1. Delete the second byte by shifting all bytes above it by one byte to the right.
        // Shift bytes 2-7 in place of bytes 1-6
        // 2. Save modifier byte
        _sequenceMask = lastKeyPressed == vkCode ? (((_sequenceMask >> 16) << 8) | modifiers) : modifiers;

        if (_onBindingCallback) {
            _onBindingCallback(vkCode, RELEASED, _sequenceMask, GetHotkeyName(_sequenceMask));
            // Skip hotkey handling if in binding mode
            return;
        }

        if (const auto it = _hotkeys.find(_sequenceMask); it != _hotkeys.end() && it->second.onRelease) {
            it->second.onRelease();
        }

        //printf("Key released | mask: 0x%.16llX\n, vkCode hex: 0x%02X\n", hotkeyMask, vkCode);
    }

    void _onKeyPress(const uint8_t vkCode) {
        if (const auto modifierBit = _getModifierBit(vkCode)) {
            _sequenceMask |= modifierBit;
            return;
        }

        // 1. Save the current (first) byte of modifiers
        const uint8_t modifiers = _sequenceMask & 0xFF;
        // 2 Shift the existing sequence 1 byte to the left. Excluding the first byte
        uint64_t sequence = (_sequenceMask & ~0xFF) << 8;
        // 3. Add a new key to the second byte
        sequence |= (static_cast<uint64_t>(vkCode) << 8);
        // 4. Restore modifier byte
        _sequenceMask = sequence | modifiers;

        if (_onBindingCallback) {
            _onBindingCallback(vkCode, PRESSED, _sequenceMask, GetHotkeyName(_sequenceMask));
            // Skip hotkey handling if in binding mode
            return;
        }

        if (const auto it = _hotkeys.find(_sequenceMask); it != _hotkeys.end() && it->second.onPress) {
            it->second.onPress();
        }

         /*
         printf("Key pressed | mask: 0x%016llX, vkCode hex: 0x%02X\n", _sequenceMask, vkCode);
        const auto hotkey = GetHotkeyName(_sequenceMask);
        printf("Hotkey: %s\n", hotkey.c_str());*/
    }

    LRESULT CALLBACK _lowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
        if (nCode != HC_ACTION) {
            return CallNextHookEx(nullptr, nCode, wParam, lParam);
        }

        const auto *const pKbdStruct = reinterpret_cast<KBDLLHOOKSTRUCT *>(lParam);
        const auto code = pKbdStruct->vkCode;
        if ((wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) && _keys[code] != Keys::KEY_PRESSED) {
            _keys[code] = Keys::KEY_PRESSED;
            _onKeyPress(pKbdStruct->vkCode);
        } else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
            _keys[code] = Keys::KEY_RELEASED;
            _onKeyRelease(pKbdStruct->vkCode);
        }

        return 1;
    }


    LRESULT CALLBACK _lowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
        static MSLLHOOKSTRUCT* pMouseStruct;

        if (nCode != HC_ACTION) {
            return CallNextHookEx(nullptr, nCode, wParam, lParam);
        }

        DWORD vkCode = -1;
        bool keyup = false;
        pMouseStruct = reinterpret_cast<MSLLHOOKSTRUCT *>(lParam);
        // We cant do anything with this strange switch, WM_X does not match VK code
        switch (wParam) {

            case WM_XBUTTONUP:
                keyup = true;
            case WM_XBUTTONDOWN:
                vkCode = (pMouseStruct->mouseData >> 16) + 4;
                break;

            case WM_LBUTTONUP:
                keyup = true;
            case WM_LBUTTONDOWN:
                vkCode = VK_LBUTTON;
                break;

            case WM_MBUTTONUP:
                keyup = true;
            case WM_MBUTTONDOWN:
                vkCode = VK_MBUTTON;
                break;

            case WM_RBUTTONUP:
                keyup = true;
            case WM_RBUTTONDOWN:
                vkCode = VK_RBUTTON;
                break;
        }

        if (vkCode != -1) {
            if (keyup && _keys[vkCode] == Keys::KEY_PRESSED) {
                _keys[vkCode] = Keys::KEY_RELEASED;
                _onKeyRelease(vkCode);
            } else if (!keyup && _keys[vkCode] == Keys::KEY_RELEASED) {
                _keys[vkCode] = Keys::KEY_PRESSED;
                _onKeyPress(vkCode);
            }
        }

        return 1;
    }

    std::string GetHotkeyName(const uint64_t keysMask) {
        std::string modifierResult;
        std::string result;

        // Add modifiers in sorted order
        const uint8_t modifierByte = keysMask & 0xFF;
        bool hasModifiers = false;
        for (const auto& mod : ModifiersOrderedList) {
            if (modifierByte & mod.bit) {
                if (hasModifiers) {
                    modifierResult += " + ";
                }
                modifierResult += mod.name;
                hasModifiers = true;
            }
        }

        bool hasKeys = false;
        for (int i = 1; i < 8; i++) {
            const uint8_t keyByte = (keysMask >> (i * 8)) & 0xFF;
            if (keyByte == 0) {
                break;
            }

            const char* keyName = KeysNameTable[keyByte];
            if (keyName == nullptr) {
                static char hexCode[5];
                sprintf_s(hexCode, "0x%02X", keyByte);
                keyName = hexCode;
            }

            if (hasKeys) {
                result.insert(0, " + ");
            }
            // ReSharper disable once CppDFALocalValueEscapesScope
            result.insert(0, keyName);
            hasKeys = true;
        }

        // Connect modifiers and keys if both are present
        if (hasModifiers && hasKeys) {
            return modifierResult + " + " + result;
        }

        return modifierResult.append(result);
    }

    bool RegisterHotkey(const uint64_t keysMask, const HotkeyBinding &binding, const bool overwrite) {
        return overwrite ?
           _hotkeys.insert_or_assign(keysMask, binding).second :
           _hotkeys.try_emplace(keysMask, binding).second;
    }

    bool RegisterHotkey(const uint64_t keysMask, const std::function<void()>& onPress, const bool overwrite) {
        return RegisterHotkey(keysMask, { onPress, nullptr }, overwrite);
    }

    void BindStart(const BindingCallback& callback) {
        if (!callback) {
            throw std::runtime_error("HotkeyManager::BindStart: callback is null");
        }

        _onBindingCallback = callback;
        if (_sequenceMask != 0) {
            const uint8_t lastKeyPressed = (_sequenceMask >> 8) & 0xFF;
            _onBindingCallback(lastKeyPressed, PRESSED, _sequenceMask, GetHotkeyName(_sequenceMask));
        }
    }

    void BindStop() {
        _onBindingCallback = nullptr;
    }

    bool UnregisterHotkey(const uint64_t keysMask) {
        return _hotkeys.erase(keysMask) > 0;
    }

    void Initialize() {
        if (_keyboardHook || _mouseHook) {
            throw std::runtime_error("HotkeyManager already initialized");
        }
        _keyboardHook = Win32Hook::Create(WH_KEYBOARD_LL, _lowLevelKeyboardProc, nullptr, 0);
        _mouseHook = Win32Hook::Create(WH_MOUSE_LL, _lowLevelMouseProc, nullptr, 0);
    }

    void Dispose() {
        _keyboardHook = nullptr;
        _mouseHook = nullptr;
    }
}


