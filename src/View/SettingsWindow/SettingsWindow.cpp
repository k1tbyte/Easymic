#include "SettingsWindow.hpp"

#include <commctrl.h>
#include <string>

#include "ActionDialog.hpp"
#include "DialogControls.hpp"
#include "UACService.hpp"
#include "../../Resources/Resource.h"
#include "../../Lib/HotkeyCapture.hpp"
#include "../../Lib/Version.hpp"

namespace {
    /// Desaturated on purpose - it marks the row type, it is not a status
    constexpr COLORREF CustomActionRowColor = RGB(237, 246, 237);
    constexpr COLORREF VersionLabelColor = RGB(128, 128, 128);

    /**
     * @brief Splits the client width across the columns - no horizontal scrolling, long commands
     * get ellipsized and hovering unfolds the full text.
     *
     * GetClientRect already excludes the vertical scrollbar, so this has to run again after every
     * fill: guessing the scrollbar width instead leaves a dead gap while the list still fits.
     */
    void LayoutActionColumns(HWND hwndList) {
        constexpr int percents[] = {34, 30, 0}; // the last one takes what is left

        RECT clientRect;
        GetClientRect(hwndList, &clientRect);

        int used = 0;
        for (int i = 0; i < static_cast<int>(std::size(percents)); i++) {
            const int width = percents[i] ? clientRect.right * percents[i] / 100
                                          : clientRect.right - used;
            used += width;
            SendMessage(hwndList, LVM_SETCOLUMNWIDTH, i, MAKELPARAM(width, 0));
        }
    }

    void InitializeActionsList(HWND hwndList) {
        if (!hwndList) {
            return;
        }

        SendMessage(hwndList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
                    LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_LABELTIP);

        const wchar_t* titles[] = {L"Action", L"Hotkey", L"Command"};

        LVCOLUMNW lvc = {};
        lvc.mask = LVCF_TEXT | LVCF_SUBITEM;

        for (int i = 0; i < static_cast<int>(std::size(titles)); i++) {
            lvc.iSubItem = i;
            lvc.pszText = const_cast<wchar_t*>(titles[i]);
            SendMessage(hwndList, LVM_INSERTCOLUMNW, i, (LPARAM)&lvc);
        }

        LayoutActionColumns(hwndList);
    }

    /// One category page. Forwards its input to the view model through the owning window.
    INT_PTR CALLBACK ChildDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* window = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        switch (message) {
            case WM_INITDIALOG:
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, lParam);
                InitializeActionsList(GetDlgItem(hwnd, IDC_HOTKEYS_LIST));
                return TRUE;

            case WM_COMMAND: {
                if (!window) {
                    break;
                }

                if (HIWORD(wParam) == BN_CLICKED && window->OnButtonClick) {
                    window->OnButtonClick(hwnd, LOWORD(wParam));
                    return TRUE;
                }

                if (HIWORD(wParam) == CBN_SELCHANGE && window->OnComboBoxChange) {
                    window->OnComboBoxChange(hwnd, LOWORD(wParam));
                    return TRUE;
                }
                break;
            }

            case WM_HSCROLL:
                if (window && LOWORD(wParam) != TB_ENDTRACK && window->OnTrackbarChange) {
                    window->OnTrackbarChange(hwnd, GetDlgCtrlID((HWND)lParam),
                                             SendMessage((HWND)lParam, TBM_GETPOS, 0, 0));
                }
                break;

            case WM_NOTIFY: {
                const auto* header = (LPNMHDR)lParam;
                if (!window || header->idFrom != IDC_HOTKEYS_LIST) {
                    break;
                }

                if (header->code == NM_DBLCLK) {
                    const auto* activated = (LPNMITEMACTIVATE)lParam;
                    if (activated->iItem != -1 && window->OnActionActivated) {
                        window->OnActionActivated(activated->iItem);
                    }
                    break;
                }

                if (header->code == NM_CUSTOMDRAW) {
                    // Tint custom action rows so they stand apart from the built-in ones
                    auto* draw = reinterpret_cast<LPNMLVCUSTOMDRAW>(lParam);
                    LRESULT result = CDRF_DODEFAULT;

                    if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) {
                        result = CDRF_NOTIFYITEMDRAW;
                    } else if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT && draw->nmcd.lItemlParam) {
                        draw->clrTextBk = CustomActionRowColor;
                    }

                    SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, result);
                    return TRUE;
                }
                break;
            }
        }

        return FALSE;
    }
}

const SettingsWindow::CategoryItem SettingsWindow::categories_[] = {
    {IDD_SETTINGS_GENERAL, L"General"},
    {IDD_SETTINGS_INDICATOR, L"Indicator"},
    {IDD_SETTINGS_SOUNDS, L"Sounds"},
    {IDD_SETTINGS_HOTKEYS, L"Hotkeys"},
    {IDD_SETTINGS_ABOUT, L"About"},
};

const size_t SettingsWindow::categoriesCount_ = std::size(categories_);

SettingsWindow::SettingsWindow(HINSTANCE hInstance)
    : BaseWindow(hInstance)
{
}

bool SettingsWindow::Initialize(const Config& config) {
    this->_parent = WindowRegistry::Instance().Get(config.parentHwnd);

    if (!this->_parent) {
        return false;
    }

    config_ = config;
    _viewModel->Init();
    return true;
}

void SettingsWindow::Show() {
    if (!hwnd_) {
        INITCOMMONCONTROLSEX controls{sizeof(INITCOMMONCONTROLSEX), ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES};
        InitCommonControlsEx(&controls);

        // Modeless: the tray app keeps pumping its own message loop while this is open
        CreateDialogParamW(hInstance_, MAKEINTRESOURCEW(IDD_SETTINGS_MAIN), config_.parentHwnd,
                           SettingsDialogProc, reinterpret_cast<LPARAM>(this));

        if (!hwnd_) {
            MessageBoxW(nullptr, L"Failed to create the settings window.", L"Error", MB_ICONERROR | MB_OK);
            return;
        }

        ShowWindow(hwnd_, SW_SHOW);
    }

    SetForegroundWindow(hwnd_);
    isVisible_ = true;
}

void SettingsWindow::Hide() {
    if (hwnd_) {
        _close(hwnd_);
    }
}

INT_PTR CALLBACK SettingsWindow::SettingsDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_INITDIALOG) {
        auto* created = reinterpret_cast<SettingsWindow*>(lParam);
        created->RegisterWindow(hwnd);
        return created->OnInitDialog();
    }

    auto* window = static_cast<SettingsWindow*>(WindowRegistry::Instance().Get(hwnd));
    if (!window) {
        return FALSE;
    }

    switch (message) {
        case WM_COMMAND:        return window->OnCommand(wParam, lParam);
        case WM_NOTIFY:         return window->OnNotify(wParam, lParam);
        case WM_CTLCOLORSTATIC: return window->OnCtlColorStatic(wParam, lParam);
        case WM_CLOSE:          window->Close(); return TRUE;
        case WM_DESTROY:        return window->OnDestroy();
        case HotkeyCapture::WM_CAPTURE_DONE:
            HotkeyCapture::Finish();
            return TRUE;
        default:                return FALSE;
    }
}

INT_PTR SettingsWindow::OnInitDialog() {
    hwndTreeView_ = GetDlgItem(hwnd_, IDC_SETTINGS_TREE);
    hwndGroupBox_ = GetDlgItem(hwnd_, IDC_SETTINGS_GROUPBOX);

    if (UAC::IsElevated()) {
        SetWindowTextW(hwnd_, L"Easymic - settings (Administrator)");
    }

    HICON icon = LoadIconW(hInstance_, MAKEINTRESOURCEW(IDI_APP));
    SendMessageW(hwnd_, WM_SETICON, ICON_BIG, (LPARAM)icon);
    SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, (LPARAM)icon);

    SetDlgItemTextA(hwnd_, IDC_SETTINGS_VERSION, g_AppVersion.GetFullFormat().c_str());

    // Hand cursor over the categories
    SetWindowSubclass(hwndTreeView_, TreeViewSubclassProc, 0, reinterpret_cast<DWORD_PTR>(this));
    PopulateTreeView();
    return TRUE;
}

INT_PTR SettingsWindow::OnCommand(WPARAM wParam, LPARAM lParam) {
    const UINT commandId = LOWORD(wParam);

    if (commandId == IDOK) {
        OnApply();
        Close();
        return TRUE;
    }

    if (commandId == IDCANCEL) {
        Close();
        return TRUE;
    }

    return FALSE;
}

INT_PTR SettingsWindow::OnNotify(WPARAM wParam, LPARAM lParam) {
    const auto* header = (LPNMHDR)lParam;

    if (header->idFrom == IDC_SETTINGS_TREE && header->code == TVN_SELCHANGEDW) {
        OnTreeViewSelectionChanged(((LPNMTREEVIEW)lParam)->itemNew.hItem);
    }

    return FALSE;
}

INT_PTR SettingsWindow::OnCtlColorStatic(WPARAM wParam, LPARAM lParam) {
    if ((HWND)lParam != GetDlgItem(hwnd_, IDC_SETTINGS_VERSION)) {
        return FALSE;
    }

    SetTextColor((HDC)wParam, VersionLabelColor);
    SetBkMode((HDC)wParam, TRANSPARENT);
    return (INT_PTR)GetStockObject(NULL_BRUSH);
}

INT_PTR SettingsWindow::OnDestroy() {
    HotkeyCapture::Cancel();

    WindowRegistry::Instance().Unregister(hwnd_);
    hwnd_ = nullptr;
    hwndTreeView_ = nullptr;
    hwndGroupBox_ = nullptr;
    hwndContentDialog_ = nullptr;
    isVisible_ = false;

    // Last statement: a subscriber may drop the last reference to this window
    _onExit();
    return TRUE;
}

void SettingsWindow::PopulateTreeView() {
    HTREEITEM firstItem = nullptr;

    for (const auto& category : categories_) {
        TVINSERTSTRUCTW insert{};
        insert.hParent = TVI_ROOT;
        insert.hInsertAfter = TVI_LAST;
        insert.item.mask = TVIF_TEXT | TVIF_PARAM;
        insert.item.pszText = const_cast<wchar_t*>(category.name);
        insert.item.lParam = category.resourceId;

        const auto item = (HTREEITEM)SendMessageW(hwndTreeView_, TVM_INSERTITEMW, 0, (LPARAM)&insert);
        firstItem = firstItem ? firstItem : item;
    }

    SendMessageW(hwndTreeView_, TVM_SELECTITEM, TVGN_CARET, (LPARAM)firstItem);
}

void SettingsWindow::OnTreeViewSelectionChanged(HTREEITEM hItem) {
    if (!hItem) {
        return;
    }

    wchar_t title[64];
    TVITEMW item{};
    item.mask = TVIF_PARAM | TVIF_TEXT;
    item.hItem = hItem;
    item.pszText = title;
    item.cchTextMax = static_cast<int>(std::size(title));

    if (!SendMessageW(hwndTreeView_, TVM_GETITEMW, 0, (LPARAM)&item)) {
        return;
    }

    currentCategoryId_ = static_cast<int>(item.lParam);
    SetWindowTextW(hwndGroupBox_, title);
    LoadCategoryContent(currentCategoryId_);

    if (OnSectionChange) {
        OnSectionChange(hwndContentDialog_, currentCategoryId_);
    }
}

void SettingsWindow::LoadCategoryContent(int resourceId) {
    if (hwndContentDialog_) {
        DestroyWindow(hwndContentDialog_);
        hwndContentDialog_ = nullptr;
    }

    hwndContentDialog_ = CreateDialogParamW(hInstance_, MAKEINTRESOURCEW(resourceId), hwndGroupBox_,
                                            ChildDialogProc, reinterpret_cast<LPARAM>(this));

    if (hwndContentDialog_) {
        UpdateGroupBoxLayout();
        ShowWindow(hwndContentDialog_, SW_SHOW);
    }
}

/// Fits the category page into the group box interior, below its title.
void SettingsWindow::UpdateGroupBoxLayout() const {
    RECT groupBox;
    GetClientRect(hwndGroupBox_, &groupBox);

    const UINT dpi = GetDpiForWindow(hwnd_);
    const int padding = MulDiv(8, dpi, 96);
    const int titleHeight = MulDiv(20, dpi, 96);

    SetWindowPos(hwndContentDialog_, nullptr,
                 padding, titleHeight,
                 groupBox.right - padding * 2,
                 groupBox.bottom - titleHeight - padding,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

void SettingsWindow::SetActionRows(const std::vector<ActionRow>& rows) {
    HWND hwndList = GetDlgItem(hwndContentDialog_, IDC_HOTKEYS_LIST);
    if (!hwndList) {
        return;
    }

    SendMessage(hwndList, LVM_DELETEALLITEMS, 0, 0);

    LVITEMA lvi = {};
    lvi.mask = LVIF_TEXT | LVIF_PARAM;
    int index = 0;

    for (const auto& row : rows) {
        const char* cells[] = {row.Name.c_str(), row.Hotkey.c_str(), row.Command.c_str()};

        lvi.iItem = index++;
        lvi.iSubItem = 0;
        lvi.lParam = row.IsCustom; // the custom draw reads it back - no parallel array to keep in sync
        lvi.pszText = const_cast<LPSTR>(cells[0]);
        const int itemIndex = SendMessageA(hwndList, LVM_INSERTITEMA, 0, (LPARAM)&lvi);

        for (int column = 1; column < static_cast<int>(std::size(cells)); column++) {
            lvi.iItem = itemIndex;
            lvi.iSubItem = column;
            lvi.pszText = const_cast<LPSTR>(cells[column]);
            SendMessageA(hwndList, LVM_SETITEMTEXTA, itemIndex, (LPARAM)&lvi);
        }
    }

    LayoutActionColumns(hwndList);
}

bool SettingsWindow::ShowActionDialog(CustomAction& action, std::set<std::string>& recentSounds,
                                      const bool allowDelete, bool& deleted) {
    return ActionDialog::Show(hInstance_, hwnd_, action, recentSounds, allowDelete, deleted);
}

void SettingsWindow::SetHotkeyCellValue(int index, LPCSTR value) {
    HWND hwndList = GetDlgItem(hwndContentDialog_, IDC_HOTKEYS_LIST);
    if (!hwndList) {
        return;
    }

    LVITEMA lvi = {};
    lvi.iItem = index;
    lvi.iSubItem = 1; // Hotkey column
    lvi.mask = LVIF_TEXT;
    lvi.pszText = const_cast<LPSTR>(value);

    SendMessageA(hwndList, LVM_SETITEMA, 0, (LPARAM)&lvi);
}

void SettingsWindow::SetHotkeySectionTitle(const wchar_t* title) {
    HWND hwndTitle = GetDlgItem(hwndContentDialog_, IDC_HOTKEYS_TITLE);
    if (!hwndTitle) {
        return;
    }
    SetWindowTextW(hwndTitle, title == nullptr ? L"Double-click to set up a hotkey:" : title);
}

LRESULT CALLBACK SettingsWindow::TreeViewSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
                                                      UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    switch (uMsg) {
        case WM_SETCURSOR: {
            TVHITTESTINFO hitTest = {};
            GetCursorPos(&hitTest.pt);
            ScreenToClient(hwnd, &hitTest.pt);

            if (SendMessage(hwnd, TVM_HITTEST, 0, (LPARAM)&hitTest) && (hitTest.flags & TVHT_ONITEM)) {
                SetCursor(LoadCursor(nullptr, IDC_HAND));
                return TRUE;
            }
            break;
        }

        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, TreeViewSubclassProc, uIdSubclass);
            break;
    }

    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}
