#pragma once

// BaseWindow first: it brings in windows.h, and commctrl.h does not compile without it
#include "BaseWindow.hpp"

#include <commctrl.h>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "Core/SettingsHost.hpp"
#include "Resources/Resource.h"
#include "ActionDialog.hpp"
#include "Event.hpp"

/// One row of the actions table, built-in or custom.
struct ActionRow {
    std::string Name;
    std::string Hotkey;
    std::string Command;
    bool RunsCommand = false;
};

/**
 * @brief Settings window with TreeView sidebar navigation.
 *
 * The frame is the IDD_SETTINGS_MAIN template and each page is an empty child dialog in the group
 * box that the view model fills from the page's rows - the dialog manager still owns the font and
 * the DPI scale.
 */
class SettingsWindow final : public BaseWindow {

public:
    /// Posted when a log line arrives, so the About page reloads itself. The line can come from
    /// any thread, and the logger must never end up waiting on this window's message queue.
    static constexpr UINT WM_LOG_REFRESH = WM_APP + 10;

    struct Config {
        HWND parentHwnd = nullptr;
    };


    explicit SettingsWindow(HINSTANCE hInstance);
    ~SettingsWindow() override = default;

    bool Initialize(const Config& config);

    void Show() override;
    void Hide() override;

    void SetActionRows(const std::vector<ActionRow>& rows) const;

    /// Modal editor for any action. @return true when the action must be saved.
    bool ShowActionDialog(ActionEdit& action, std::set<std::string>& recentSounds) const;

    // Subscribe side only - the window is the one that raises these
    IEvent<>& OnExit = _onExit;
    IEvent<>& OnApply = _onApply;

    // View model delegation - single subscriber each, assigned once during Init
    /// The page is sized and still hidden, so whatever fills it can measure it.
    std::function<void(HWND page, const SettingsPage& desc)> OnPageCreated;
    /// WM_COMMAND and WM_HSCROLL from the open page.
    std::function<void(HWND page, UINT message, WPARAM wParam, LPARAM lParam)> OnPageInput;
    std::function<void(int rowIndex)> OnActionActivated;

private:
    void PopulateTreeView() const;
    void ShowPage(const SettingsPage& page);
    void UpdateGroupBoxLayout() const;
    void OnTreeViewSelectionChanged(HTREEITEM hItem);

    static INT_PTR CALLBACK SettingsDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK TreeViewSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
                                                 UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

    INT_PTR OnInitDialog();
    INT_PTR OnCommand(WPARAM wParam);
    INT_PTR OnNotify(LPARAM lParam);
    INT_PTR OnDestroy();
    INT_PTR OnCtlColorStatic(WPARAM wParam, LPARAM lParam) const;

    Event<> _onExit;
    Event<> _onApply;

    Config _config;
    HWND _hwndTreeView = nullptr;
    HWND _hwndGroupBox = nullptr;
    HWND _hwndContentDialog = nullptr;

};
