#pragma once

#include <set>
#include <string>
#include <windows.h>

#include "AppConfig.hpp"

struct ActionLayout {
    std::string Title;
    /// Empty: the action takes no argument and the row is not there.
    std::string ArgsLabel;
    std::string ArgsHint;
    bool RunsCommand = false;
    bool HasSound = true;
    bool HoldOnly = false;
    bool AllowDelete = false;
};

namespace ActionDialog {

    enum class Result { Cancel, Save, Delete };

    /// Edits `binding` in place; a caller that gets anything but Save discards it.
    Result Show(HINSTANCE hInstance, HWND owner, Binding& binding, const ActionLayout& layout,
                std::set<std::string>& recentSounds);
}
