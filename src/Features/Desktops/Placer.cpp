#include "Placer.hpp"

#include "AppConfig.hpp"
#include "Str.hpp"
#include "VirtualDesktops.hpp"
#include "WindowList.hpp"

#include <algorithm>
#include <functional>
#include <memory>

namespace {

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

    std::shared_ptr<const Rules> _rules = std::make_shared<Rules>();
    HWND _listener = nullptr;
    UINT _shellMessage = 0;
    bool _watching = false;

    void _submit(std::function<void()> work) {
        auto* task = new std::function<void()>(std::move(work));
        if (!TrySubmitThreadpoolCallback([](PTP_CALLBACK_INSTANCE, void* context) {
                const std::unique_ptr<std::function<void()>> owned(static_cast<std::function<void()>*>(context));
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

    /// The preset's missing desktops up to count, named as it says.
    void _grow(const Rules& rules, const int count) {
        const int had = VirtualDesktops::Count();
        if (had >= count || !VirtualDesktops::Grow(count)) {
            return;
        }
        for (int i = had; i < count && i < static_cast<int>(rules.Names.size()); ++i) {
            if (!rules.Names[i].empty()) {
                VirtualDesktops::Rename(i, rules.Names[i]);
            }
        }
    }

    void _placeNew(const Rules& rules, const HWND window) {
        const std::wstring exe = WindowList::ExeName(window);
        const App* app = rules.Find(exe);
        if (!app) {
            return;
        }
        // Qt apps keep a new window cloaked while they set it up - Telegram for about a second
        for (int waited = 0; !WindowList::IsAppWindow(window); ++waited) {
            if (waited == 30) {
                return;
            }
            Sleep(100);
        }

        // Counted so a second window of the app does not stack on the first one
        size_t open = 0;
        for (const HWND other : WindowList::AppWindows()) {
            open += other != window && WindowList::ExeName(other) == exe;
        }
        if (open >= app->Places.size()) {
            return;
        }
        const Placement& where = app->Places[open];

        _place(window, where);
        _grow(rules, where.Desktop + 1);
        const bool follow = rules.Follow && GetForegroundWindow() == window;
        // Give a newly opened window time to take focus before moving it away when Follow is on.
        const bool moved = (!rules.Follow || follow) && VirtualDesktops::MoveWindow(window, where.Desktop);
        const bool followed = moved && follow && VirtualDesktops::Switch(where.Desktop);

        Sleep(300);
        _place(window, where);
        if (VirtualDesktops::MoveWindow(window, where.Desktop)
            && !followed && rules.Follow && GetForegroundWindow() == window) {
            VirtualDesktops::Switch(where.Desktop);
        }

        Sleep(700);
        if (IsWindow(window)) {
            _place(window, where);
            VirtualDesktops::MoveWindow(window, where.Desktop);
        }
    }

    LRESULT CALLBACK _listen(const HWND hwnd, const UINT message, const WPARAM wParam, const LPARAM lParam) {
        if (message == _shellMessage && wParam == HSHELL_WINDOWCREATED) {
            _submit([rules = _rules, window = reinterpret_cast<HWND>(lParam)] { _placeNew(*rules, window); });
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
            for (const HWND window : WindowList::AppWindows()) {
                if (const App* app = rules->Find(WindowList::ExeName(window))) {
                    if (size_t& next = taken[app - rules->Apps.data()]; next < app->Places.size()) {
                        moves.emplace_back(window, &app->Places[next++]);
                    }
                }
            }

            VirtualDesktops::Grow(static_cast<int>(rules->Names.size()));
            for (int i = 0; i < static_cast<int>(rules->Names.size()); ++i) {
                if (!rules->Names[i].empty()) {
                    VirtualDesktops::Rename(i, rules->Names[i]);
                }
            }

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
}
