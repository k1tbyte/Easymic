#include "SettingsWindow.hpp"

#include <algorithm>
#include <commctrl.h>
#include <string>

#include "ActionDialog.hpp"
#include "UACService.hpp"
#include "Resources/Resource.h"
#include "Version.hpp"
#include "Logger.hpp"
#include "Str.hpp"

namespace {
    constexpr COLORREF VersionLabelColor = RGB(128, 128, 128);

    /// Marks the row type, so it has to stay a tint of whatever the list actually paints rather
    /// than a colour of its own - pulling red and blue down leaves a green cast on any base.
    COLORREF CustomActionRowColor() {
        const COLORREF background = GetSysColor(COLOR_WINDOW);
        return RGB(GetRValue(background) * 93 / 100, GetGValue(background),
                   GetBValue(background) * 93 / 100);
    }

    /**
     * @brief Splits the client width across the columns - no horizontal scrolling, long commands
     * get ellipsized and hovering unfolds the full text.
     *
     * GetClientRect already excludes the vertical scrollbar, so this has to run again after every
     * fill: guessing the scrollbar width instead leaves a dead gap while the list still fits.
     */
    void LayoutActionColumns(HWND hwndList) {
        // The hotkey column carries the combination, the press count and the release marker,
        // so it is the one that must not truncate
        constexpr int percents[] = {32, 38, 0}; // the last one takes what is left

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
    }

    /**
     * @brief Reloads the About page's log box from the file.
     *
     * Wholesale rather than appending the one new line: only errors are logged, so this runs
     * almost never, and it means the page can be refreshed by a bare message with no payload to
     * marshal from whichever thread produced the line.
     */
    void ReloadLogText(HWND page) {
        HWND edit = GetDlgItem(page, IDC_ABOUT_LOG_LIST);
        if (!edit) {
            return;
        }

        SetWindowTextW(edit, Str::Utf8ToWide(Logger::GetLogText()).c_str());

        const int length = GetWindowTextLengthW(edit);
        SendMessage(edit, EM_SETSEL, length, length);
        SendMessage(edit, EM_SCROLLCARET, 0, 0);
    }

    /// The page's own scroll bar, shown by SettingsRows::Build when the rows run past the page.
    bool ScrollPage(HWND page, UINT message, WPARAM wParam) {
        SCROLLINFO info{.cbSize = sizeof(info), .fMask = SIF_ALL};
        if (!(GetWindowLongW(page, GWL_STYLE) & WS_VSCROLL) || !GetScrollInfo(page, SB_VERT, &info)) {
            return false;
        }
        RECT line{0, 0, 0, 14}; // a check row and its gap, in dialog units
        MapDialogRect(page, &line);

        int pos = info.nPos;
        if (message == WM_MOUSEWHEEL) {
            pos -= MulDiv(GET_WHEEL_DELTA_WPARAM(wParam), line.bottom * 3, WHEEL_DELTA);
        } else {
            switch (LOWORD(wParam)) {
                case SB_LINEUP:     pos -= line.bottom; break;
                case SB_LINEDOWN:   pos += line.bottom; break;
                case SB_PAGEUP:     pos -= static_cast<int>(info.nPage); break;
                case SB_PAGEDOWN:   pos += static_cast<int>(info.nPage); break;
                case SB_THUMBTRACK: pos = info.nTrackPos; break;
                case SB_TOP:        pos = info.nMin; break;
                case SB_BOTTOM:     pos = info.nMax; break;
                default:            break;
            }
        }
        pos = std::clamp(pos, info.nMin, std::max(info.nMin, info.nMax - static_cast<int>(info.nPage) + 1));

        if (pos != info.nPos) {
            SetScrollPos(page, SB_VERT, pos, TRUE);
            ScrollWindowEx(page, 0, info.nPos - pos, nullptr, nullptr, nullptr, nullptr,
                           SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
        }
        return true;
    }

    /// One category page. Forwards its input to the view model through the owning window.
    INT_PTR CALLBACK ChildDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* window = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        switch (message) {
            case WM_INITDIALOG:
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, lParam);
                return TRUE;

            case SettingsWindow::WM_LOG_REFRESH:
                ReloadLogText(hwnd);
                return TRUE;

            case WM_COMMAND:
            case WM_HSCROLL:
                if (window && window->OnPageInput) {
                    window->OnPageInput(hwnd, message, wParam, lParam);
                }
                break;

            case WM_VSCROLL:
                // A control's own bar names itself; the page's bar sends no handle
                return !lParam && ScrollPage(hwnd, message, wParam);

            case WM_MOUSEWHEEL:
                return ScrollPage(hwnd, message, wParam);

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
                    // Tint the rows that launch a command line, so they stand apart from the rest
                    auto* draw = reinterpret_cast<LPNMLVCUSTOMDRAW>(lParam);
                    LRESULT result = CDRF_DODEFAULT;

                    if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) {
                        result = CDRF_NOTIFYITEMDRAW;
                    } else if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT && draw->nmcd.lItemlParam) {
                        draw->clrTextBk = CustomActionRowColor();
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

SettingsWindow::SettingsWindow(HINSTANCE hInstance)
    : BaseWindow(hInstance)
{
}

bool SettingsWindow::Initialize(const Config& config) {
    _parent = FromHandle(config.parentHwnd);

    if (!_parent) {
        return false;
    }

    _config = config;
    _viewModel->Init();
    return true;
}

void SettingsWindow::Show() {
    if (!_hwnd) {
        INITCOMMONCONTROLSEX controls{sizeof(INITCOMMONCONTROLSEX), ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES};
        InitCommonControlsEx(&controls);

        // Modeless: the tray app keeps pumping its own message loop while this is open
        CreateDialogParamW(_hInstance, MAKEINTRESOURCEW(IDD_SETTINGS_MAIN), _config.parentHwnd,
                           SettingsDialogProc, reinterpret_cast<LPARAM>(this));

        if (!_hwnd) {
            MessageBoxW(nullptr, L"Failed to create the settings window.", L"Error", MB_ICONERROR | MB_OK);
            return;
        }

        ShowWindow(_hwnd, SW_SHOW);
    }

    SetForegroundWindow(_hwnd);
    _isVisible = true;
}

void SettingsWindow::Hide() {
    if (_hwnd) {
        _close(_hwnd);
    }
}

INT_PTR CALLBACK SettingsWindow::SettingsDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_INITDIALOG) {
        auto* created = reinterpret_cast<SettingsWindow*>(lParam);
        created->RegisterWindow(hwnd);
        return created->OnInitDialog();
    }

    auto* window = static_cast<SettingsWindow*>(FromHandle(hwnd));
    if (!window) {
        return FALSE;
    }

    switch (message) {
        case WM_COMMAND:        return window->OnCommand(wParam);
        case WM_NOTIFY:         return window->OnNotify(lParam);
        case WM_CTLCOLORSTATIC: return window->OnCtlColorStatic(wParam, lParam);
        case WM_CLOSE:          window->Close(); return TRUE;
        case WM_DESTROY:        return window->OnDestroy();
        default:                return FALSE;
    }
}

INT_PTR SettingsWindow::OnInitDialog() {
    _hwndTreeView = GetDlgItem(_hwnd, IDC_SETTINGS_TREE);
    _hwndGroupBox = GetDlgItem(_hwnd, IDC_SETTINGS_GROUPBOX);

    if (UAC::IsElevated()) {
        SetWindowTextW(_hwnd, APP_NAME L" - settings (Administrator)");
    }

    HICON icon = LoadIconW(_hInstance, MAKEINTRESOURCEW(IDI_APP));
    SendMessageW(_hwnd, WM_SETICON, ICON_BIG, (LPARAM)icon);
    SendMessageW(_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);

    SetDlgItemTextW(_hwnd, IDC_SETTINGS_VERSION, Str::Utf8ToWide(g_AppVersion.GetFullFormat()).c_str());

    // Hand cursor over the categories
    SetWindowSubclass(_hwndTreeView, TreeViewSubclassProc, 0, reinterpret_cast<DWORD_PTR>(this));
    PopulateTreeView();
    return TRUE;
}

INT_PTR SettingsWindow::OnCommand(WPARAM wParam) {
    const UINT commandId = LOWORD(wParam);

    if (commandId == IDOK) {
        _onApply();
        Close();
        return TRUE;
    }

    if (commandId == IDCANCEL) {
        Close();
        return TRUE;
    }

    return FALSE;
}

INT_PTR SettingsWindow::OnNotify(LPARAM lParam) {
    const auto* header = (LPNMHDR)lParam;

    if (header->idFrom == IDC_SETTINGS_TREE && header->code == TVN_SELCHANGEDW) {
        OnTreeViewSelectionChanged(((LPNMTREEVIEW)lParam)->itemNew.hItem);
    }

    return FALSE;
}

INT_PTR SettingsWindow::OnCtlColorStatic(WPARAM wParam, LPARAM lParam) const {
    if ((HWND)lParam != GetDlgItem(_hwnd, IDC_SETTINGS_VERSION)) {
        return FALSE;
    }

    SetTextColor((HDC)wParam, VersionLabelColor);
    SetBkMode((HDC)wParam, TRANSPARENT);
    return (INT_PTR)GetStockObject(NULL_BRUSH);
}

INT_PTR SettingsWindow::OnDestroy() {
    SetWindowLongPtrW(_hwnd, GWLP_USERDATA, 0);
    _hwnd = nullptr;
    _hwndTreeView = nullptr;
    _hwndGroupBox = nullptr;
    _hwndContentDialog = nullptr;
    _isVisible = false;

    // Last statement: subscribers treat this as "the window is gone" and one of them queues its
    // destruction, so nothing may touch this object afterwards
    _onExit();
    return TRUE;
}

/// One row per registered page, carrying its index - the window has no business knowing one page
/// from another.
void SettingsWindow::PopulateTreeView() const {
    HTREEITEM firstItem = nullptr;

    for (size_t i = 0; i < SettingsHost::Pages.size(); i++) {
        TVINSERTSTRUCTW insert{};
        insert.hParent = TVI_ROOT;
        insert.hInsertAfter = TVI_LAST;
        insert.item.mask = TVIF_TEXT | TVIF_PARAM;
        insert.item.pszText = const_cast<wchar_t*>(SettingsHost::Pages[i].Title);
        insert.item.lParam = static_cast<LPARAM>(i);

        const auto item = (HTREEITEM)SendMessageW(_hwndTreeView, TVM_INSERTITEMW, 0, (LPARAM)&insert);
        firstItem = firstItem ? firstItem : item;
    }

    SendMessageW(_hwndTreeView, TVM_SELECTITEM, TVGN_CARET, (LPARAM)firstItem);
}

void SettingsWindow::OnTreeViewSelectionChanged(HTREEITEM hItem) {
    if (!hItem) {
        return;
    }

    TVITEMW item{};
    item.mask = TVIF_PARAM;
    item.hItem = hItem;

    if (!SendMessageW(_hwndTreeView, TVM_GETITEMW, 0, (LPARAM)&item)) {
        return;
    }

    const auto index = static_cast<size_t>(item.lParam);
    if (index < SettingsHost::Pages.size()) {
        ShowPage(SettingsHost::Pages[index]);
    }
}

void SettingsWindow::ShowPage(const SettingsPage& page) {
    SetWindowTextW(_hwndGroupBox, page.Title);

    if (_hwndContentDialog) {
        DestroyWindow(_hwndContentDialog);
        _hwndContentDialog = nullptr;
    }

    _hwndContentDialog = CreateDialogParamW(_hInstance, MAKEINTRESOURCEW(IDD_SETTINGS_PAGE), _hwndGroupBox,
                                            ChildDialogProc, reinterpret_cast<LPARAM>(this));
    if (!_hwndContentDialog) {
        return;
    }

    // Sized before it is filled: the rows take their width from the page
    UpdateGroupBoxLayout();
    if (OnPageCreated) {
        OnPageCreated(_hwndContentDialog, page);
    }
    ShowWindow(_hwndContentDialog, SW_SHOW);
}

/// Fits the category page into the group box interior, below its title.
void SettingsWindow::UpdateGroupBoxLayout() const {
    RECT groupBox;
    GetClientRect(_hwndGroupBox, &groupBox);

    const UINT dpi = GetDpiForWindow(_hwnd);
    const int padding = MulDiv(8, dpi, 96);
    const int titleHeight = MulDiv(20, dpi, 96);

    SetWindowPos(_hwndContentDialog, nullptr,
                 padding, titleHeight,
                 groupBox.right - padding * 2,
                 groupBox.bottom - titleHeight - padding,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

void SettingsWindow::SetActionRows(const std::vector<ActionRow>& rows) const {
    HWND hwndList = GetDlgItem(_hwndContentDialog, IDC_HOTKEYS_LIST);
    if (!hwndList) {
        return;
    }

    // The list is rebuilt with its page on every visit, so its columns come with its first fill
    if (!Header_GetItemCount(ListView_GetHeader(hwndList))) {
        InitializeActionsList(hwndList);
    }

    SendMessage(hwndList, LVM_DELETEALLITEMS, 0, 0);

    LVITEMW lvi = {};
    lvi.mask = LVIF_TEXT | LVIF_PARAM;
    int index = 0;

    for (const auto& row : rows) {
        const std::wstring cells[] = {Str::Utf8ToWide(row.Name), Str::Utf8ToWide(row.Hotkey),
                                      Str::Utf8ToWide(row.Command)};

        lvi.iItem = index++;
        lvi.iSubItem = 0;
        lvi.lParam = row.RunsCommand; // the custom draw reads it back - no parallel array to keep in sync
        lvi.pszText = const_cast<LPWSTR>(cells[0].c_str());
        const int itemIndex = SendMessageW(hwndList, LVM_INSERTITEMW, 0, (LPARAM)&lvi);

        for (int column = 1; column < static_cast<int>(std::size(cells)); column++) {
            lvi.iItem = itemIndex;
            lvi.iSubItem = column;
            lvi.pszText = const_cast<LPWSTR>(cells[column].c_str());
            SendMessageW(hwndList, LVM_SETITEMTEXTW, itemIndex, (LPARAM)&lvi);
        }
    }

    LayoutActionColumns(hwndList);
}

bool SettingsWindow::ShowActionDialog(ActionEdit& action, std::set<std::string>& recentSounds) const {
    return ActionDialog::Show(_hInstance, _hwnd, action, recentSounds);
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
