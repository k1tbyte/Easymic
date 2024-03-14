#ifndef EASYMIC_HOTKEYMANAGER_H
#define EASYMIC_HOTKEYMANAGER_H

#include <functional>

#define HKModifier(h) ((HIBYTE (h) & 2) | ((HIBYTE (h) & 4) >> 2) | ((HIBYTE (h) & 1) << 2))
#define HotkeyUID 565746541

struct Hotkey {
    const char* Name;
    int VK;
    int WM;
};

class HotkeyManager final {

    static Hotkey* mouseHotkey;
    static HHOOK mouseHook;
    static HWND* hotkeyReceiver;
    static void (*OnHotkeyPressed)();
    static WORD keybdHotkey;
public:


    static void Initialize(HWND* receiver, void (*hotkeyPressed)()) {
        HotkeyManager::OnHotkeyPressed = hotkeyPressed;
        HotkeyManager::hotkeyReceiver = receiver;
    }

    constexpr static const Hotkey mouseHotkeys[] =  {
            { "NONE",0,0 },
            { "MBUTTON",VK_MBUTTON,WM_MBUTTONUP },
            { "RBUTTON", VK_RBUTTON, WM_RBUTTONUP},
            { "LBUTTON", VK_LBUTTON, WM_LBUTTONUP },
            { "XBUTTON1", VK_XBUTTON1, WM_XBUTTONUP },
            { "XBUTTON2", VK_XBUTTON2, WM_XBUTTONUP }
    };

    static LRESULT MouseHookCallback(int code, WPARAM wParam, LPARAM lParam) {
        if (code >= 0 && wParam == mouseHotkey->WM)
        {
            auto* event = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
            auto data = (event->mouseData >> 16);
            if(data == 0 || data == mouseHotkey->VK-4)
            {
                OnHotkeyPressed();
            }
        }

        return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    static bool RegisterMouseHotkey(Hotkey* hotkey) {
        if(mouseHook == nullptr) {
            mouseHook = SetWindowsHookEx(WH_MOUSE_LL,MouseHookCallback, nullptr, 0);
        }

        mouseHotkey = hotkey;
        return mouseHook != nullptr;
    }

    static bool RegisterKeyboardHotkey(WORD shortcut) {

        if(!hotkeyReceiver) {
            throw std::runtime_error("Hotkey receiver HWND is not specified");
        }

        HotkeyManager::UnregisterHotkey();
        HotkeyManager::keybdHotkey = shortcut;
        return RegisterHotKey(*hotkeyReceiver, HotkeyUID, HKModifier(shortcut), LOBYTE(shortcut));
    }

    static void UnregisterHotkey() {
        if(mouseHook != nullptr) {
            UnhookWindowsHookEx(mouseHook);
            mouseHook = nullptr;
        }

        if(keybdHotkey) {
            UnregisterHotKey(*hotkeyReceiver,HotkeyUID);
            keybdHotkey = 0;
        }
    }

};

#endif //EASYMIC_HOTKEYMANAGER_H
