#pragma once

// BaseWindow first: it brings in windows.h, and commctrl.h does not compile without it
#include "BaseWindow.hpp"

#include <commctrl.h>
#include <span>

#include "AppConfig.hpp"
#include "Core/SettingsHost.hpp"
#include "Event.hpp"

/// Modeless: each page is an empty child dialog filled from its rows, which edit the live config. OK saves, Cancel restores the snapshot.
class SettingsWindow final : public BaseWindow {
public:
    SettingsWindow(HINSTANCE hInstance, HWND owner, AppConfig& config);
    ~SettingsWindow() override = default;

    void Show();

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
    AppConfig _cfgPrev;
    std::span<const SettingsRow> _rows;
    std::span<const SettingsTab> _tabs;

    HWND _owner;
    HWND _hwndTreeView = nullptr;
    HWND _hwndGroupBox = nullptr;
    HWND _hwndContentDialog = nullptr;
    HWND _hwndTabs = nullptr;
    HWND _hwndTabPage = nullptr;
};
