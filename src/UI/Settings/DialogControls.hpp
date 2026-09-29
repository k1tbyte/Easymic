#pragma once

#include <commctrl.h>
#include <iterator>
#include <set>
#include <string>
#include <windows.h>

#include "SoundCatalog.hpp"
#include "Str.hpp"

namespace DialogControls {

    /// Only moves the window: safe in WM_INITDIALOG, where activation and z-order still belong to the dialog manager.
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

    inline constexpr int RowHeightDlu = 20;

    /// Lets one template serve every kind of action: a row that does not apply is removed, not left as a hole.
    inline void CollapseRow(HWND dialog, std::initializer_list<int> controlIds) {
        RECT step{0, 0, 0, RowHeightDlu};
        MapDialogRect(dialog, &step);

        RECT row{};
        GetWindowRect(GetDlgItem(dialog, *controlIds.begin()), &row);

        for (const int id : controlIds) {
            ShowWindow(GetDlgItem(dialog, id), SW_HIDE);
        }

        for (HWND child = GetWindow(dialog, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
            RECT childRect;
            GetWindowRect(child, &childRect);
            if (childRect.top <= row.top) {
                continue;
            }

            POINT position{childRect.left, childRect.top - step.bottom};
            ScreenToClient(dialog, &position);
            SetWindowPos(child, nullptr, position.x, position.y, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }

        RECT window;
        GetWindowRect(dialog, &window);
        SetWindowPos(dialog, nullptr, 0, 0, window.right - window.left,
                     window.bottom - window.top - step.bottom, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    inline bool IsChecked(HWND dialog, int controlId) {
        return SendMessage(GetDlgItem(dialog, controlId), BM_GETCHECK, 0, 0) == BST_CHECKED;
    }

    inline bool DoesFileExist(const std::string& filePath) {
        const DWORD attributes = GetFileAttributesW(Str::Utf8ToWide(filePath).c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
    }

    inline void PruneRecentSounds(std::set<std::string>& recent) {
        std::erase_if(recent, [](const std::string& source) { return !DoesFileExist(source); });
    }

    inline void AddRecentSound(std::set<std::string>& recent, const std::string& source) {
        if (!source.empty() && DoesFileExist(source)) {
            recent.insert(source);
        }
    }

    /// None, the bundled sounds, then the user files. Vanished files are pruned first so an item index maps back by position.
    inline void PopulateSoundCombo(HWND comboBox, std::set<std::string>& recent, const std::string& current) {
        PruneRecentSounds(recent);

        SendMessageW(comboBox, CB_RESETCONTENT, 0, 0);
        SendMessageW(comboBox, CB_ADDSTRING, 0, (LPARAM)L"None");

        LRESULT selectedIndex = 0;

        for (const auto& bundled : SoundCatalog::All) {
            const LRESULT index = SendMessageW(comboBox, CB_ADDSTRING, 0,
                                               (LPARAM)Str::Utf8ToWide(bundled.Title).c_str());
            if (current == bundled.Key) {
                selectedIndex = index;
            }
        }

        for (const auto& source : recent) {
            const std::wstring path = Str::Utf8ToWide(source);
            const size_t slash = path.find_last_of(L"\\/");
            const wchar_t* name = slash == std::wstring::npos ? path.c_str() : path.c_str() + slash + 1;
            const LRESULT index = SendMessageW(comboBox, CB_ADDSTRING, 0, (LPARAM)name);
            if (current == source) {
                selectedIndex = index;
            }
        }

        SendMessageW(comboBox, CB_SETCURSEL, selectedIndex, 0);
    }

    inline std::string ResolveSound(HWND comboBox, const std::set<std::string>& recent) {
        const LRESULT selected = SendMessageW(comboBox, CB_GETCURSEL, 0, 0);
        constexpr LRESULT bundledCount = static_cast<LRESULT>(std::size(SoundCatalog::All));

        if (selected <= 0) {
            return {};
        }

        if (selected <= bundledCount) {
            return SoundCatalog::All[selected - 1].Key;
        }

        const auto fileIndex = static_cast<size_t>(selected - bundledCount - 1);
        if (fileIndex >= recent.size()) {
            return {};
        }

        auto it = recent.begin();
        std::advance(it, fileIndex);
        return *it;
    }
}

