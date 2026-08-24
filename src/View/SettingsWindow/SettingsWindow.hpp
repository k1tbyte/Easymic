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
#include "Event.hpp"


/// Actions with fixed behaviour. The names double as config keys, so they must stay stable.
namespace BuiltInAction {
    inline constexpr const char* ToggleMute      = "Toggle mute";
    inline constexpr const char* PushToTalk      = "Push to talk";
    inline constexpr const char* MicVolumeUp     = "Mic volume up";
    inline constexpr const char* MicVolumeDown   = "Mic volume down";
    inline constexpr const char* ToggleBellSound = "Toggle bell sound";

    inline constexpr std::array All = {
        ToggleMute, PushToTalk, MicVolumeUp, MicVolumeDown, ToggleBellSound
    };
}

/// One row of the actions table, built-in or custom.
struct ActionRow {
    std::string Name;
    std::string Hotkey;
    std::string Command;
    bool IsCustom = false;
};

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
    static constexpr UINT_PTR ID_TREEVIEW = 1001;
    static constexpr UINT_PTR ID_GROUPBOX = 1002;

    using OnButtonClickCallback = std::function<void(HWND hWnd, int buttonId)>;
    using OnComboBoxChangeCallback = std::function<void(HWND hWnd, int comboBoxId)>;
    using OnTrackbarChangeCallback = std::function<void(HWND hWnd, int trackbarId, int value)>;
    using OnSectionChangeCallback = std::function<void(HWND hWnd, int sectionId)>;
    using OnActionActivatedCallback = std::function<void(HWND hWnd, int rowIndex)>;

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
    void SetActionRows(const std::vector<ActionRow>& rows);
    void SetHotkeyCellValue(int index, LPCSTR value);
    void SetHotkeySectionTitle(const wchar_t* title);
    bool IsCustomActionRow(int index) const;

    /**
     * @brief Modal editor for a custom action.
     * @param action in/out, prefilled when editing an existing one
     * @param allowDelete shows the Delete button
     * @param deleted set when the user pressed Delete
     * @return true when the action must be saved
     */
    bool ShowActionDialog(CustomAction& action, std::set<std::string>& recentSounds,
                          bool allowDelete, bool& deleted);

    Event<>& OnExit = _onExit;
    Event<>& OnApply = _onApply;
    OnButtonClickCallback OnButtonClick = nullptr;
    OnComboBoxChangeCallback OnComboBoxChange = nullptr;
    OnTrackbarChangeCallback OnTrackbarChange = nullptr;
    OnSectionChangeCallback OnSectionChange = nullptr;
    OnActionActivatedCallback OnActionActivated = nullptr;

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
    std::vector<bool> customRows_;


    static const CategoryItem categories_[];
    static const size_t categoriesCount_;
};

#endif //EASYMIC_SETTINGSWINDOW_V2_HPP

