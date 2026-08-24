#ifndef EASYMIC_SETTINGSWINDOW_V2_HPP
#define EASYMIC_SETTINGSWINDOW_V2_HPP

#include "../Core/BaseWindow.hpp"
#include "Resources/Resource.h"
#include <array>
#include <commctrl.h>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "AppConfig.hpp"
#include "ActionDialog.hpp"
#include "Event.hpp"


/// One row of the actions table, built-in or custom.
struct ActionRow {
    std::string Name;
    std::string Hotkey;
    std::string Command;
    bool IsCustom = false;
};

/**
 * @brief Settings window with TreeView sidebar navigation.
 *
 * The frame is the IDD_SETTINGS_MAIN template and each category is a child dialog placed inside
 * the group box - the dialog manager owns the layout, the fonts and the DPI scaling.
 */
class SettingsWindow : public BaseWindow {

public:
    using OnButtonClickCallback = std::function<void(HWND hWnd, int buttonId)>;
    using OnComboBoxChangeCallback = std::function<void(HWND hWnd, int comboBoxId)>;
    using OnTrackbarChangeCallback = std::function<void(HWND hWnd, int trackbarId, int value)>;
    using OnSectionChangeCallback = std::function<void(HWND hWnd, int sectionId)>;
    using OnActionActivatedCallback = std::function<void(int rowIndex)>;

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

    void SetActionRows(const std::vector<ActionRow>& rows);

    /// Modal editor for any action. @return true when the action must be saved.
    bool ShowActionDialog(ActionEdit& action, std::set<std::string>& recentSounds);

    Event<>& OnExit = _onExit;
    Event<>& OnApply = _onApply;
    OnButtonClickCallback OnButtonClick = nullptr;
    OnComboBoxChangeCallback OnComboBoxChange = nullptr;
    OnTrackbarChangeCallback OnTrackbarChange = nullptr;
    OnSectionChangeCallback OnSectionChange = nullptr;
    OnActionActivatedCallback OnActionActivated = nullptr;

private:
    void PopulateTreeView();
    void LoadCategoryContent(int resourceId);
    void UpdateGroupBoxLayout() const;
    void OnTreeViewSelectionChanged(HTREEITEM hItem);

    static INT_PTR CALLBACK SettingsDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK TreeViewSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

    INT_PTR OnInitDialog();
    INT_PTR OnCommand(WPARAM wParam, LPARAM lParam);
    INT_PTR OnNotify(WPARAM wParam, LPARAM lParam);
    INT_PTR OnDestroy();
    INT_PTR OnCtlColorStatic(WPARAM wParam, LPARAM lParam);

    Event<> _onExit;
    Event<> _onApply;

    Config config_;
    HWND hwndTreeView_ = nullptr;
    HWND hwndGroupBox_ = nullptr;
    HWND hwndContentDialog_ = nullptr;
    int currentCategoryId_ = -1;

    static const CategoryItem categories_[];
    static const size_t categoriesCount_;
};

#endif //EASYMIC_SETTINGSWINDOW_V2_HPP
