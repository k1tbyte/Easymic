#include "Placer.hpp"

#include "AppConfig.hpp"
#include "Str.hpp"
#include "Features/Desktops/VirtualDesktops.hpp"
#include "Windowing/WindowCatalog.hpp"
#include "WindowList.hpp"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>

namespace {

    constexpr int AppWindowWaitMs = 3000;
    constexpr int AppWindowStepMs = 100;
    constexpr int SettleDelaysMs[] = {0, 300, 700};

    struct Placement {
        int Desktop = 0;
        /// Empty picks the desktop only.
        RECT Frame{};
        bool Maximized = false;
    };

    struct App {
        std::wstring Exe;
        /// The app's k-th open window takes the k-th one.
        std::vector<Placement> Places;
    };

    /// The preset as one Load read it, shared read-only with every placement still running.
    struct Rules {
        std::vector<App> Apps;
        std::vector<std::wstring> Names;
        bool Follow = false;

        const App* Find(const std::wstring& exe) const {
            const auto found = std::ranges::find(Apps, exe, &App::Exe);
            return found != Apps.end() ? &*found : nullptr;
        }
    };

    /// Windows still to count their siblings: siblings skip them, or two that appear together each count the other and share a slot.
    struct Arrivals {
        std::mutex Lock;
        std::vector<HWND> Uncounted;
    };

    // Never destroyed: a placement can still run while statics tear down at exit
    Arrivals& _arrivals = *new Arrivals;

    std::shared_ptr<const Rules> _rules = std::make_shared<Rules>();
    HWND _listener = nullptr;
    UINT _shellMessage = 0;
    bool _watching = false;

    void _submit(std::function<void()> work) {
        auto* task = new std::function<void()>(std::move(work));
        if (!TrySubmitThreadpoolCallback([](PTP_CALLBACK_INSTANCE instance, void* context) {
                const std::unique_ptr<std::function<void()>> owned(static_cast<std::function<void()>*>(context));
                // It sleeps for seconds on a window: the pool is not to wait for it
                CallbackMayRunLong(instance);
                (*owned)();
            }, task, nullptr)) {
            delete task;
        }
    }

    void _place(const HWND window, const Placement& where) {
        if (!IsRectEmpty(&where.Frame)) {
            WindowList::Place(window, where.Frame, where.Maximized);
        }
    }

    void _grow(const Rules& rules, const int count) {
        const int had = VirtualDesktops::Count();
        if (had >= count || !VirtualDesktops::Grow(count)) {
            return;
        }
        const int named = std::min(count, static_cast<int>(rules.Names.size()));
        if (had < named) {
            VirtualDesktops::Rename(std::span(rules.Names).subspan(had, named - had), had);
        }
    }

    /// Qt apps keep a new window cloaked while they set it up - Telegram for about a second.
    bool _becomesAppWindow(const HWND window) {
        for (int waited = 0; waited <= AppWindowWaitMs; waited += AppWindowStepMs) {
            if (WindowCatalog::IsAppWindow(window)) {
                return true;
            }
            Sleep(AppWindowStepMs);
        }
        return false;
    }

    const Placement* _claimPlacement(const Rules& rules, const HWND window) {
        const std::wstring exe = WindowCatalog::ExeName(window);
        const App* app = rules.Find(exe);
        const bool real = app && _becomesAppWindow(window);

        std::lock_guard lock(_arrivals.Lock);
        std::erase(_arrivals.Uncounted, window);
        if (!real) {
            return nullptr;
        }

        size_t open = 0;
        for (const HWND other : WindowCatalog::AppWindows()) {
            open += other != window && WindowCatalog::ExeName(other) == exe
                    && std::ranges::find(_arrivals.Uncounted, other) == _arrivals.Uncounted.end();
        }
        return open < app->Places.size() ? &app->Places[open] : nullptr;
    }

    void _placeNew(const Rules& rules, const HWND window) {
        const Placement* where = _claimPlacement(rules, window);
        if (!where) {
            return;
        }

        _grow(rules, where->Desktop + 1);
        bool followed = false;
        for (const int delayMs : SettleDelaysMs) {
            Sleep(delayMs);
            if (!IsWindow(window)) {
                return;
            }

            _place(window, *where);
            // Follow needs the window in front, which a new one takes a moment to get
            if (delayMs == 0 && rules.Follow && GetForegroundWindow() != window) {
                continue;
            }
            const bool moved = VirtualDesktops::MoveWindow(window, where->Desktop);
            if (moved && rules.Follow && !followed && GetForegroundWindow() == window) {
                followed = VirtualDesktops::Switch(where->Desktop);
            }
        }
    }

    LRESULT CALLBACK _listen(const HWND hwnd, const UINT message, const WPARAM wParam, const LPARAM lParam) {
        if (message == _shellMessage && wParam == HSHELL_WINDOWCREATED) {
            const auto window = reinterpret_cast<HWND>(lParam);
            {
                std::lock_guard lock(_arrivals.Lock);
                _arrivals.Uncounted.push_back(window);
            }
            _submit([rules = _rules, window] { _placeNew(*rules, window); });
            return 0;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    void _watch(const bool on) {
        if (on == _watching) {
            return;
        }
        if (!on) {
            DeregisterShellHookWindow(_listener);
            _watching = false;
            return;
        }

        if (!_listener) {
            const WNDCLASSW windowClass{.lpfnWndProc = _listen,
                                        .hInstance = GetModuleHandleW(nullptr),
                                        .lpszClassName = L"EasyLauncher.Desktops"};
            RegisterClassW(&windowClass);
            _shellMessage = RegisterWindowMessageW(L"SHELLHOOK");
            _listener = CreateWindowExW(0, windowClass.lpszClassName, nullptr, 0, 0, 0, 0, 0,
                                        HWND_MESSAGE, nullptr, windowClass.hInstance, nullptr);
        }
        _watching = _listener && RegisterShellHookWindow(_listener);
    }

} // anonymous namespace

namespace Placer {

    void Load(const DesktopSettings& settings) {
        auto rules = std::make_shared<Rules>();
        rules->Follow = settings.Follow;

        for (int desktop = 0; desktop < static_cast<int>(settings.Presets.size()); ++desktop) {
            const DesktopPreset& preset = settings.Presets[desktop];
            rules->Names.push_back(Str::Utf8ToWide(preset.Name));

            for (const WindowRule& rule : preset.Windows) {
                const std::wstring exe = Str::Lower(Str::Utf8ToWide(rule.Exe));
                if (exe.empty()) {
                    continue;
                }
                auto app = std::ranges::find(rules->Apps, exe, &App::Exe);
                if (app == rules->Apps.end()) {
                    app = rules->Apps.insert(app, App{.Exe = exe});
                }
                app->Places.push_back({.Desktop = desktop,
                                       .Frame = {rule.X, rule.Y, rule.X + rule.Width, rule.Y + rule.Height},
                                       .Maximized = rule.Maximized});
            }
        }

        _watch(settings.Watch && !rules->Apps.empty());
        _rules = std::move(rules);
    }

    void PlaceAll() {
        _submit([rules = _rules] {
            std::vector<size_t> taken(rules->Apps.size());
            std::vector<std::pair<HWND, const Placement*>> moves;
            for (const HWND window : WindowCatalog::AppWindows()) {
                if (const App* app = rules->Find(WindowCatalog::ExeName(window))) {
                    if (size_t& next = taken[app - rules->Apps.data()]; next < app->Places.size()) {
                        moves.emplace_back(window, &app->Places[next++]);
                    }
                }
            }

            VirtualDesktops::Grow(static_cast<int>(rules->Names.size()));
            VirtualDesktops::Rename(rules->Names);

            for (const auto& [window, where] : moves) {
                _place(window, *where);
                VirtualDesktops::MoveWindow(window, where->Desktop);
            }
            // A window that was maximized only shows its borders once restored
            Sleep(300);
            for (const auto& [window, where] : moves) {
                _place(window, *where);
            }
        });
    }

    bool Remember(DesktopSettings& settings, const HWND window, const int desktop) {
        const std::wstring exe = WindowCatalog::ExeName(window);
        if (exe.empty() || !WindowCatalog::IsAppWindow(window)) {
            return false;
        }
        const WindowRule captured = WindowList::Capture(window);

        size_t open = 0;
        for (const HWND other : WindowCatalog::AppWindows()) {
            open += WindowList::IsOnCurrentDesktop(other) && WindowCatalog::ExeName(other) == exe;
        }

        auto& presets = settings.Presets;
        if (presets.size() <= static_cast<size_t>(desktop)) {
            presets.resize(desktop + 1);
        }
        auto& windows = presets[desktop].Windows;

        WindowRule* nearest = nullptr;
        size_t owned = 0;
        long best = LONG_MAX;
        for (WindowRule& rule : windows) {
            if (rule.Exe != captured.Exe) {
                continue;
            }
            ++owned;
            const long distance = std::abs(rule.X + rule.Width / 2 - captured.X - captured.Width / 2)
                                  + std::abs(rule.Y + rule.Height / 2 - captured.Y - captured.Height / 2);
            if (distance < best) {
                best = distance;
                nearest = &rule;
            }
        }

        if (nearest && owned >= open) {
            *nearest = captured;
        } else {
            windows.push_back(captured);
        }

        Load(settings);
        return true;
    }
}
