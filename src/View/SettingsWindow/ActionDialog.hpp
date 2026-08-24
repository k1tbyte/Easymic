#ifndef EASYMIC_ACTIONDIALOG_HPP
#define EASYMIC_ACTIONDIALOG_HPP

#include <set>
#include <string>
#include <windows.h>

#include "AppConfig.hpp"

/// Modal editor for one custom action.
namespace ActionDialog {
    /**
     * @param action in/out, prefilled when editing an existing one
     * @param allowDelete shows the Delete button
     * @param deleted set when the user pressed Delete
     * @return true when the action must be saved
     */
    bool Show(HINSTANCE hInstance, HWND owner, CustomAction& action,
              std::set<std::string>& recentSounds, bool allowDelete, bool& deleted);
}

#endif //EASYMIC_ACTIONDIALOG_HPP
