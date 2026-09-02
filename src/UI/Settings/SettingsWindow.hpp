#pragma once

// BaseWindow first: it brings in windows.h, and commctrl.h does not compile without it
#include "BaseWindow.hpp"

#include <commctrl.h>
#include <functional>
#include <set>
#include <string>
#include <vector>

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
 * The frame is the IDD_SETTINGS_MAIN template and each category is a child dialog placed inside
 * the group box - the dialog manager owns the layout, the fonts and the DPI scaling.
 */
class SettingsWindow final : public BaseWindow {

public:
    /// Posted when a log line arrives, so the About page reloads itself. The line can come from
    /// any thread, and the logger must never end up waiting on this window's message queue.
    static constexpr UINT WM_LOG_REFRESH = WM_APP + 10;

    struct Config {
        HWND parentHwnd = nullptr;
    };

    struct CategoryItem {
        int resourceId;
        const wchar_t* name;
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
    std::function<void(HWND hWnd, int buttonId)> OnButtonClick;
    std::function<void(HWND hWnd, int comboBoxId)> OnComboBoxChange;
    std::function<void(HWND hWnd, int trackbarId, int value)> OnTrackbarChange;
    std::function<void(HWND hWnd, int sectionId)> OnSectionChange;
    std::function<void(int rowIndex)> OnActionActivated;

private:
    void PopulateTreeView() const;
    void LoadCategoryContent(int resourceId);
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

    static const CategoryItem Categories[];
};

