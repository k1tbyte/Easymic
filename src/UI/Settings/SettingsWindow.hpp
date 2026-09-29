#pragma once

// BaseWindow first: it brings in windows.h, and commctrl.h does not compile without it
#include "BaseWindow.hpp"

#include <commctrl.h>
#include <span>

#include "AppConfig.hpp"
#include "Core/SettingsHost.hpp"
#include "Event.hpp"

/**
 * @brief Settings window with TreeView sidebar navigation.
 *
 * The frame is the IDD_SETTINGS_MAIN template and each page is an empty child dialog in the group
 * box, filled from the page's rows - the dialog manager still owns the font and the DPI scale.
 * The rows edit the live config: OK saves it, Cancel puts back the snapshot taken on open.
 */
class SettingsWindow final : public BaseWindow {

public:
    SettingsWindow(HINSTANCE hInstance, HWND owner, AppConfig& config);
    ~SettingsWindow() override = default;

    void Show() override;

    // Subscribe side only - the window is the one that raises these
    /// OK, before the rows commit: what the config does not follow live goes into it now.
    IEvent<>& OnApply = _onApply;
    IEvent<>& OnExit = _onExit;

private:
    void PopulateTreeView() const;
    void ShowPage(const SettingsPage& page);
    void ShowTab(int index);
    void UpdateGroupBoxLayout() const;
    void OnTreeViewSelectionChanged(HTREEITEM hItem);
    void Apply();

    static INT_PTR CALLBACK SettingsDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static INT_PTR CALLBACK PageProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK TreeViewSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
                                                 UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

    INT_PTR OnInitDialog();
    INT_PTR OnCommand(WPARAM wParam);
    INT_PTR OnNotify(LPARAM lParam);
    INT_PTR OnDestroy();
    INT_PTR OnCtlColorStatic(WPARAM wParam, LPARAM lParam) const;

    Event<> _onExit;
    Event<> _onApply;

    AppConfig& _cfg;
    /// What OK compares against and Cancel puts back - the whole config, so no row needs an undo.
    AppConfig _cfgPrev;
    /// The open page's rows, which its controls' input is routed through.
    std::span<const SettingsRow> _rows;
    std::span<const SettingsTab> _tabs;

    HWND _owner;
    HWND _hwndTreeView = nullptr;
    HWND _hwndGroupBox = nullptr;
    HWND _hwndContentDialog = nullptr;
    HWND _hwndTabs = nullptr;
    HWND _hwndTabPage = nullptr;
};
