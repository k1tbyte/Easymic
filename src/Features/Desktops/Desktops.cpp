#include "Desktops.hpp"

#include "AppConfig.hpp"
#include "Core/ActionRegistry.hpp"
#include "Core/Dispatcher.hpp"
#include "Core/Lifecycle.hpp"
#include "Page.hpp"
#include "Placer.hpp"
#include "Str.hpp"
#include "Tracker.hpp"
#include "VirtualDesktops.hpp"
#include "WindowList.hpp"

#include <charconv>
#include <climits>
#include <cstdlib>
#include <optional>
#include <utility>

namespace {

    AppConfig* _config = nullptr;

    /// The desktop an action points at: a number from 1, a name, or a step from the current one.
    struct Target {
        int Index = -1;
        int Step = 0;
        std::wstring Name;
    };

    std::optional<Target> _parse(const std::string& args) {
        if (args.empty()) {
            return std::nullopt;
        }
        if (args == "next") {
            return Target{.Step = 1};
        }
        if (args == "prev") {
            return Target{.Step = -1};
        }

        int number = 0;
        const char* const end = args.data() + args.size();
        if (const auto [at, error] = std::from_chars(args.data(), end, number); at == end && error == std::errc{}) {
            return number >= 1 ? std::optional(Target{.Index = number - 1}) : std::nullopt;
        }
        return Target{.Name = Str::Utf8ToWide(args)};
    }

    /// At fire time: names and steps follow whatever the desktops are by then.
    int _resolve(const Target& target) {
        if (target.Step != 0) {
            const int current = VirtualDesktops::Current();
            const int to = current + target.Step;
            return current >= 0 && to >= 0 && to < VirtualDesktops::Count() ? to : -1;
        }
        if (!target.Name.empty()) {
            for (int i = 0, count = VirtualDesktops::Count(); i < count; ++i) {
                if (CompareStringOrdinal(VirtualDesktops::Name(i).c_str(), -1,
                                         target.Name.c_str(), -1, TRUE) == CSTR_EQUAL) {
                    return i;
                }
            }
            return -1;
        }
        return target.Index;
    }

    ActionFn _bind(const ActionContext& context, void (*run)(int index)) {
        const auto target = _parse(context.Args);
        if (!target) {
            return {};
        }
        return [target = *target, run] {
            if (const int index = _resolve(target); index >= 0) {
                run(index);
            }
        };
    }

    /**
     * @brief UI thread - the config is its.
     *
     * A window has no identity that outlives it, so which of the app's rules it owns is a guess:
     * with fewer rules on the desktop than the app has windows there, it gets one of its own,
     * otherwise it takes over the one nearest to where it sits.
     */
    void _remember(const HWND window, const int desktop) {
        const std::wstring exe = WindowList::ExeName(window);
        if (exe.empty() || !WindowList::IsAppWindow(window)) {
            return;
        }
        const WindowRule captured = WindowList::Capture(window);

        size_t open = 0;
        for (const HWND other : WindowList::AppWindows()) {
            open += WindowList::IsOnCurrentDesktop(other) && WindowList::ExeName(other) == exe;
        }

        auto& presets = _config->Desktops.Presets;
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

        _config->Save();
        Placer::Load(_config->Desktops);
    }

    constexpr std::string_view TargetLabel = "Desktop";
    constexpr std::string_view TargetHint = "number from 1, name, next or prev";

    constexpr ActionDesc Actions[] = {
        {.Id = "vd.switch",
         .Title = "Switch desktop",
         .Group = "Desktops",
         .ArgsLabel = TargetLabel,
         .ArgsHint = TargetHint,
         .Make = [](const ActionContext& context) {
             return _bind(context, [](const int index) { VirtualDesktops::Switch(index); });
         }},

        {.Id = "vd.move_window",
         .Title = "Move window to desktop",
         .Group = "Desktops",
         .ArgsLabel = TargetLabel,
         .ArgsHint = TargetHint,
         .Make = [](const ActionContext& context) {
             return _bind(context, [](const int index) {
                 VirtualDesktops::MoveWindow(GetForegroundWindow(), index);
             });
         }},

        {.Id = "vd.move_window_follow",
         .Title = "Move window and switch",
         .Group = "Desktops",
         .ArgsLabel = TargetLabel,
         .ArgsHint = TargetHint,
         .Make = [](const ActionContext& context) {
             return _bind(context, [](const int index) {
                 if (VirtualDesktops::MoveWindow(GetForegroundWindow(), index)) {
                     VirtualDesktops::Switch(index);
                 }
             });
         }},

        {.Id = "vd.pin_window",
         .Title = "Show window on all desktops",
         .Group = "Desktops",
         .Make = [](const ActionContext&) -> ActionFn {
             return [] { VirtualDesktops::TogglePinned(GetForegroundWindow()); };
         }},

        {.Id = "vd.apply",
         .Title = "Arrange windows",
         .Group = "Desktops",
         .Make = [](const ActionContext&) -> ActionFn {
             return [] { Dispatcher::ToUi(&Placer::PlaceAll); };
         }},

        // Read here, when the key goes down: by the time the UI thread runs, focus may have moved
        {.Id = "vd.remember_window",
         .Title = "Remember window place",
         .Group = "Desktops",
         .DefaultNotification = ActionRegistry::DefaultNotification,
         .Make = [](const ActionContext&) -> ActionFn {
             return [] {
                 const HWND window = GetForegroundWindow();
                 if (const int desktop = VirtualDesktops::Current(); desktop >= 0) {
                     Dispatcher::ToUi([window, desktop] { _remember(window, desktop); });
                 }
             };
         }},
    };

} // anonymous namespace

namespace Desktops {

    void Register(Host& host) {
        _config = &host.Config;
        for (const auto& desc : Actions) {
            ActionRegistry::Add(desc);
        }
        Page::Register(host.Config);
        Tracker::Start(host.Fb, host.Config.Desktops);

        Lifecycle::Restore += [] {
            Placer::Load(_config->Desktops);
            static bool started = false;
            if (!std::exchange(started, true) && _config->Desktops.ApplyOnStartup) {
                Placer::PlaceAll();
            }
        };
    }
}
