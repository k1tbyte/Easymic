#pragma once

#include <windows.h>

#include <functional>

/**
 * @brief The worker thread every action runs on, and the way back to the UI thread.
 *
 * The threading contract of the whole app lives here:
 *
 *  - **LL hook procs** look a mask up and Post. Nothing else. A proc that overruns
 *    LowLevelHooksTimeout is dropped by Windows and input stalls desktop-wide.
 *  - **The worker** is where action bodies run - playing a sound, launching a command, talking
 *    to WASAPI. It is the thread a feature's callback lands on.
 *  - **The UI thread** owns every window and the config. Anything on the worker that needs to
 *    touch either goes through ToUi.
 */
namespace Dispatcher {

    /// Starts the worker. False when it was already running.
    bool Start();

    /// Drops whatever is queued or waiting and joins the worker. Safe when not running.
    void Stop();

    /// Queues an action. An empty function is ignored, so an unbound slot needs no check of its own.
    void Post(std::function<void()> action);

    /// Posted by ToUi. The bound window has to route this message to RunPosted.
    inline constexpr UINT WM_DISPATCH_RUN = WM_APP + 20;

    /// The window whose message loop ToUi borrows. Set once, by the window that owns it.
    void BindUi(HWND target);

    /// Runs the action on the UI thread. Silently drops it when no window is bound - which is the
    /// shutdown case, where there is no loop left to run it on.
    void ToUi(std::function<void()> action);

    /// The UI side of ToUi: the bound window hands WM_DISPATCH_RUN's lParam back here.
    void RunPosted(LPARAM lParam);
}
