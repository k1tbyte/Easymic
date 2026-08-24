#ifndef EASYMIC_DIALOGCONTROLS_HPP
#define EASYMIC_DIALOGCONTROLS_HPP

#include <commctrl.h>
#include <filesystem>
#include <set>
#include <string>
#include <windows.h>

/// Small helpers shared by the settings pages and the action dialog.
namespace DialogControls {

    /// Centers a window on the primary screen. Called from WM_INITDIALOG, so it only moves the
    /// window - activation and z-order stay with the dialog manager that is still building it.
    inline void CenterOnScreen(HWND hWnd) {
        RECT window;
        GetWindowRect(hWnd, &window);

        const int width = window.right - window.left;
        const int height = window.bottom - window.top;

        SetWindowPos(hWnd, nullptr,
                     (GetSystemMetrics(SM_CXSCREEN) - width) / 2,
                     (GetSystemMetrics(SM_CYSCREEN) - height) / 2,
                     0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    inline void InitTrackbar(HWND hWnd, LPARAM pageSize, LPARAM minMax, LPARAM value) {
        SendMessage(hWnd, TBM_SETRANGE, TRUE, minMax);
        SendMessage(hWnd, TBM_SETPAGESIZE, 0, pageSize);
        SendMessage(hWnd, TBM_SETPOS, TRUE, value);
    }

    inline bool IsChecked(HWND dialog, int controlId) {
        return SendMessage(GetDlgItem(dialog, controlId), BM_GETCHECK, 0, 0) == BST_CHECKED;
    }

    inline bool DoesFileExist(const std::string& filePath) {
        const DWORD attributes = GetFileAttributesA(filePath.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
    }

    inline void CleanupInvalidSources(std::set<std::string>& recentSources) {
        std::erase_if(recentSources, [](const std::string& source) { return !DoesFileExist(source); });
    }

    inline void AddToRecentSources(std::set<std::string>& recentSources, const std::string& source) {
        if (!source.empty() && DoesFileExist(source)) {
            recentSources.insert(source);
        }
    }

    /**
     * @brief Fills a source picker: the empty label first, then one entry per recent source.
     *
     * Vanished files are dropped up front, so item index N maps to the N-1'th element of the set
     * and no item has to carry a pointer into a container it does not own.
     * @param emptyLabel first entry, meaning "no source picked" - a built-in sound or none at all
     */
    inline void PopulateSourceComboBox(HWND comboBox, std::set<std::string>& recentSources,
                                       const std::string& currentSource = "", const char* emptyLabel = "Default") {
        CleanupInvalidSources(recentSources);

        SendMessageA(comboBox, CB_RESETCONTENT, 0, 0);
        SendMessageA(comboBox, CB_ADDSTRING, 0, (LPARAM)emptyLabel);

        LRESULT selectedIndex = 0;
        for (const auto& source : recentSources) {
            const auto fileName = std::filesystem::path(source).filename().string();
            const LRESULT itemIndex = SendMessageA(comboBox, CB_ADDSTRING, 0, (LPARAM)fileName.c_str());
            if (source == currentSource) {
                selectedIndex = itemIndex;
            }
        }

        SendMessageA(comboBox, CB_SETCURSEL, selectedIndex, 0);
    }

    /// Maps the current selection back to the full source path. Empty means "no source picked".
    inline std::string ResolveSourceFromComboBox(HWND comboBox, const std::set<std::string>& recentSources) {
        const LRESULT selectedIndex = SendMessageA(comboBox, CB_GETCURSEL, 0, 0);
        if (selectedIndex <= 0 || static_cast<size_t>(selectedIndex) > recentSources.size()) {
            return {};
        }

        auto it = recentSources.begin();
        std::advance(it, selectedIndex - 1);
        return *it;
    }
}

#endif //EASYMIC_DIALOGCONTROLS_HPP
