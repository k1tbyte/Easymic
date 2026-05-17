#ifndef EASYMIC_SETTINGSWINDOW_V2_HPP
#define EASYMIC_SETTINGSWINDOW_V2_HPP

#include "../Core/BaseWindow.hpp"
#include "Resources/Resource.h"
#include <commctrl.h>
#include <functional>

#include "Event.hpp"


#pragma pack(push, 1)
static inline struct  {
    const char* ToggleMute = "Toggle mute";
    const char* PushToTalk = "Push to talk";
    const char* MicVolumeUp = "Mic volume up";
    const char* MicVolumeDown = "Mic volume down";
    const char* ToggleBellSound = "Toggle bell sound";
} HotkeyTitles;
#pragma pack(pop)

/**
 * @brief Settings window with TreeView sidebar navigation
 */
class SettingsWindow : public BaseWindow {

public:
    static int Scale(int px, UINT dpi) { return MulDiv(px, static_cast<int>(dpi), 96); }

    // Runtime DPI-scaled layout helpers
    int SideMenuWidth()    const { return Scale(90, dpi_); }
    int Margin()           const { return Scale(8,  dpi_); }
    int ButtonAreaHeight() const { return Scale(30, dpi_); }
    int GroupBoxPadding()  const { return Scale(8,  dpi_); }
    int ButtonWidth()      const { return Scale(75, dpi_); }
    int ButtonHeight()     const { return Scale(23, dpi_); }

    // Control IDs
    static constexpr int ID_TREEVIEW = 1001;
    static constexpr int ID_GROUPBOX = 1002;

    using OnButtonClickCallback = std::function<void(HWND hWnd, int buttonId)>;
    using OnComboBoxChangeCallback = std::function<void(HWND hWnd, int comboBoxId)>;
    using OnTrackbarChangeCallback = std::function<void(HWND hWnd, int trackbarId, int value)>;
    using OnSectionChangeCallback = std::function<void(HWND hWnd, int sectionId)>;
    using OnHotkeyListChangeCallback = std::function<void(HWND hWnd, int listId, LPCSTR itemText)>;

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
    void SetHotkeyCellValue(int index, LPCSTR value);
    void SetHotkeySectionTitle(const wchar_t* title);
    void ResetHotkeyCellValue(LPCSTR actionTitle);

    Event<>& OnExit = _onExit;
    Event<>& OnApply = _onApply;
    OnButtonClickCallback OnButtonClick = nullptr;
    OnComboBoxChangeCallback OnComboBoxChange = nullptr;
    OnTrackbarChangeCallback OnTrackbarChange = nullptr;
    OnSectionChangeCallback OnSectionChange = nullptr;
    OnHotkeyListChangeCallback OnHotkeyListChange = nullptr;

    /*LRESULT HandleMessage (UINT message, WPARAM wParam, LPARAM lParam) override {
        if (const auto it = messageHandlers_.find(message); it != messageHandlers_.end()) {
            return it->second(wParam, lParam);
        }
        return true;
    }*/

private:
    void SetupMessageHandlers();
    void RegisterWindowClass();
    void CreateMainButtons();
    void CreateTreeView();
    void PopulateTreeView();
    void CreateGroupBox();
    void LoadCategoryContent(int resourceId);
    void UpdateGroupBoxLayout();
    void UpdateVersionLabel();

    HTREEITEM AddTreeViewItem(HTREEITEM hParent, const CategoryItem& item);
    void OnTreeViewSelectionChanged(HTREEITEM hItem);

    static LRESULT CALLBACK SettingsWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK TreeViewSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

    LRESULT OnCreate(WPARAM wParam, LPARAM lParam);
    LRESULT OnCommand(WPARAM wParam, LPARAM lParam);
    LRESULT OnNotify(WPARAM wParam, LPARAM lParam);
    LRESULT OnSize(WPARAM wParam, LPARAM lParam);
    LRESULT OnDestroy(WPARAM wParam, LPARAM lParam);
    LRESULT OnCtlColorStatic(WPARAM wParam, LPARAM lParam);
    LRESULT OnDpiChanged(WPARAM wParam, LPARAM lParam);

    void RebuildFonts();
    void RepositionButtons();

    Event<> _onExit;
    Event<> _onApply;

    Config config_;
    UINT dpi_ = 96;
    HWND hwndTreeView_ = nullptr;
    HWND hwndGroupBox_ = nullptr;
    HWND hwndContentDialog_ = nullptr;
    HWND hwndOkButton_ = nullptr;
    HWND hwndCancelButton_ = nullptr;
    HWND hwndVersionLabel_ = nullptr;
    HBRUSH hGrayBrush_ = nullptr;
    HFONT hButtonFont_ = nullptr;
    HFONT hVersionFont_ = nullptr;
    HFONT hGroupBoxFont_ = nullptr;
    int currentCategoryId_ = -1;


    static const CategoryItem categories_[];
    static const size_t categoriesCount_;
};

#endif //EASYMIC_SETTINGSWINDOW_V2_HPP

