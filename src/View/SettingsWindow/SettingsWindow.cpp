#include "SettingsWindow.hpp"

#include <commctrl.h>
#include <string>
#include <uxtheme.h>

#include "../../Resources/Resource.h"

const SettingsWindow::CategoryItem SettingsWindow::categories_[] = {
    {IDD_SETTINGS_GENERAL, L"General"},
    {IDD_SETTINGS_INDICATOR, L"Indicator"},
    {IDD_SETTINGS_SOUNDS, L"Sounds"},
    {IDD_SETTINGS_HOTKEYS, L"Hotkeys"},
    {IDD_SETTINGS_ABOUT, L"About"},
};

const size_t SettingsWindow::categoriesCount_ = sizeof(categories_) / sizeof(CategoryItem);

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
    SetupMessageHandlers();
    _viewModel->Init();
    return true;
}

void SettingsWindow::SetupMessageHandlers() {
    RegisterMessageHandler(WM_INITDIALOG, [this](WPARAM wParam, LPARAM lParam) {
        return OnInitDialog(wParam, lParam);
    });

    RegisterMessageHandler(WM_COMMAND, [this](WPARAM wParam, LPARAM lParam) {
        return OnCommand(wParam, lParam);
    });

    RegisterMessageHandler(WM_NOTIFY, [this](WPARAM wParam, LPARAM lParam) {
        return OnNotify(wParam, lParam);
    });

    RegisterMessageHandler(WM_SIZE, [this](WPARAM wParam, LPARAM lParam) {
        return OnSize(wParam, lParam);
    });

    RegisterMessageHandler(WM_DESTROY, [this](WPARAM wParam, LPARAM lParam) {
        return OnDestroy(wParam, lParam);
    });
}

void SettingsWindow::Show() {
    if (hwnd_) {
        SetForegroundWindow(hwnd_);
        isVisible_ = true;
        return;
    }

    hwnd_ = CreateDialogParamW(
        hInstance_,
        MAKEINTRESOURCEW(IDD_SETTINGS),
        config_.parentHwnd,
        StaticWindowProc,
        reinterpret_cast<LPARAM>(this)
    );

    if (!hwnd_) {
        //print last error
        auto error = GetLastError();
        std::wstring msg = L"Failed to create settings dialog. Error code: " + std::to_wstring(error);
        MessageBoxW(nullptr, msg.c_str(), L"Error", MB_ICONERROR | MB_OK);

        return;
    }

    ShowWindow(hwnd_, SW_SHOW);
    isVisible_ = true;
}

void SettingsWindow::Hide() {
    if (!hwnd_) {
        return;
    }

    _close(hwnd_);
}

LRESULT SettingsWindow::OnInitDialog(WPARAM wParam, LPARAM lParam) {

    // Allow drag move on parent window by removing WS_EX_TRANSPARENT
    LONG_PTR dwExStyle = GetWindowLongPtr(config_.parentHwnd, GWL_EXSTYLE);
    dwExStyle &= ~WS_EX_TRANSPARENT; //Deleting WS_EX_TRANSPARENT
    SetWindowLongPtr(config_.parentHwnd, GWL_EXSTYLE, dwExStyle);

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_TREEVIEW_CLASSES;
    InitCommonControlsEx(&icex);

    CreateTreeView();
    CreateGroupBox();
    PopulateTreeView();

    // Center window on screen
    RECT rect;
    GetWindowRect(hwnd_, &rect);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;

    const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    const int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    const int posX = (screenWidth - width) / 2;
    const int posY = (screenHeight - height) / 2;

    SetWindowPos(hwnd_, nullptr, posX, posY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

    return TRUE;
}

LRESULT SettingsWindow::OnCommand(WPARAM wParam, LPARAM lParam) {
    const UINT commandId = LOWORD(wParam);

    if (commandId == IDOK) {
        OnApply();
        Close();
    } else if (commandId == IDCANCEL) {
        Close();
    }

    return 0;
}

LRESULT SettingsWindow::OnNotify(WPARAM wParam, LPARAM lParam) {
    auto *pnmh = (LPNMHDR)lParam;

    if (pnmh->idFrom == ID_TREEVIEW) {
        if (pnmh->code == TVN_SELCHANGED) {
            LPNMTREEVIEW pnmtv = (LPNMTREEVIEW)lParam;
            OnTreeViewSelectionChanged(pnmtv->itemNew.hItem);
        }
    }

    return 0;
}

LRESULT SettingsWindow::OnSize(WPARAM wParam, LPARAM lParam) {
    if (wParam == SIZE_MINIMIZED) {
        return 0;
    }

    RECT clientRect;
    GetClientRect(hwnd_, &clientRect);

    if (hwndTreeView_) {
        SetWindowPos(hwndTreeView_, nullptr,
                   MARGIN, MARGIN,
                   SIDE_MENU_WIDTH, clientRect.bottom - BUTTON_AREA_HEIGHT - MARGIN * 2,
                   SWP_NOZORDER | SWP_NOACTIVATE);
    }

    if (hwndGroupBox_) {
        const int groupBoxX = MARGIN + SIDE_MENU_WIDTH + MARGIN;
        SetWindowPos(hwndGroupBox_, nullptr,
                   groupBoxX, MARGIN,
                   clientRect.right - groupBoxX - MARGIN,
                   clientRect.bottom - BUTTON_AREA_HEIGHT - MARGIN * 2,
                   SWP_NOZORDER | SWP_NOACTIVATE);
    }

    UpdateGroupBoxLayout();

    return 0;
}

LRESULT SettingsWindow::OnDestroy(WPARAM wParam, LPARAM lParam) {
    SetWindowLongPtr(config_.parentHwnd, GWL_EXSTYLE,
        GetWindowLongPtr(config_.parentHwnd, GWL_EXSTYLE) | WS_EX_TRANSPARENT
    );
    _onExit();
    hwnd_ = nullptr;
    hwndTreeView_ = nullptr;
    hwndGroupBox_ = nullptr;
    hwndContentDialog_ = nullptr;
    isVisible_ = false;
    return 0;
}

void SettingsWindow::CreateTreeView() {
    RECT clientRect;
    GetClientRect(hwnd_, &clientRect);

    // TreeView height = full height - top and bottom padding - button area
    const int treeViewHeight = clientRect.bottom - (MARGIN * 2) - BUTTON_AREA_HEIGHT;

    hwndTreeView_ = CreateWindowExW(
        WS_EX_STATICEDGE,
        WC_TREEVIEWW,
        nullptr,
        WS_VISIBLE | WS_CHILD | TVS_HASLINES |
        TVS_LINESATROOT | TVS_SHOWSELALWAYS |
        TVS_TRACKSELECT | TVS_FULLROWSELECT |
        TVS_DISABLEDRAGDROP,
        MARGIN, MARGIN, SIDE_MENU_WIDTH, treeViewHeight,
        hwnd_,
        (HMENU)ID_TREEVIEW,
        hInstance_,
        NULL
    );

    if (hwndTreeView_) {
        // Setting the subclass to handle the cursor
        SetWindowSubclass(hwndTreeView_, TreeViewSubclassProc, 0, reinterpret_cast<DWORD_PTR>(this));
    }
}

void SettingsWindow::CreateGroupBox() {
    RECT clientRect;
    GetClientRect(hwnd_, &clientRect);

    // Position after TreeView + indent
    constexpr int groupBoxX = MARGIN + SIDE_MENU_WIDTH + MARGIN;
    const int groupBoxWidth = clientRect.right - groupBoxX - MARGIN;
    const int groupBoxHeight = clientRect.bottom - BUTTON_AREA_HEIGHT - MARGIN * 2;

    hwndGroupBox_ = CreateWindowW(
        L"BUTTON",
        L"",
        WS_VISIBLE | WS_CHILD | BS_GROUPBOX,
        groupBoxX, MARGIN,
        groupBoxWidth, groupBoxHeight,
        hwnd_,
        (HMENU)ID_GROUPBOX,
        hInstance_,
        NULL
    );

    if (hwndGroupBox_) {
        // Set the bold font for the groupbox title
        HFONT hFont = CreateFontW(
            -12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"MS Shell Dlg"
        );
        SendMessage(hwndGroupBox_, WM_SETFONT, (WPARAM)hFont, TRUE);
    }
}

void SettingsWindow::PopulateTreeView() {
    if (!hwndTreeView_) {
        return;
    }

    HTREEITEM hFirstItem = nullptr;

    // Add all categories as root elements
    for (size_t i = 0; i < categoriesCount_; ++i) {
        HTREEITEM hItem = AddTreeViewItem(TVI_ROOT, categories_[i]);
        if (i == 0) {
            hFirstItem = hItem;
        }
    }

    // Select the first item by default
    if (hFirstItem) {
        SendMessage(hwndTreeView_, TVM_SELECTITEM, TVGN_CARET, (LPARAM)hFirstItem);
    }
}

HTREEITEM SettingsWindow::AddTreeViewItem(HTREEITEM hParent, const CategoryItem& item) {
    TVITEMW tvi;
    TVINSERTSTRUCTW tvins;

    tvi.mask = TVIF_TEXT | TVIF_PARAM;
    tvi.pszText = const_cast<wchar_t*>(item.name);
    tvi.cchTextMax = wcslen(item.name);
    tvi.lParam = item.resourceId;

    tvins.item = tvi;
    tvins.hInsertAfter = TVI_LAST;
    tvins.hParent = hParent;

    return (HTREEITEM)SendMessage(hwndTreeView_, TVM_INSERTITEMW, 0, (LPARAM)&tvins);
}

void SettingsWindow::OnTreeViewSelectionChanged(HTREEITEM hItem) {
    if (!hItem) {
        return;
    }

    TVITEMW item;
    item.mask = TVIF_PARAM | TVIF_TEXT;
    item.hItem = hItem;

    wchar_t buffer[255];
    item.pszText = buffer;
    item.cchTextMax = sizeof(buffer) / sizeof(wchar_t);

    if (SendMessage(hwndTreeView_, TVM_GETITEMW, 0, (LPARAM)&item)) {
        int resourceId = (int)item.lParam;
        currentCategoryId_ = resourceId;

        SetWindowTextW(hwndGroupBox_, buffer);

        LoadCategoryContent(resourceId);

        if (OnSectionChange) {
            OnSectionChange(hwndContentDialog_, currentCategoryId_);
        }
    }
}

void SettingsWindow::SetActiveCategory(int categoryId) {
    if (!hwndTreeView_) return;

    // Find element by ID
    auto *hItem = (HTREEITEM)SendMessage(hwndTreeView_, TVM_GETNEXTITEM, TVGN_ROOT, 0);

    while (hItem) {
        TVITEM item;
        item.mask = TVIF_PARAM;
        item.hItem = hItem;

        if (SendMessage(hwndTreeView_, TVM_GETITEM, 0, (LPARAM)&item)) {
            if ((int)item.lParam == categoryId) {
                SendMessage(hwndTreeView_, TVM_SELECTITEM, TVGN_CARET, (LPARAM)hItem);
                return;
            }
        }

        hItem = (HTREEITEM)SendMessage(hwndTreeView_, TVM_GETNEXTITEM, TVGN_NEXT, (LPARAM)hItem);
    }
}

LRESULT CALLBACK SettingsWindow::TreeViewSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    switch (uMsg) {
    case WM_SETCURSOR:
        {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);

            TVHITTESTINFO hitTest = {0};
            hitTest.pt = pt;
            auto *hItem = (HTREEITEM)SendMessage(hwnd, TVM_HITTEST, 0, (LPARAM)&hitTest);

            if (hItem && (hitTest.flags & TVHT_ONITEM)) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                return TRUE;
            }
        }
        break;

    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, TreeViewSubclassProc, uIdSubclass);
        break;
    }

    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

// Static dialog procedure for child dialogs
static LRESULT CALLBACK ChildDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    static SettingsWindow* settingsWindow = nullptr;

    switch (message) {
        case WM_INITDIALOG:
            settingsWindow = reinterpret_cast<SettingsWindow*>(lParam);
            break;
        case WM_COMMAND: {
            switch (HIWORD(wParam)) {
                case BN_CLICKED:
                    if (settingsWindow->OnButtonClick) {
                        settingsWindow->OnButtonClick(hwnd, LOWORD(wParam));
                    }
                    break;
                case CBN_SELCHANGE:
                    if (settingsWindow->OnComboBoxChange) {
                        settingsWindow->OnComboBoxChange(hwnd, LOWORD(wParam), SendMessage(hwnd, CB_GETCURSEL, 0, 0));
                    }
                    break;
                // Handle child dialog commands if needed
            }
            break;
        }
        case WM_HSCROLL:
            if (LOWORD(wParam) != TB_ENDTRACK) {
                if (settingsWindow->OnTrackbarChange) {
                    settingsWindow->OnTrackbarChange(hwnd, GetDlgCtrlID((HWND)lParam),
                        SendMessage((HWND)lParam, TBM_GETPOS, 0, 0));
                }
            }
            break;
    }


    return DefWindowProc(hwnd, message, wParam, lParam);
}

void SettingsWindow::LoadCategoryContent(int resourceId) {
    if (hwndContentDialog_) {
        DestroyWindow(hwndContentDialog_);
        hwndContentDialog_ = nullptr;
    }

    if (resourceId == 0) {
        return;
    }

    hwndContentDialog_ = CreateDialogParamW(
        hInstance_,
        MAKEINTRESOURCEW(resourceId),
        hwndGroupBox_,
        ChildDialogProc,
        reinterpret_cast<LPARAM>(this)
    );

    if (hwndContentDialog_) {
        ShowWindow(hwndContentDialog_, SW_SHOW);
        UpdateGroupBoxLayout();
    }
}

void SettingsWindow::UpdateGroupBoxLayout() {
    if (!hwndGroupBox_ || !hwndContentDialog_) {
        return;
    }

    // Get GroupBox dimensions
    RECT groupBoxRect;
    GetClientRect(hwndGroupBox_, &groupBoxRect);

    constexpr int titleHeight = 20;
    SetWindowPos(hwndContentDialog_, NULL,
               GROUPBOX_PADDING, titleHeight + GROUPBOX_PADDING,
               groupBoxRect.right - (GROUPBOX_PADDING * 2),
               groupBoxRect.bottom - titleHeight - (GROUPBOX_PADDING * 2),
               SWP_NOZORDER | SWP_NOACTIVATE);
}
