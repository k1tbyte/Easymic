#include "SettingsWindow.hpp"

#include <algorithm>
#include <commctrl.h>
#include <string>

#include "SettingsPages.hpp"
#include "SettingsRows.hpp"
#include "UACService.hpp"
#include "Resources/Resource.h"
#include "Version.hpp"
#include "Str.hpp"

namespace {
    constexpr COLORREF VersionLabelColor = RGB(128, 128, 128);

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
}

SettingsWindow::SettingsWindow(HINSTANCE hInstance, HWND owner, AppConfig& config)
    : BaseWindow(hInstance), _cfg(config), _cfgPrev(config), _owner(owner)
{
    SettingsPages::Open();
}

void SettingsWindow::Show() {
    if (!_hwnd) {
        INITCOMMONCONTROLSEX controls{sizeof(INITCOMMONCONTROLSEX), ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES};
        InitCommonControlsEx(&controls);

        // Modeless: the tray app keeps pumping its own message loop while this is open
        CreateDialogParamW(_hInstance, MAKEINTRESOURCEW(IDD_SETTINGS_MAIN), _owner,
                           SettingsDialogProc, reinterpret_cast<LPARAM>(this));

        if (!_hwnd) {
            MessageBoxW(nullptr, L"Failed to create the settings window.", L"Error", MB_ICONERROR | MB_OK);
            // No WM_DESTROY will say so, and the frame waits for this to resume the app
            _onExit();
            return;
        }

        ShowWindow(_hwnd, SW_SHOW);
    }

    SetForegroundWindow(_hwnd);
    _isVisible = true;
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

/// One category page: its controls' input goes to its rows.
INT_PTR CALLBACK SettingsWindow::PageProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* window = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (message) {
        case WM_INITDIALOG:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, lParam);
            return TRUE;

        case WM_COMMAND:
        case WM_HSCROLL:
            if (window) {
                SettingsRows::Handle(hwnd, window->_rows, window->_cfg, message, wParam, lParam);
            }
            break;

        case WM_VSCROLL:
            // A control's own bar names itself; the page's bar sends no handle
            return !lParam && ScrollPage(hwnd, message, wParam);

        case WM_MOUSEWHEEL:
            return ScrollPage(hwnd, message, wParam);
    }

    return FALSE;
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
        Apply();
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

void SettingsWindow::Apply() {
    _onApply();

    // Every page's rows, not only the open one's - a choice made on a page left earlier is still
    // waiting for this
    for (const SettingsPage& page : SettingsHost::Pages) {
        for (const SettingsRow& row : page.Rows) {
            if (row.Commit) {
                row.Commit(_hwnd, _cfg, _cfgPrev);
            }
        }
    }

    if (_cfg != _cfgPrev) {
        _cfg.Save();
        _cfgPrev = _cfg;
    }
}

INT_PTR SettingsWindow::OnDestroy() {
    SetWindowLongPtrW(_hwnd, GWLP_USERDATA, 0);
    _hwnd = nullptr;
    _hwndTreeView = nullptr;
    _hwndGroupBox = nullptr;
    _hwndContentDialog = nullptr;
    _isVisible = false;

    SettingsPages::Close();
    _cfg = _cfgPrev;

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
                                            PageProc, reinterpret_cast<LPARAM>(this));
    if (!_hwndContentDialog) {
        return;
    }

    // Sized before it is filled: the rows take their width from the page
    UpdateGroupBoxLayout();
    _rows = page.Rows;
    SettingsRows::Build(_hwndContentDialog, _rows, _cfg);
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
