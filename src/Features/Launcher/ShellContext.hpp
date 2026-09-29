#pragma once

#include <windows.h>

#include <string>

/**
 * @brief What the window in front is looking at.
 *
 * Explorer publishes this over COM only, so a lookup is a handful of calls into explorer.exe and
 * costs about 3 ms - fine once per hotkey, never on a path that runs unconditionally.
 */
namespace ShellContext {

    /**
     * @brief The folder the active Explorer tab shows, empty when there is none.
     *
     * Apartment threaded, and the caller owns CoInitialize - this runs on the command thread,
     * which is in an apartment already because ShellExecuteEx needs one.
     */
    std::wstring ActiveFolder(HWND window);
}
