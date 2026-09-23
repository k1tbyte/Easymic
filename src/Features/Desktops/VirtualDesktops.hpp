#pragma once

#include <string>
#include <windows.h>

/**
 * @brief The shell's virtual desktops, through its undocumented COM interfaces.
 *
 * The one file that knows a GUID or a vtable slot. Windows reshuffles the slots between builds
 * without touching the IID, so the layout is picked by build number, and a build this does not
 * know is refused rather than guessed at - a wrong slot calls a different method in explorer.
 *
 * Any thread in the process MTA (main joins it before modules register): the connection is
 * guarded, and every object in it is an MTA proxy. A call made after explorer restarted
 * reconnects once and retries.
 */
namespace VirtualDesktops {

    /// Static and cheap - the build is one this knows the layout of.
    bool Supported();

    /// 0 when the shell does not answer.
    int Count();

    /// -1 when the shell does not answer.
    int Current();

    /// -1 for a window on every desktop, or one the shell does not track.
    int DesktopOf(HWND window);

    /// What the shell calls it, or "Desktop N" for one nobody named.
    std::wstring Name(int index);

    /// An empty name gives the desktop back its "Desktop N".
    bool Rename(int index, const std::wstring& name);

    /// Adds desktops at the end until there are count of them. Never removes one.
    bool Grow(int count);

    bool Switch(int index);

    bool MoveWindow(HWND window, int index);

    /// Shows the window on every desktop, or takes it back to one.
    bool TogglePinned(HWND window);
}
