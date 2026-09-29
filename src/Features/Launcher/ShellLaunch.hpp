#pragma once

#include <windows.h>

#include <string>
#include <vector>

/**
 * @brief Launching through explorer.exe instead of out of this process, and putting what comes
 *        up in front.
 *
 * A child inherits the token of whoever started it, and EasyLauncher runs elevated whenever the user
 * asked it to skip the UAC prompt - which would hand administrator rights to every bound app,
 * silently and for no reason. The desktop shell is the one process that can start it as the user
 * instead.
 *
 * Nothing about the launch grants it the foreground, though. That belongs to whoever the user
 * last dealt with, so the window has to be raised from here once it exists.
 */
namespace ShellLaunch {

    /// What the user could switch to right now, so a launch can tell afterwards what is new.
    std::vector<HWND> Switchable();

    /**
     * @brief Brings up the window the launch produced, when it did not come up on its own.
     *
     * An app that was already running gets the foreground from nobody - nothing started it, so
     * its own request is refused and Windows flashes its taskbar button instead. That is what a
     * terminal asked for a second window does, and raising it from here is the only way a hotkey
     * launcher can put a window it did not create in front.
     *
     * Gives up as soon as the foreground moves anywhere at all, so a user who switches away mid
     * launch is never yanked back, and an app that came up by itself is left alone.
     */
    void RaiseNew(const std::vector<HWND>& before, HWND wasInFront);

    /**
     * @brief Runs the command as the logged on user.
     *
     * The shell reports a bad command to the user itself, so unlike ShellExecuteEx there is
     * nothing to log here - success only says the request reached explorer.exe.
     *
     * @return false when the shell is out of reach: no desktop yet, or explorer restarting.
     */
    bool Run(const std::wstring& file, const std::wstring& params);
}
