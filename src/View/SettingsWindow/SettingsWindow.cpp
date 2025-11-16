#include "SettingsWindow.hpp"

#include <commctrl.h>
#include <string>
#include <uxtheme.h>

#include "../../Resources/Resource.h"
#include "../../Lib/Version.hpp"

// Forward declaration
static void InitializeHotkeysList(HWND hwndList);

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
    RegisterMessageHandler(WM_CREATE, [this](WPARAM wParam, LPARAM lParam) {
        return OnCreate(wParam, lParam);
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

    RegisterMessageHandler(WM_CTLCOLORSTATIC, [this](WPARAM wParam, LPARAM lParam) {
        return OnCtlColorStatic(wParam, lParam);
    });
}

void SettingsWindow::RegisterWindowClass() {
    static bool classRegistered = false;
    if (classRegistered) {
        return;
    }

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = StaticWindowProc; // Use our own proc instead of StaticWindowProc
    wc.hInstance = hInstance_;
    wc.hIcon = LoadIcon(hInstance_, MAKEINTRESOURCE(IDI_APP));
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_3DFACE + 1); // Standard dialog background
    wc.lpszClassName = L"SettingsWindow";
    wc.hIconSm = LoadIcon(hInstance_, MAKEINTRESOURCE(IDI_APP));

    if (!RegisterClassExW(&wc)) {
        auto error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            std::wstring msg = L"Failed to register window class. Error code: " + std::to_wstring(error);
            MessageBoxW(nullptr, msg.c_str(), L"Error", MB_ICONERROR | MB_OK);
        }
        return;
    }

    classRegistered = true;
}



void SettingsWindow::CreateMainButtons() {
    if (!hwnd_) {
        return;
    }

    // Get client area
    RECT clientRect;
    GetClientRect(hwnd_, &clientRect);

    static constexpr int BUTTON_WIDTH = 75;
    static constexpr int BUTTON_HEIGHT = 23;

    // Create version label in bottom left corner (smaller and grayer)
    hwndVersionLabel_ = CreateWindowW(
        L"STATIC",
        L"", // Will be updated with actual version
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        MARGIN, clientRect.bottom - BUTTON_HEIGHT - MARGIN,
        150, BUTTON_HEIGHT,
        hwnd_,
        nullptr,
        hInstance_,
        nullptr
    );

    // Create OK button
    hwndOkButton_ = CreateWindowW(
        L"BUTTON",
        L"OK",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
        clientRect.right - BUTTON_WIDTH * 2 - MARGIN * 2, clientRect.bottom - BUTTON_HEIGHT - MARGIN,
        BUTTON_WIDTH, BUTTON_HEIGHT,
        hwnd_,
        (HMENU)IDOK,
        hInstance_,
        nullptr
    );

    // Create Cancel button
    hwndCancelButton_ = CreateWindowW(
        L"BUTTON",
        L"Cancel",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        clientRect.right - BUTTON_WIDTH - MARGIN, clientRect.bottom - BUTTON_HEIGHT - MARGIN,
        BUTTON_WIDTH, BUTTON_HEIGHT,
        hwnd_,
        (HMENU)IDCANCEL,
        hInstance_,
        nullptr
    );

    // Set fonts
    HFONT hButtonFont = CreateFontW(
        -11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Ms Shell Dlg"
    );

    // Smaller font for version label
    HFONT hVersionFont = CreateFontW(
        -8, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Ms Shell Dlg"
    );

    if (hwndOkButton_) {
        SendMessage(hwndOkButton_, WM_SETFONT, (WPARAM)hButtonFont, TRUE);
    }
    if (hwndCancelButton_) {
        SendMessage(hwndCancelButton_, WM_SETFONT, (WPARAM)hButtonFont, TRUE);
    }
    if (hwndVersionLabel_) {
        SendMessage(hwndVersionLabel_, WM_SETFONT, (WPARAM)hVersionFont, TRUE);
    }
}

void SettingsWindow::Show() {
    if (hwnd_) {
        SetForegroundWindow(hwnd_);
        isVisible_ = true;
        return;
    }

    // Register window class if not already registered
    RegisterWindowClass();

    // Create window with same dimensions as IDD_SETTINGS (270x170 dialog units)
    // Convert dialog units to pixels
    RECT rect = {0, 0, 422, 315};


    const int width = rect.right;
    const int height = rect.bottom;

    // Center window on screen
    const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    const int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    const int posX = (screenWidth - width) / 2;
    const int posY = (screenHeight - height) / 2;

    hwnd_ = CreateWindowExW(
        0,                              // Extended window style
        L"SettingsWindow",              // Window class name
        L"Easymic - settings",          // Window title (same as CAPTION in resource)
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, // Same style as DS_MODALFRAME but for CreateWindow
        posX, posY,                     // Position
        width, height,                  // Size
        config_.parentHwnd,             // Parent window
        nullptr,                        // Menu
        hInstance_,                     // Instance handle
        this                            // Creation parameter
    );

    if (!hwnd_) {
        auto error = GetLastError();
        std::wstring msg = L"Failed to create settings window. Error code: " + std::to_wstring(error);
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

LRESULT SettingsWindow::OnCreate(WPARAM wParam, LPARAM lParam) {
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icex);

    // Create gray brush for version label
    hGrayBrush_ = CreateSolidBrush(RGB(128, 128, 128));

    // Create OK and Cancel buttons manually with same positions as in resource
    CreateMainButtons();
    
    // Update version label with actual version info
    UpdateVersionLabel();

    CreateTreeView();
    CreateGroupBox();
    PopulateTreeView();

    return 0; // Return 0 for WM_CREATE (success)
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
        if (pnmh->code == TVN_SELCHANGEDW) {
            LPNMTREEVIEW pnmtv = (LPNMTREEVIEW)lParam;
            OnTreeViewSelectionChanged(pnmtv->itemNew.hItem);
        }
    }

    return FALSE;
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
    _onExit();
    
    // Clean up gray brush
    if (hGrayBrush_) {
        DeleteObject(hGrayBrush_);
        hGrayBrush_ = nullptr;
    }
    
    hwnd_ = nullptr;
    hwndTreeView_ = nullptr;
    hwndGroupBox_ = nullptr;
    hwndContentDialog_ = nullptr;
    hwndOkButton_ = nullptr;
    hwndCancelButton_ = nullptr;
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

void SettingsWindow::SetHotkeyCellValue(int index, LPCSTR value) {
    HWND hwndList = GetDlgItem(hwndContentDialog_, IDC_HOTKEYS_LIST);
    if (!hwndList) {
        return;
    }

    LVITEMA lvi = {0};
    lvi.iItem = index;
    lvi.iSubItem = 1; // Assuming hotkey is in the second column
    lvi.mask = LVIF_TEXT;
    lvi.pszText = const_cast<LPSTR>(value);

    SendMessageA(hwndList, LVM_SETITEMA, 0, (LPARAM)&lvi);
}

void SettingsWindow::SetHotkeySectionTitle(const wchar_t *title) {
    HWND hwndTitle = GetDlgItem(hwndContentDialog_, IDC_HOTKEYS_TITLE);
    if (!hwndTitle) {
        return;
    }
    SetWindowTextW(hwndTitle, title == nullptr ? L"Double-click to set up a hotkey:" : title);
}

void SettingsWindow::ResetHotkeyCellValue(LPCSTR actionTitle) {
    const auto *hotkeyPtr = reinterpret_cast<char**>(&HotkeyTitles);

    for (int i = 0; i < sizeof(HotkeyTitles) / sizeof(char*); i++) {
        if (strcmp(hotkeyPtr[i], actionTitle) == 0) {
            SetHotkeyCellValue(i, "");
            return;
        }
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
            // Initialize ListView for hotkeys dialog
            if (GetDlgItem(hwnd, IDC_HOTKEYS_LIST)) {
                ::InitializeHotkeysList(GetDlgItem(hwnd, IDC_HOTKEYS_LIST));
            }
            break;
        case WM_COMMAND: {
            switch (HIWORD(wParam)) {
                case BN_CLICKED:
                    if (settingsWindow->OnButtonClick) {
                        settingsWindow->OnButtonClick(hwnd, LOWORD(wParam));
                        return TRUE;
                    }
                    break;
                case CBN_SELCHANGE:
                    if (settingsWindow->OnComboBoxChange) {
                        settingsWindow->OnComboBoxChange(hwnd, LOWORD(wParam));
                        return TRUE;
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
        case WM_NOTIFY: {
            LPNMHDR pnmh = (LPNMHDR)lParam;
            if (pnmh->idFrom == IDC_HOTKEYS_LIST) {
                switch (pnmh->code) {
                    case NM_DBLCLK: {
                        // Handle double click on hotkey item
                        LPNMITEMACTIVATE pnmia = (LPNMITEMACTIVATE)lParam;
                        if (pnmia->iItem != -1 && settingsWindow->OnHotkeyListChange) {
                            settingsWindow->OnHotkeyListChange(hwnd, pnmia->iItem, reinterpret_cast<char**>(&HotkeyTitles)[pnmia->iItem]);
                        }
                        break;
                    }
                }
            }
            break;
        }
    }


    return FALSE;
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

static void InitializeHotkeysList(HWND hwndList) {
    if (!hwndList) {
        return;
    }

    // Enable full row selection and grid lines
    DWORD dwExStyle = LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES;
    SendMessage(hwndList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, dwExStyle);

    // Add columns
    LVCOLUMNW lvc = {};
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

    // First column - Action name
    lvc.iSubItem = 0;
    lvc.cxMin = 100;
    lvc.pszText = const_cast<wchar_t*>(L"Action");
    lvc.cx = 120;
    SendMessage(hwndList, LVM_INSERTCOLUMNW, 0, (LPARAM)&lvc);

    // Second column - Key combination
    lvc.iSubItem = 1;
    lvc.pszText = const_cast<wchar_t*>(L"Key Combination");
    lvc.cx = 130;
    SendMessage(hwndList, LVM_INSERTCOLUMNW, 1, (LPARAM)&lvc);

    // Add sample data for UI demonstration
    LVITEMA lvi = {};
    lvi.mask = LVIF_TEXT;

    const auto *hotkeyPtr = reinterpret_cast<char**>(&HotkeyTitles);

    for (int i = 0; i < sizeof(HotkeyTitles) / sizeof(char*); i++) {
        lvi.iItem = i;
        lvi.iSubItem = 0;
        lvi.pszText = hotkeyPtr[i];
        int itemIndex = SendMessageA(hwndList, LVM_INSERTITEMA, 0, (LPARAM)&lvi);

        lvi.iItem = itemIndex;
        lvi.iSubItem = 1;
        lvi.pszText = (char*)"";
        SendMessageA(hwndList, LVM_SETITEMTEXTA, itemIndex, (LPARAM)&lvi);
        /*printf("Hotkey title: %s\n", title);*/
    }
}

void SettingsWindow::UpdateVersionLabel() {
    if (!hwndVersionLabel_) {
        return;
    }
    
    // Get version from global Version instance and set it as window text
    std::string version = g_AppVersion.GetFullFormat();
    std::wstring wversion(version.begin(), version.end());
    SetWindowTextW(hwndVersionLabel_, wversion.c_str());
}

LRESULT SettingsWindow::OnCtlColorStatic(WPARAM wParam, LPARAM lParam) {
    HWND hStatic = (HWND)lParam;
    
    // Check if this is our version label
    if (hStatic == hwndVersionLabel_) {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, RGB(128, 128, 128)); // Gray text color
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)GetStockObject(NULL_BRUSH); // Transparent background
    }
    
    return DefWindowProc(hwnd_, WM_CTLCOLORSTATIC, wParam, lParam);
}

