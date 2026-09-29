#pragma once

#include <windows.h>

#include <string>
#include <vector>

/// Through explorer.exe so the app gets the user's token, not ours: we run elevated when "skip UAC" is on.
namespace ShellLaunch {

    /// Gives up once the foreground moves, so a user who switched away is not yanked back.
    void RaiseNew(const std::vector<HWND>& before, HWND wasInFront);

    /// False when the shell is out of reach (explorer restarting). A bad command is reported by the shell itself.
    bool Run(const std::wstring& file, const std::wstring& params);
}
