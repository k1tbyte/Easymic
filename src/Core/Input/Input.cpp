#include "Input.hpp"

#include <atomic>
#include <exception>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include "Hold.hpp"
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
    /// Edits and what a hold replays: past every stage.
    constexpr uint8_t EditLevel = 0xFF;
    constexpr UINT HoldTickMs = 50;

    std::thread _thread;
    std::atomic<DWORD> _threadId{0};
    std::unique_ptr<Win32Hook> _keyboard;
    std::unique_ptr<Win32Hook> _mouse;

    UINT_PTR _holdTimer = 0;
    /// Outgoing swaps buffers with the hold, so it reserves what the hold may fill
    std::vector<INPUT> _outgoing = [] {
        std::vector<INPUT> outgoing;
        outgoing.reserve(Hold::Capacity * 4);
        return outgoing;
    }();
    bool _flushing = false;

    struct EditJob {
        HoldId Id;
        std::function<void()> Work;
    };
    /// Lane callbacks still running. Stop waits for them: they read state main destroys next.
    std::atomic<int> _edits{0};
    thread_local HoldId _currentHold = 0;
    /// Input thread: what runs once the hold's edit is sent. One hold at a time, so one slot.
    HoldId _landedHold = 0;
    std::function<void()> _landed;

    uint8_t _level(const ULONG_PTR extra) {
        return (extra & ~ULONG_PTR{0xFF}) == Signature ? static_cast<uint8_t>(extra) : 0;
    }

    UINT _send(const std::span<INPUT> inputs, const uint8_t level) {
        const ULONG_PTR tag = Signature | level;
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

    /// Sends what the hold has due, and keeps its timer running exactly while it is up.
    void _flush() {
        // Our SendInput re-enters the hook; the loop here picks up whatever that made due
        if (!_flushing) {
            _flushing = true;
            while (Hold::Outgoing(_outgoing)) {
                Hold::Unsent(_outgoing.size() - _send(_outgoing, EditLevel));
            }
            _flushing = false;
        }

        const bool active = Hold::Active();
        if (active && !_holdTimer) {
            _holdTimer = SetTimer(nullptr, 0, HoldTickMs, nullptr);
        } else if (!active && _holdTimer) {
            KillTimer(nullptr, _holdTimer);
            _holdTimer = 0;
        }
    }

    /// Our own event came back through the hook. Only the events a hold sends carry EditLevel.
    void _arrived() {
        Hold::Arrived();
        _flush();
    }

    /// What the hold keeps goes out now, edit or not: one tick expires it, the next stops waiting for its echo.
    void _release() {
        for (int phase = 0; phase < 2; ++phase) {
            Hold::Tick(UINT64_MAX);
            _flush();
        }
    }

    void _hold(const INPUT& event) {
        Hold::Take(event, GetTickCount64());
        _flush();
    }

    INPUT _keyInput(const KBDLLHOOKSTRUCT& info, const bool down) {
        // Someone else's Unicode injection: the character rides in the scan code
        const bool unicode = info.vkCode == VK_PACKET;
        INPUT input{.type = INPUT_KEYBOARD};
        input.ki = {.wVk = static_cast<WORD>(unicode ? 0 : info.vkCode),
                    .wScan = static_cast<WORD>(info.scanCode),
                    .dwFlags = (down ? 0u : KEYEVENTF_KEYUP)
                               | (info.flags & LLKHF_EXTENDED ? KEYEVENTF_EXTENDEDKEY : 0u)
                               | (unicode ? KEYEVENTF_UNICODE : 0u)};
        return input;
    }

    /// No coordinates: a held click lands wherever the cursor is when it goes out.
    INPUT _buttonInput(const uint8_t vk, const bool down) {
        INPUT input{.type = INPUT_MOUSE};
        switch (vk) {
            case VK_LBUTTON: input.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP; break;
            case VK_RBUTTON: input.mi.dwFlags = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP; break;
            case VK_MBUTTON: input.mi.dwFlags = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP; break;
            default:
                input.mi.dwFlags = down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
                input.mi.mouseData = vk == VK_XBUTTON1 ? XBUTTON1 : XBUTTON2;
                break;
        }
        return input;
    }

    LRESULT CALLBACK _keyboardProc(const int code, const WPARAM wParam, const LPARAM lParam) {
        if (code == HC_ACTION) {
            const auto* info = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
            // The struct carries a raw DWORD, and injected input is free to put anything in it
            const bool down = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
            const uint8_t level = _level(info->dwExtraInfo);
            if (level == EditLevel) {
                _arrived();
            } else if (info->vkCode < 256
                       && Router::Key(static_cast<uint8_t>(info->vkCode), down, false, level)) {
                return 1;
            } else if (Hold::Active()) {
                _hold(_keyInput(*info, down));
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
            if (const uint8_t level = _level(info->dwExtraInfo); level == EditLevel) {
                _arrived();
            } else if (!vk) {
                // Moves and the wheel are never held
            } else if (Router::Key(vk, down, true, level)) {
                return 1;
            } else if (Hold::Active()) {
                _hold(_buttonInput(vk, down));
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
        // The secure desktop would only eat what is held
        _release();
    }

    void CALLBACK _runEdit(PTP_CALLBACK_INSTANCE, void* context) {
        {
            const std::unique_ptr<EditJob> job(static_cast<EditJob*>(context));
            _currentHold = job->Id;
            try {
                job->Work();
            } catch (const std::exception& e) {
                LOG_ERROR("Input: edit threw: %s", e.what());
            } catch (...) {
                LOG_ERROR("Input: edit threw an unknown exception");
            }
            _currentHold = 0;
            // Queued behind the work's own Commit, if it posted one - that one takes the hold first
            Post([id = job->Id] { Commit(id, {}); });
        }
        --_edits;
        _edits.notify_all();
    }

    void _startEdit(std::function<void()> work, std::function<void()> landed) {
        const HoldId id = Hold::Begin(GetTickCount64());
        if (!id) {
            return;
        }
        _landedHold = id;
        _landed = std::move(landed);
        Router::NotifyHold(id);
        _flush();

        auto* job = new EditJob{id, std::move(work)};
        ++_edits;
        if (!TrySubmitThreadpoolCallback(_runEdit, job, nullptr)) {
            delete job;
            --_edits;
            Commit(id, {});
        }
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
            } else if (msg.message == WM_TIMER) {
                Hold::Tick(GetTickCount64());
                _flush();
            }
        }

        _threadId = 0;
        if (desktopSwitch) {
            UnhookWinEvent(desktopSwitch);
        }
        // Rather than lost, since the hooks go down next
        _release();
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
    for (int left; (left = _edits.load());) {
        _edits.wait(left);
    }
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
    return _send(inputs, static_cast<uint8_t>(stage + 1));
}

void Edit(std::function<void()> work, std::function<void()> landed) {
    if (!work) {
        return;
    }
    if (GetCurrentThreadId() == _threadId) {
        _startEdit(std::move(work), std::move(landed));
    } else {
        Post([work = std::move(work), landed = std::move(landed)]() mutable {
            _startEdit(std::move(work), std::move(landed));
        });
    }
}

HoldId CurrentHold() {
    return _currentHold;
}

bool Commit(const HoldId id, const std::span<const INPUT> edit) {
    // A refused commit may have just expired the hold, and what it held is due now
    const bool taken = Hold::Commit(id, edit, GetTickCount64());
    _flush();
    if (id == _landedHold) {
        _landedHold = 0;
        if (const auto landed = std::exchange(_landed, {}); landed && taken && !edit.empty()) {
            landed();
        }
    }
    return taken;
}

}
