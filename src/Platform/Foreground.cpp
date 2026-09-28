#include "Foreground.hpp"

#include <atomic>
#include <cstdint>
#include <cwchar>
#include <mutex>

#include "Dispatcher.hpp"
#include "Str.hpp"

namespace Foreground {

namespace {

    HWINEVENTHOOK _hook = nullptr;
    PTP_WORK _work = nullptr;
    /// Written on the UI thread only, so the hop below still orders a resolve against a newer focus
    std::atomic<std::shared_ptr<const Snapshot>> _snapshot;
    std::atomic<HWND> _latestHwnd{nullptr};
    std::atomic<uint64_t> _generation{0};
    std::atomic<bool> _running{false};
    std::mutex _lifetimeMutex;

    void _publish(HWND window, std::string exe, const bool fullscreen = false) {
        _snapshot.store(std::make_shared<const Snapshot>(Snapshot{window, std::move(exe), fullscreen}));
    }

    /// Borderless or exclusive. A maximized window fills the monitor too once the taskbar
    /// auto-hides, and the desktop always does - both are typed in.
    bool _fullscreen(const HWND window) {
        wchar_t name[16]{};
        GetClassNameW(window, name, 16);
        RECT rect{};
        MONITORINFO monitor{.cbSize = sizeof monitor};
        return !IsZoomed(window) && std::wcscmp(name, L"Progman") && std::wcscmp(name, L"WorkerW")
               && GetWindowRect(window, &rect)
               && GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONULL), &monitor)
               && EqualRect(&rect, &monitor.rcMonitor);
    }

    void CALLBACK _resolve(PTP_CALLBACK_INSTANCE, void*, PTP_WORK) {
        const HWND window = _latestHwnd.load(std::memory_order_relaxed);
        const uint64_t generation = _generation.load(std::memory_order_relaxed);
        if (!window) {
            return;
        }
        std::string exe = Str::WideToUtf8(ExeName(window));
        const bool fullscreen = _fullscreen(window);
        std::lock_guard lock(_lifetimeMutex);
        if (!_running.load(std::memory_order_relaxed)
            || _generation.load(std::memory_order_relaxed) != generation) {
            return;
        }
        // No GetForegroundWindow recheck: caught mid-switch (a virtual desktop), it left the window without its exe
        // until the next switch, and keyboard tracking off there
        Dispatcher::ToUi([window, generation, fullscreen, exe = std::move(exe)]() mutable {
            if (!_running.load(std::memory_order_relaxed)
                || _generation.load(std::memory_order_relaxed) != generation
                || _latestHwnd.load(std::memory_order_relaxed) != window) {
                return;
            }
            _publish(window, std::move(exe), fullscreen);
        });
    }

    void _focus(HWND window) {
        _latestHwnd.store(window, std::memory_order_relaxed);
        // Refocusing the same window: a resolve still in flight from before must not land last
        _generation.fetch_add(1, std::memory_order_relaxed);
        _publish(window, {});
        if (window) {
            SubmitThreadpoolWork(_work);
        }
    }

    void CALLBACK _onForeground(HWINEVENTHOOK, DWORD, HWND window, LONG, LONG, DWORD, DWORD) {
        _focus(window);
    }

} // anonymous namespace

bool Start() {
    if (_hook) {
        return false;
    }
    _work = CreateThreadpoolWork(_resolve, nullptr, nullptr);
    if (!_work) {
        return false;
    }
    _hook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                            nullptr, _onForeground, 0, 0, WINEVENT_OUTOFCONTEXT);
    if (!_hook) {
        CloseThreadpoolWork(_work);
        _work = nullptr;
        return false;
    }
    _generation.fetch_add(1, std::memory_order_relaxed);
    _running.store(true, std::memory_order_relaxed);
    _focus(GetForegroundWindow());
    return true;
}

void Stop() {
    if (_hook) {
        UnhookWinEvent(_hook);
        _hook = nullptr;
    }
    {
        std::lock_guard lock(_lifetimeMutex);
        _running.store(false, std::memory_order_relaxed);
        _generation.fetch_add(1, std::memory_order_relaxed);
        _latestHwnd.store(nullptr, std::memory_order_relaxed);
    }
    if (_work) {
        WaitForThreadpoolWorkCallbacks(_work, TRUE);
        CloseThreadpoolWork(_work);
        _work = nullptr;
    }
    _publish(nullptr, {});
}

std::shared_ptr<const Snapshot> Current() {
    return _snapshot.load();
}

std::wstring ExeName(HWND window) {
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return {};

    wchar_t path[1024];
    DWORD size = std::size(path);
    const BOOL found = QueryFullProcessImageNameW(process, 0, path, &size);
    CloseHandle(process);
    if (!found) return {};

    const std::wstring_view full(path, size);
    std::wstring name(full.substr(full.find_last_of(L'\\') + 1));
    CharLowerBuffW(name.data(), static_cast<DWORD>(name.size()));
    return name;
}

std::string CanonicalApp(std::string_view exe) {
    auto start = exe.find_first_not_of(' ');
    if (start == std::string_view::npos) return {};
    auto end = exe.find_last_not_of(' ');
    exe = exe.substr(start, end - start + 1);
    if (const auto separator = exe.find_last_of("/\\"); separator != std::string_view::npos) {
        exe.remove_prefix(separator + 1);
    }

    std::wstring wide = Str::Utf8ToWide(std::string(exe));
    if (!wide.empty()) {
        CharLowerBuffW(wide.data(), static_cast<DWORD>(wide.size()));
    }
    return Str::WideToUtf8(wide);
}

}
