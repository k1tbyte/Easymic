#ifndef EASYMIC_HOTKEYMANAGER_HPP
#define EASYMIC_HOTKEYMANAGER_HPP

#define HKModifier(h) ((HIBYTE (h) & 2) | ((HIBYTE (h) & 4) >> 2) | ((HIBYTE (h) & 1) << 2))

class HotkeyManager final {
    static HHOOK mouseHook;
    static HHOOK keybdHook;
    static std::function<void()> OnHotkeyPressed;
    static std::function<void(DWORD)> OnHotkeyBound;

    static DWORD currentSequenceMask;
    static DWORD hotkeySequenceMask;
    static WORD mouseWm;
    static BYTE mouseData;

    static int codeNum;
    static BYTE prevCode;
public:

    static void Initialize(const std::function<void()>& hotkeyPressed) {
        HotkeyManager::OnHotkeyPressed = hotkeyPressed;
        keybdHook = mouseHook = nullptr;
        hotkeySequenceMask = currentSequenceMask = 0;
        mouseWm = 0;
        mouseData = 0;
        prevCode = codeNum = 0;
    }

    static LRESULT CALLBACK MouseInterceptor(int code, WPARAM wParam, LPARAM lParam) {

        if(code != HC_ACTION || wParam == WM_MOUSEMOVE ||
            wParam == WM_LBUTTONUP || wParam == WM_LBUTTONDOWN || wParam == WM_LBUTTONDBLCLK) {
            goto exit;
        }

        static MSLLHOOKSTRUCT * event;
        // If WM is known, we do not need to iterate through everything
        if(mouseWm) {
            if(wParam == mouseWm && (mouseData == 0 ||
                (((MSLLHOOKSTRUCT *)lParam)->mouseData >> 16) == mouseData)) {
                /*std::future<void> result = std::async(std::launch::async,[]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    const auto mask = HotkeyManager::hotkeySequenceMask;
                    UnregisterHotkey();
                    OnHotkeyPressed();
                    RegisterHotkey(mask);
                });*/
                /*OnHotkeyPressed();*/
            }
            goto exit;
        }

        event = reinterpret_cast<MSLLHOOKSTRUCT *>(lParam);
        switch (wParam) {
            case WM_XBUTTONDOWN: {
                HotkeyPressed((event->mouseData >> 16) + 4, true);
                break;
            }
            case WM_MBUTTONDOWN:
                HotkeyPressed(VK_MBUTTON, true);
                break;
            case WM_RBUTTONDOWN:
                HotkeyPressed(VK_RBUTTON, true);
                break;

            case WM_LBUTTONDOWN:
            case WM_XBUTTONUP:
            case WM_MBUTTONUP:
            case WM_RBUTTONUP:
                ResetSequence();
                break;

        }

        exit: return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    static LRESULT CALLBACK KeyboardInterceptor(int code, WPARAM wParam, LPARAM lParam)
    {
        if (code == HC_ACTION)
        {
            switch (wParam)
            {
                case WM_KEYDOWN:
                case WM_SYSKEYDOWN:
                    //  std::cout << "keydown: " << reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam)->vkCode <<   std::endl;
                    HotkeyPressed(reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam)->vkCode);
                    break;
                case WM_KEYUP:
                    ResetSequence();
                    break;
            }

        }
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    static inline void ResetSequence() {

        // If binding mode we should disable hooks
        if(OnHotkeyBound) {
            OnHotkeyBound(currentSequenceMask);
            UnregisterHotkey();
        }

        // If it has already reset - exit
        if(currentSequenceMask == 0) {
            return;
        }

        codeNum = prevCode = currentSequenceMask = 0;
    }

    static void HotkeyPressed(WORD hotkeyVkCode, bool mouse = false) {
        if(prevCode == hotkeyVkCode || codeNum == sizeof(DWORD)){
            return;
        }

        // We don't need to save the previous code since the mouse doesn't send signals if the button is held down
        // If this is not done and the mouse button is the last one in the sequence,
        // we will get a duplicate button from the keyboard
        if(!mouse) {
            prevCode = hotkeyVkCode;
        }

        currentSequenceMask |= (hotkeyVkCode << ( codeNum * 8 ));
        codeNum++;

        if(OnHotkeyBound) {
            if(hotkeyVkCode == VK_ESCAPE) {
                codeNum = prevCode = currentSequenceMask = 0;
                ResetSequence();
                return;
            }
            OnHotkeyBound(currentSequenceMask);
            return;
        }

        if(currentSequenceMask == hotkeySequenceMask) {
            OnHotkeyPressed();
        }
    }

    static void RegisterHotkey(DWORD hotkeyMask) {
        bool mouseHooked = false, keybdHooked = false;
        UnregisterHotkey();
        HotkeyManager::hotkeySequenceMask = hotkeyMask;
        const auto& keys = GetKeys(hotkeyMask);

        for(const auto& key : keys) {
            if(!mouseHooked && (key == VK_MBUTTON || key == VK_XBUTTON1 || key == VK_XBUTTON2 || key == VK_RBUTTON)) {
                mouseWm = (key == VK_MBUTTON ? WM_MBUTTONUP :
                           (key == VK_RBUTTON ? WM_RBUTTONUP : WM_XBUTTONUP));
                mouseData = (key == VK_XBUTTON1  || key == VK_XBUTTON2)  ? (key - 4) : 0;
                mouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseInterceptor, nullptr, 0);
                mouseHooked = true;
                continue;
            }

            if(!keybdHooked) {
                keybdHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardInterceptor, nullptr, 0);
                keybdHooked = true;
            }
        }

        if(keybdHooked && mouseHooked) {
            mouseWm = 0;
        }
    }

    static void Bind(const std::function<void(DWORD)>& hotkeyBound)
    {
        UnregisterHotkey();
        OnHotkeyBound = hotkeyBound;
        mouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseInterceptor, nullptr, 0);
        keybdHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardInterceptor, nullptr, 0);
    }

    static std::vector<BYTE> GetKeys(DWORD hotkeyMask) {
        std::vector<BYTE> keys;
        for (int i = 0; i < sizeof(DWORD); ++i) {
            const auto& vkCode = (hotkeyMask >> (i * 8)) & 0xFF;

            if(!vkCode) {
                break;
            }

            keys.push_back(vkCode);
        }
        return keys;
    }

    static std::string GetSequenceName(DWORD sequence) {
        std::string sequenceName{};
        for (int i = 0; i < sizeof(DWORD); ++i) {
            const auto& vkCode = (sequence >> (i * 8)) & 0xFF;

            if(!vkCode) {
                break;
            }

            sequenceName.append(HotkeyManager::KeysNameTable[vkCode]).append(" + ");
        }
        return sequenceName.erase(sequenceName.size()-3);
    }

    static void UnregisterHotkey() {
        if(mouseHook != nullptr) {
            UnhookWindowsHookEx(mouseHook);
            mouseHook = nullptr;
        }

        if(keybdHook != nullptr) {
            UnhookWindowsHookEx(keybdHook);
            keybdHook = nullptr;
        }
        OnHotkeyBound = nullptr;
        hotkeySequenceMask = 0;
        mouseWm = 0;
        mouseData = 0;
    }

    //#region <== Names table ==>
public:
    static constexpr const char* KeysNameTable[] = {
            nullptr,             // 0x00
            "LButton",           // VK_LBUTTON 	0x01
            "Mouse1",            // VK_RBUTTON 	0x02
            "Cancel",            // VK_CANCEL 	0x03
            "Mouse2",            // VK_MBUTTON 	0x04
            "Mouse3",            // VK_XBUTTON1 	0x05
            "Mouse4",            // VK_XBUTTON2 	0x06
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
//#endregion
};

inline DWORD HotkeyManager::hotkeySequenceMask;
inline DWORD HotkeyManager::currentSequenceMask;
inline WORD HotkeyManager::mouseWm;
inline BYTE HotkeyManager::mouseData;
inline HHOOK HotkeyManager::keybdHook;
inline HHOOK HotkeyManager::mouseHook;
inline int HotkeyManager::codeNum;
inline BYTE HotkeyManager::prevCode;
inline std::function<void()> HotkeyManager::OnHotkeyPressed;
inline std::function<void(DWORD)> HotkeyManager::OnHotkeyBound;

#endif //EASYMIC_HOTKEYMANAGER_HPP
