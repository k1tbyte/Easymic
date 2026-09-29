#include "SettingsWindow.hpp"

#include <algorithm>
#include <commctrl.h>
#include <string>

#include "SettingsPages.hpp"
#include "SettingsRows.hpp"
#include "System/UACService.hpp"
#include "Windowing/Controls.hpp"
#include "Resources/Resource.h"
#include "System/Version.hpp"
#include "Str.hpp"
#include "definitions.h"

namespace {
    constexpr COLORREF VersionLabelColor = RGB(128, 128, 128);

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
        INITCOMMONCONTROLSEX controls{sizeof(INITCOMMONCONTROLSEX),
                                      ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES};
        InitCommonControlsEx(&controls);

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

        case WM_NOTIFY:
            if (const auto* header = reinterpret_cast<const NMHDR*>(lParam);
                window && header->hwndFrom == window->_hwndTabs && header->code == TCN_SELCHANGE) {
                window->ShowTab(TabCtrl_GetCurSel(header->hwndFrom));
                return TRUE;
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

    SetDlgItemTextW(_hwnd, IDC_SETTINGS_VERSION, Str::Utf8ToWide(Version::App().GetFullFormat()).c_str());

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

    const auto commit = [&](const std::span<const SettingsRow> rows) {
        for (const SettingsRow& row : rows) {
            if (row.Commit) {
                row.Commit(_hwnd, _cfg, _cfgPrev);
            }
        }
    };
    for (const SettingsPage& page : SettingsHost::Pages) {
        commit(page.Rows);
        for (const SettingsTab& tab : page.Tabs) {
            commit(tab.Rows);
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
    _hwndTabs = nullptr;
    _hwndTabPage = nullptr;
    _rows = {};
    _tabs = {};

    SettingsPages::Close();
    _cfg = _cfgPrev;

    // Last: a subscriber queues this object's destruction, so nothing may touch it afterwards
    _onExit();
    return TRUE;
}

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
    }
    _hwndTabs = nullptr;
    _hwndTabPage = nullptr;
    _tabs = page.Tabs;
    _rows = page.Rows;

    _hwndContentDialog = CreateDialogParamW(_hInstance, MAKEINTRESOURCEW(IDD_SETTINGS_PAGE), _hwndGroupBox,
                                            PageProc, reinterpret_cast<LPARAM>(this));
    if (!_hwndContentDialog) {
        return;
    }

    UpdateGroupBoxLayout();
    if (_tabs.empty()) {
        SettingsRows::Build(_hwndContentDialog, _rows, _cfg);
    } else {
        RECT client;
        GetClientRect(_hwndContentDialog, &client);
        RECT unit{0, 0, 100, 0};
        MapDialogRect(_hwndContentDialog, &unit);
        _hwndTabs = Controls::Create(_hwndContentDialog, _hwndContentDialog, WC_TABCONTROLW, L"",
                                     WS_TABSTOP | WS_CLIPSIBLINGS,
                                     {0, 0, MulDiv(client.right, 100, unit.right), 14}, 900);
        for (size_t i = 0; i < _tabs.size(); ++i) {
            TCITEMW item{.mask = TCIF_TEXT, .pszText = const_cast<wchar_t*>(_tabs[i].Title)};
            TabCtrl_InsertItem(_hwndTabs, static_cast<int>(i), &item);
        }
        ShowTab(0);
    }
    ShowWindow(_hwndContentDialog, SW_SHOW);
}

void SettingsWindow::ShowTab(const int index) {
    if (index < 0 || static_cast<size_t>(index) >= _tabs.size()) {
        return;
    }
    if (_hwndTabPage) {
        DestroyWindow(_hwndTabPage);
    }
    _rows = _tabs[index].Rows;
    TabCtrl_SetCurSel(_hwndTabs, index);
    _hwndTabPage = CreateDialogParamW(_hInstance, MAKEINTRESOURCEW(IDD_SETTINGS_PAGE), _hwndContentDialog,
                                     PageProc, reinterpret_cast<LPARAM>(this));
    if (!_hwndTabPage) {
        return;
    }

    RECT client;
    GetClientRect(_hwndContentDialog, &client);
    RECT top{0, 0, 0, 18};
    MapDialogRect(_hwndContentDialog, &top);
    SetWindowPos(_hwndTabPage, nullptr, 0, top.bottom, client.right, std::max(0L, client.bottom - top.bottom),
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SettingsRows::Build(_hwndTabPage, _rows, _cfg);
    ShowWindow(_hwndTabPage, SW_SHOW);
}

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
