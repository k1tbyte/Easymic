#ifndef EASYMIC_SETTINGSWINDOW_V2_HPP
#define EASYMIC_SETTINGSWINDOW_V2_HPP

#include "../Core/BaseWindow.hpp"
#include "Resources/Resource.h"
#include <commctrl.h>
#include <functional>

#include "Event.hpp"

/**
 * @brief Settings window with TreeView sidebar navigation
 */
class SettingsWindow : public BaseWindow {

public:
    // Layout constants
    static constexpr int SIDE_MENU_WIDTH = 90;
    static constexpr int MARGIN = 8;
    static constexpr int BUTTON_AREA_HEIGHT = 30;
    static constexpr int GROUPBOX_PADDING = 8;

    // Control IDs
    static constexpr int ID_TREEVIEW = 1001;
    static constexpr int ID_GROUPBOX = 1002;

    using OnButtonClickCallback = std::function<void(int buttonId)>;
    using OnComboBoxChangeCallback = std::function<void(int comboBoxId, int value)>;
    using OnTrackbarChangeCallback = std::function<void(int trackbarId, int value)>;
    using OnSectionChangeCallback = std::function<void(HWND hWnd, int sectionId)>;

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

    void SetActiveCategory(int categoryId);

    Event<>& OnExit = _onExit;
    OnButtonClickCallback OnButtonClick = nullptr;
    OnComboBoxChangeCallback OnComboBoxChange = nullptr;
    OnTrackbarChangeCallback OnTrackbarChange = nullptr;
    OnSectionChangeCallback OnSectionChange = nullptr;

private:
    void SetupMessageHandlers();
    void CreateTreeView();
    void PopulateTreeView();
    void CreateGroupBox();
    void LoadCategoryContent(int resourceId);
    void UpdateGroupBoxLayout();

    HTREEITEM AddTreeViewItem(HTREEITEM hParent, const CategoryItem& item);
    void OnTreeViewSelectionChanged(HTREEITEM hItem);

    static LRESULT CALLBACK TreeViewSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

    LRESULT OnInitDialog(WPARAM wParam, LPARAM lParam);
    LRESULT OnCommand(WPARAM wParam, LPARAM lParam);
    LRESULT OnNotify(WPARAM wParam, LPARAM lParam);
    LRESULT OnSize(WPARAM wParam, LPARAM lParam);
    LRESULT OnDestroy(WPARAM wParam, LPARAM lParam);

    Event<> _onExit;

    Config config_;
    HWND hwndTreeView_ = nullptr;
    HWND hwndGroupBox_ = nullptr;
    HWND hwndContentDialog_ = nullptr;
    int currentCategoryId_ = -1;


    static const CategoryItem categories_[];
    static const size_t categoriesCount_;
};

#endif //EASYMIC_SETTINGSWINDOW_V2_HPP

