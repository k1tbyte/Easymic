#include "Desktops.hpp"

#include "AppConfig.hpp"
#include "Core/ActionRegistry.hpp"
#include "Core/Dispatcher.hpp"
#include "Core/Lifecycle.hpp"
#include "Features/Desktops/Settings/Page.hpp"
#include "Features/Desktops/Placement/Placer.hpp"
#include "Str.hpp"
#include "Features/Desktops/Placement/Target.hpp"
#include "Tracker.hpp"
#include "VirtualDesktops.hpp"

#include <deque>
#include <functional>
#include <mutex>
#include <utility>

namespace {

    AppConfig* _config = nullptr;

    /// Explorer calls and the switch animation run here in press order, off the action worker that mute and volume share:
    /// a step or a name resolves against what the last press left.
    struct Lane {
        std::mutex Lock;
        std::deque<std::function<void()>> Jobs;
        bool Draining = false;
    };

    // Never destroyed: a job can still run while statics tear down at exit
    Lane& _lane = *new Lane;

    void _drain(PTP_CALLBACK_INSTANCE instance, void*) {
        CallbackMayRunLong(instance);
        for (;;) {
            std::function<void()> job;
            {
                std::lock_guard lock(_lane.Lock);
                if (_lane.Jobs.empty()) {
                    _lane.Draining = false;
                    return;
                }
                job = std::move(_lane.Jobs.front());
                _lane.Jobs.pop_front();
            }
            job();
        }
    }

    void _enqueue(std::function<void()> job) {
        {
            std::lock_guard lock(_lane.Lock);
            _lane.Jobs.push_back(std::move(job));
            if (std::exchange(_lane.Draining, true)) {
                return;
            }
        }
        if (!TrySubmitThreadpoolCallback(_drain, nullptr, nullptr)) {
            std::lock_guard lock(_lane.Lock);
            _lane.Draining = false;
        }
    }

    /// The foreground window is read at the press: by the time the lane runs, focus may have moved.
    ActionFn _queued(std::function<void(HWND foreground)> job) {
        return [job = std::move(job)] { _enqueue([job, foreground = GetForegroundWindow()] { job(foreground); }); };
    }

    int _resolve(const Desktops::Target& target) {
        if (target.Step != 0) {
            const int current = VirtualDesktops::Current();
            const int to = current + target.Step;
            return current >= 0 && to >= 0 && to < VirtualDesktops::Count() ? to : -1;
        }
        if (!target.Name.empty()) {
            const std::wstring name = Str::Utf8ToWide(target.Name);
            const std::vector<std::wstring> names = VirtualDesktops::Names();
            for (size_t i = 0; i < names.size(); ++i) {
                if (CompareStringOrdinal(names[i].c_str(), -1, name.c_str(), -1, TRUE) == CSTR_EQUAL) {
                    return static_cast<int>(i);
                }
            }
            return -1;
        }
        return target.Index;
    }

    ActionFn _bind(const ActionContext& context, void (*run)(int index, HWND foreground)) {
        const auto target = Desktops::ParseTarget(context.Args);
        if (!target) {
            return {};
        }
        return _queued([target = *target, run](const HWND foreground) {
            if (const int index = _resolve(target); index >= 0) {
                run(index, foreground);
            }
        });
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
             return _bind(context, [](const int index, HWND) { VirtualDesktops::Switch(index); });
         }},

        {.Id = "vd.move_window",
         .Title = "Move window to desktop",
         .Group = "Desktops",
         .ArgsLabel = TargetLabel,
         .ArgsHint = TargetHint,
         .Make = [](const ActionContext& context) {
             return _bind(context, [](const int index, const HWND window) {
                 VirtualDesktops::MoveWindow(window, index);
             });
         }},

        {.Id = "vd.move_window_follow",
         .Title = "Move window and switch",
         .Group = "Desktops",
         .ArgsLabel = TargetLabel,
         .ArgsHint = TargetHint,
         .Make = [](const ActionContext& context) {
             return _bind(context, [](const int index, const HWND window) {
                 if (VirtualDesktops::MoveWindow(window, index)) {
                     VirtualDesktops::Switch(index);
                 }
             });
         }},

        {.Id = "vd.pin_window",
         .Title = "Show window on all desktops",
         .Group = "Desktops",
         .Make = [](const ActionContext&) -> ActionFn {
             return _queued([](const HWND window) { VirtualDesktops::TogglePinned(window); });
         }},

        {.Id = "vd.apply",
         .Title = "Arrange windows",
         .Group = "Desktops",
         .Make = [](const ActionContext&) -> ActionFn {
             return [] { Dispatcher::ToUi(&Placer::PlaceAll); };
         }},

        {.Id = "vd.remember_window",
         .Title = "Remember window place",
         .Group = "Desktops",
         .DefaultNotification = ActionRegistry::DefaultNotification,
         .Make = [](const ActionContext&) -> ActionFn {
             return _queued([](const HWND window) {
                 if (const int desktop = VirtualDesktops::Current(); desktop >= 0) {
                     Dispatcher::ToUi([window, desktop] {
                         if (Placer::Remember(_config->Desktops, window, desktop)) {
                             _config->Save();
                         }
                     });
                 }
             });
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
