#pragma once

#include <span>
#include <string>
#include <vector>
#include <windows.h>

/// The one file that knows a GUID or a vtable slot. Windows reshuffles slots between builds without touching the
/// IID, so an unknown build is refused: a wrong slot calls another method in explorer. Any MTA thread.
namespace VirtualDesktops {

    bool Supported();

    int Count();

    /// -1 when the shell does not answer.
    int Current();

    /// Per window, -1 for one on every desktop or that the shell does not track. One list fetch for all.
    std::vector<int> DesktopsOf(std::span<const HWND> windows);

    std::wstring DefaultName(int index);

    std::wstring Name(int index);

    std::vector<std::wstring> Names();

    /// Names desktops `from`, `from + 1`, ... as listed; an empty entry leaves that one alone.
    bool Rename(std::span<const std::wstring> names, int from = 0);

    bool Grow(int count);

    /// Animated; overlapping calls from any thread queue behind each other.
    bool Switch(int index);

    bool MoveWindow(HWND window, int index);

    bool TogglePinned(HWND window);
}
