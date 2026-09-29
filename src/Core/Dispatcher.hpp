#pragma once

#include <windows.h>

#include <functional>

/// Hook procs only look up and Post: Windows drops a hook that overruns LowLevelHooksTimeout.
/// Actions run on the worker; whatever touches a window or the config goes through ToUi.
namespace Dispatcher {

    bool Start();
    /// Drops what is queued and joins the worker. Safe when not running.
    void Stop();

    /// An empty action is ignored, so an unbound slot needs no check.
    void Post(std::function<void()> action);

    /// The bound window must route this message to RunPosted.
    inline constexpr UINT WM_DISPATCH_RUN = WM_APP + 20;

    void BindUi(HWND target);
    /// Dropped when no window is bound: at shutdown there is no loop left to run it.
    void ToUi(std::function<void()> action);
    void RunPosted(LPARAM lParam);
}
