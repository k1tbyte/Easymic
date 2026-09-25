#include "Input.hpp"

#include <atomic>
#include <memory>
#include <thread>

#include "Router.hpp"
#include "Win32Hook.hpp"
#include "definitions.h"

namespace Input {

namespace {

    using Work = std::move_only_function<void()>;

    /// A thread message: the input thread has no window for it to collide with.
    constexpr UINT WM_INPUT_RUN = WM_APP;

    /// Our events carry this above the low byte, and the sender's level in it.
    constexpr ULONG_PTR Signature = 0x454C'4C41'554E'4300ull;
    static_assert(sizeof(ULONG_PTR) == 8, "the signature needs a 64-bit dwExtraInfo");

    std::thread _thread;
    std::atomic<DWORD> _threadId{0};
    std::unique_ptr<Win32Hook> _keyboard;
    std::unique_ptr<Win32Hook> _mouse;

    uint8_t _level(const ULONG_PTR extra) {
        return (extra & ~ULONG_PTR{0xFF}) == Signature ? static_cast<uint8_t>(extra) : 0;
    }

    LRESULT CALLBACK _keyboardProc(const int code, const WPARAM wParam, const LPARAM lParam) {
        if (code == HC_ACTION) {
            const auto* info = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
            // The struct carries a raw DWORD, and injected input is free to put anything in it
            const bool down = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
            if (info->vkCode < 256
                && Router::Key(static_cast<uint8_t>(info->vkCode), down, false, _level(info->dwExtraInfo))) {
                return 1;
            }
        }
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    LRESULT CALLBACK _mouseProc(const int code, const WPARAM wParam, const LPARAM lParam) {
        if (code == HC_ACTION) {
            const auto* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
            uint8_t vk = 0;
            bool down = false;
            // WM_*BUTTON* and VK_*BUTTON do not line up, so this switch is the table. A move falls
            // through to the default: it is most of the traffic and nobody wants it
            switch (wParam) {
                case WM_LBUTTONDOWN: down = true; [[fallthrough]];
                case WM_LBUTTONUP: vk = VK_LBUTTON; break;
                case WM_RBUTTONDOWN: down = true; [[fallthrough]];
                case WM_RBUTTONUP: vk = VK_RBUTTON; break;
                case WM_MBUTTONDOWN: down = true; [[fallthrough]];
                case WM_MBUTTONUP: vk = VK_MBUTTON; break;
                case WM_XBUTTONDOWN: down = true; [[fallthrough]];
                case WM_XBUTTONUP: {
                    const WORD button = HIWORD(info->mouseData);
                    vk = button == XBUTTON1 ? VK_XBUTTON1 : button == XBUTTON2 ? VK_XBUTTON2 : 0;
                    break;
                }
                default: break;
            }
            if (vk && Router::Key(vk, down, true, _level(info->dwExtraInfo))) {
                return 1;
            }
        }
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    /// Puts the hook up or down to match what the stages want. True when that changed anything.
    bool _sync(std::unique_ptr<Win32Hook>& hook, const bool wanted, const int id, const HOOKPROC proc) {
        if (wanted == (hook != nullptr)) {
            return false;
        }
        if (!wanted) {
            hook = nullptr;
            return true;
        }
        auto created = Win32Hook::Create(id, proc, nullptr, 0);
        if (!created->IsValid()) {
            LOG_ERROR("SetWindowsHookEx(%d) failed: 0x%08lX", id, created->LastError());
            return false;
        }
        hook = std::move(created);
        return true;
    }

    void _reconcile() {
        const Wants needed = Router::Needed();
        const bool keyboard = _sync(_keyboard, needed & WantKeys, WH_KEYBOARD_LL, _keyboardProc);
        const bool mouse = _sync(_mouse, needed & WantButtons, WH_MOUSE_LL, _mouseProc);
        // A hook that was down missed downs and ups alike
        if (keyboard || mouse) {
            Router::Reset();
        }
    }

    /// Lock, Ctrl+Alt+Del and the UAC prompt run on the secure desktop, which no hook sees - the
    /// ups of whatever was held when it came up are gone.
    void CALLBACK _onDesktopSwitch(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
        Router::Reset();
    }

    void _run(const HANDLE ready) {
        MSG msg;
        // The queue exists from the first peek; a post before it would be lost
        PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        const HWINEVENTHOOK desktopSwitch = SetWinEventHook(
            EVENT_SYSTEM_DESKTOPSWITCH, EVENT_SYSTEM_DESKTOPSWITCH, nullptr, _onDesktopSwitch, 0, 0,
            WINEVENT_OUTOFCONTEXT);
        _threadId = GetCurrentThreadId();
        SetEvent(ready);

        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (msg.message == WM_INPUT_RUN) {
                const std::unique_ptr<Work> work(reinterpret_cast<Work*>(msg.lParam));
                (*work)();
            }
        }

        _threadId = 0;
        if (desktopSwitch) {
            UnhookWinEvent(desktopSwitch);
        }
        _keyboard = nullptr;
        _mouse = nullptr;
        while (PeekMessageW(&msg, nullptr, WM_INPUT_RUN, WM_INPUT_RUN, PM_REMOVE)) {
            delete reinterpret_cast<Work*>(msg.lParam);
        }
    }

} // anonymous namespace

void Add(const Stage& stage) {
    if (_thread.joinable() || !Router::Add(stage)) {
        LOG_ERROR("Input: stage '%.*s' refused", static_cast<int>(stage.Id.size()), stage.Id.data());
    }
}

bool Start() {
    if (_thread.joinable()) {
        return false;
    }
    const HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ready) {
        return false;
    }
    _thread = std::thread(_run, ready);
    WaitForSingleObject(ready, INFINITE);
    CloseHandle(ready);
    return true;
}

void Stop() {
    if (!_thread.joinable()) {
        return;
    }
    PostThreadMessageW(GetThreadId(_thread.native_handle()), WM_QUIT, 0, 0);
    _thread.join();
}

void Enable(const std::string_view id, const Wants wants) {
    if (const int stage = Router::Find(id); stage >= 0) {
        Post([stage, wants] {
            Router::Enable(stage, wants);
            _reconcile();
        });
    }
}

void Disable(const std::string_view id) {
    if (const int stage = Router::Find(id); stage >= 0) {
        Post([stage] {
            Router::Disable(stage);
            _reconcile();
        });
    }
}

void Post(Work work) {
    const DWORD thread = _threadId;
    if (!thread || !work) {
        return;
    }
    // Heap allocated to travel through the queue; the loop owns it from there
    auto* posted = new Work(std::move(work));
    if (!PostThreadMessageW(thread, WM_INPUT_RUN, 0, reinterpret_cast<LPARAM>(posted))) {
        delete posted;
    }
}

UINT Send(const std::span<INPUT> inputs, const std::string_view from) {
    const int stage = Router::Find(from);
    if (stage < 0 || inputs.empty()) {
        return 0;
    }

    const ULONG_PTR tag = Signature | static_cast<ULONG_PTR>(stage + 1);
    for (INPUT& input : inputs) {
        if (input.type == INPUT_KEYBOARD) {
            input.ki.dwExtraInfo = tag;
        } else if (input.type == INPUT_MOUSE) {
            input.mi.dwExtraInfo = tag;
        }
    }

    const UINT sent = SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    if (sent != inputs.size()) {
        LOG_WARNING("SendInput took %u of %zu events: 0x%08lX", sent, inputs.size(), GetLastError());
    }
    return sent;
}

}
