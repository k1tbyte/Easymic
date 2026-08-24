#include "SettingsWindow.hpp"

#include <commctrl.h>
#include <shellscalingapi.h>
#include <string>
#include <uxtheme.h>

#include "UACService.hpp"
#include "../../Resources/Resource.h"
#include "../../Audio/AudioFileValidator.hpp"
#include "../../Lib/HotkeyManager.hpp"
#include "../../Lib/Utils.hpp"
#include "../../Lib/Version.hpp"

// Forward declaration
static void InitializeActionsList(HWND hwndList);

namespace {
    /// Desaturated on purpose - it marks the row type, it is not a status
    constexpr COLORREF CustomActionRowColor = RGB(237, 246, 237);

    struct ActionDialogState {
        CustomAction* action = nullptr;
        std::set<std::string>* recentSounds = nullptr;
        bool allowDelete = false;
        bool deleted = false;
        uint64_t mask = 0;
        uint64_t captured = 0;
        bool capturing = false;
    };

    void SetHotkeyButtonText(HWND dialog, const uint64_t mask) {
        SetDlgItemTextA(dialog, IDC_ACTION_HOTKEY,
                        mask ? HotkeyManager::GetHotkeyName(mask).c_str() : "Click to bind");
    }

    void StopCapture(ActionDialogState& state) {
        if (!state.capturing) {
            return;
        }

        state.capturing = false;
        HotkeyManager::BindStop();
        HotkeyManager::Dispose();
    }

    void StartCapture(HWND dialog, ActionDialogState& state) {
        if (state.capturing) {
            return;
        }

        state.capturing = true;
        state.captured = 0;
        HotkeyManager::Initialize();
        SetDlgItemTextA(dialog, IDC_ACTION_HOTKEY, "Press desired key combination or ESC to clear...");

        HotkeyManager::BindStart([dialog, &state](uint8_t vkCode, Keys::State keyState,
                                                  uint64_t sequenceMask, const std::string& hotkeyName) {
            if (keyState != Keys::State::KEY_RELEASED) {
                state.captured = sequenceMask;
                SetDlgItemTextA(dialog, IDC_ACTION_HOTKEY, hotkeyName.c_str());
                return;
            }

            // The key that activated the button was pressed before the hooks existed - only its
            // release arrives here, and it must not end the capture before anything was typed
            if (state.captured == 0 && vkCode != VK_ESCAPE) {
                return;
            }

            if (vkCode == VK_ESCAPE && sequenceMask == 0) {
                state.captured = 0;
            }

            state.mask = state.captured;
            StopCapture(state);
            SetHotkeyButtonText(dialog, state.mask);
        });
    }

    INT_PTR CALLBACK ActionDialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* state = reinterpret_cast<ActionDialogState*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));

        switch (message) {
            case WM_INITDIALOG: {
                state = reinterpret_cast<ActionDialogState*>(lParam);
                SetWindowLongPtrW(dialog, GWLP_USERDATA, lParam);

                SetDlgItemTextA(dialog, IDC_ACTION_NAME, state->action->Name.c_str());
                SetDlgItemTextA(dialog, IDC_ACTION_COMMAND, state->action->Command.c_str());
                CheckDlgButton(dialog, IDC_ACTION_ON_RELEASE, state->action->OnRelease ? BST_CHECKED : BST_UNCHECKED);
                SetHotkeyButtonText(dialog, state->mask);
                Utils::PopulateSourceComboBox(GetDlgItem(dialog, IDC_ACTION_SOUND),
                                              *state->recentSounds, state->action->Sound, "None");
                ShowWindow(GetDlgItem(dialog, IDC_ACTION_DELETE), state->allowDelete ? SW_SHOW : SW_HIDE);
                Utils::CenterWindowOnScreen(dialog);
                return TRUE;
            }
            case WM_COMMAND: {
                if (!state) {
                    break;
                }

                switch (LOWORD(wParam)) {
                    case IDC_ACTION_HOTKEY:
                        StartCapture(dialog, *state);
                        return TRUE;

                    case IDC_ACTION_SOUND_BROWSE: {
                        std::string selected;
                        if (AudioFileValidator::PickValidWavFile(dialog, "Select action sound file", selected)) {
                            Utils::AddToRecentSources(*state->recentSounds, selected);
                            Utils::PopulateSourceComboBox(GetDlgItem(dialog, IDC_ACTION_SOUND),
                                                          *state->recentSounds, selected, "None");
                        }
                        return TRUE;
                    }

                    case IDC_ACTION_DELETE:
                        StopCapture(*state);
                        state->deleted = true;
                        EndDialog(dialog, TRUE);
                        return TRUE;

                    case IDOK: {
                        StopCapture(*state);

                        char buffer[512];
                        GetDlgItemTextA(dialog, IDC_ACTION_NAME, buffer, sizeof(buffer));
                        state->action->Name = buffer;
                        GetDlgItemTextA(dialog, IDC_ACTION_COMMAND, buffer, sizeof(buffer));
                        state->action->Command = buffer;
                        state->action->OnRelease = IsDlgButtonChecked(dialog, IDC_ACTION_ON_RELEASE) == BST_CHECKED;
                        state->action->Hotkey = state->mask;
                        state->action->Sound = Utils::ResolveSourceFromComboBox(
                            GetDlgItem(dialog, IDC_ACTION_SOUND), *state->recentSounds);

                        if (state->action->Name.empty() || state->action->Command.empty()) {
                            MessageBoxW(dialog, L"Name and command are required.", L"Action", MB_OK | MB_ICONWARNING);
                            return TRUE;
                        }

                        EndDialog(dialog, TRUE);
                        return TRUE;
                    }

                    case IDCANCEL:
                        StopCapture(*state);
                        EndDialog(dialog, FALSE);
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

    RegisterMessageHandler(WM_DPICHANGED, [this](WPARAM wParam, LPARAM lParam) {
        return OnDpiChanged(wParam, lParam);
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

    const int bw = ButtonWidth();
    const int bh = ButtonHeight();
    const int mg = Margin();

    // Create version label in bottom left corner (smaller and grayer)
    hwndVersionLabel_ = CreateWindowW(
        L"STATIC",
        L"", // Will be updated with actual version
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        mg, clientRect.bottom - bh - mg,
        Scale(150, dpi_), bh,
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
        clientRect.right - bw * 2 - mg * 2, clientRect.bottom - bh - mg,
        bw, bh,
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
        clientRect.right - bw - mg, clientRect.bottom - bh - mg,
        bw, bh,
        hwnd_,
        (HMENU)IDCANCEL,
        hInstance_,
        nullptr
    );

    RebuildFonts();
}

void SettingsWindow::Show() {
    if (hwnd_) {
        SetForegroundWindow(hwnd_);
        isVisible_ = true;
        return;
    }

    // Register window class if not already registered
    RegisterWindowClass();

    // GetDpiForSystem() returns DPI at process start — stale if user changed scaling.
    // GetDpiForMonitor gives the live DPI of the monitor where the window will appear.
    {
        const POINT center = { GetSystemMetrics(SM_CXSCREEN) / 2, GetSystemMetrics(SM_CYSCREEN) / 2 };
        HMONITOR hMon = MonitorFromPoint(center, MONITOR_DEFAULTTOPRIMARY);
        UINT dpiX = 96, dpiY = 96;
        GetDpiForMonitor(hMon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
        dpi_ = dpiX;
    }
    const int width  = Scale(422, dpi_);
    const int height = Scale(315, dpi_);

    // Center window on screen
    const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    const int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    const int posX = (screenWidth - width) / 2;
    const int posY = (screenHeight - height) / 2;

    hwnd_ = CreateWindowExW(
        0,                              // Extended window style
        L"SettingsWindow",              // Window class name
        (std::wstring(L"Easymic - settings") + (UAC::IsElevated() ? L" (Administrator)" : L"")).c_str(),
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

    dpi_ = GetDpiForWindow(hwnd_);
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

    const int mg  = Margin();
    const int smw = SideMenuWidth();
    const int bah = ButtonAreaHeight();
    const int bw  = ButtonWidth();
    const int bh  = ButtonHeight();

    if (hwndTreeView_) {
        SetWindowPos(hwndTreeView_, nullptr,
                   mg, mg,
                   smw, clientRect.bottom - bah - mg * 2,
                   SWP_NOZORDER | SWP_NOACTIVATE);
    }

    if (hwndGroupBox_) {
        const int groupBoxX = mg + smw + mg;
        SetWindowPos(hwndGroupBox_, nullptr,
                   groupBoxX, mg,
                   clientRect.right - groupBoxX - mg,
                   clientRect.bottom - bah - mg * 2,
                   SWP_NOZORDER | SWP_NOACTIVATE);
    }

    if (hwndOkButton_) {
        SetWindowPos(hwndOkButton_, nullptr,
                   clientRect.right - bw * 2 - mg * 2, clientRect.bottom - bh - mg,
                   bw, bh,
                   SWP_NOZORDER | SWP_NOACTIVATE);
    }

    if (hwndCancelButton_) {
        SetWindowPos(hwndCancelButton_, nullptr,
                   clientRect.right - bw - mg, clientRect.bottom - bh - mg,
                   bw, bh,
                   SWP_NOZORDER | SWP_NOACTIVATE);
    }

    if (hwndVersionLabel_) {
        SetWindowPos(hwndVersionLabel_, nullptr,
                   mg, clientRect.bottom - bh - mg,
                   Scale(150, dpi_), bh,
                   SWP_NOZORDER | SWP_NOACTIVATE);
    }

    UpdateGroupBoxLayout();

    return 0;
}

LRESULT SettingsWindow::OnDestroy(WPARAM wParam, LPARAM lParam) {
    _onExit();
    
    // Clean up GDI resources
    if (hGrayBrush_) {
        DeleteObject(hGrayBrush_);
        hGrayBrush_ = nullptr;
    }
    if (hButtonFont_) {
        DeleteObject(hButtonFont_);
        hButtonFont_ = nullptr;
    }
    if (hVersionFont_) {
        DeleteObject(hVersionFont_);
        hVersionFont_ = nullptr;
    }
    if (hGroupBoxFont_) {
        DeleteObject(hGroupBoxFont_);
        hGroupBoxFont_ = nullptr;
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

    const int mg  = Margin();
    const int smw = SideMenuWidth();

    // TreeView height = full height - top and bottom padding - button area
    const int treeViewHeight = clientRect.bottom - (mg * 2) - ButtonAreaHeight();

    hwndTreeView_ = CreateWindowExW(
        WS_EX_STATICEDGE,
        WC_TREEVIEWW,
        nullptr,
        WS_VISIBLE | WS_CHILD | TVS_HASLINES |
        TVS_LINESATROOT | TVS_SHOWSELALWAYS |
        TVS_TRACKSELECT | TVS_FULLROWSELECT |
        TVS_DISABLEDRAGDROP,
        mg, mg, smw, treeViewHeight,
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

    const int mg  = Margin();
    const int smw = SideMenuWidth();

    const int groupBoxX      = mg + smw + mg;
    const int groupBoxWidth  = clientRect.right - groupBoxX - mg;
    const int groupBoxHeight = clientRect.bottom - ButtonAreaHeight() - mg * 2;

    hwndGroupBox_ = CreateWindowW(
        L"BUTTON",
        L"",
        WS_VISIBLE | WS_CHILD | BS_GROUPBOX,
        groupBoxX, mg,
        groupBoxWidth, groupBoxHeight,
        hwnd_,
        (HMENU)ID_GROUPBOX,
        hInstance_,
        NULL
    );

    if (hwndGroupBox_) {
        hGroupBoxFont_ = CreateFontW(
            Scale(-12, dpi_), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"MS Shell Dlg"
        );
        SendMessage(hwndGroupBox_, WM_SETFONT, (WPARAM)hGroupBoxFont_, TRUE);
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

void SettingsWindow::SetActionRows(const std::vector<ActionRow>& rows) {
    HWND hwndList = GetDlgItem(hwndContentDialog_, IDC_HOTKEYS_LIST);
    if (!hwndList) {
        return;
    }

    SendMessage(hwndList, LVM_DELETEALLITEMS, 0, 0);
    customRows_.clear();

    LVITEMA lvi = {};
    lvi.mask = LVIF_TEXT;

    for (const auto& row : rows) {
        const char* cells[] = {row.Name.c_str(), row.Hotkey.c_str(), row.Command.c_str()};

        lvi.iItem = static_cast<int>(customRows_.size());
        lvi.iSubItem = 0;
        lvi.pszText = const_cast<LPSTR>(cells[0]);
        const int itemIndex = SendMessageA(hwndList, LVM_INSERTITEMA, 0, (LPARAM)&lvi);

        for (int column = 1; column < static_cast<int>(std::size(cells)); column++) {
            lvi.iItem = itemIndex;
            lvi.iSubItem = column;
            lvi.pszText = const_cast<LPSTR>(cells[column]);
            SendMessageA(hwndList, LVM_SETITEMTEXTA, itemIndex, (LPARAM)&lvi);
        }

        customRows_.push_back(row.IsCustom);
    }
}

bool SettingsWindow::IsCustomActionRow(int index) const {
    return index >= 0 && index < static_cast<int>(customRows_.size()) && customRows_[index];
}

bool SettingsWindow::ShowActionDialog(CustomAction& action, std::set<std::string>& recentSounds,
                                     const bool allowDelete, bool& deleted) {
    ActionDialogState state{.action = &action, .recentSounds = &recentSounds,
                            .allowDelete = allowDelete, .mask = action.Hotkey};

    const INT_PTR result = DialogBoxParamW(hInstance_, MAKEINTRESOURCEW(IDD_ACTION_EDIT), hwnd_,
                                           ActionDialogProc, reinterpret_cast<LPARAM>(&state));
    deleted = state.deleted;
    return result == TRUE;
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
    SettingsWindow* settingsWindow = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (message) {
        case WM_INITDIALOG:
            settingsWindow = reinterpret_cast<SettingsWindow*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(settingsWindow));
            // Initialize ListView for hotkeys dialog
            ::InitializeActionsList(GetDlgItem(hwnd, IDC_HOTKEYS_LIST));
            break;
        case WM_COMMAND: {
            if (!settingsWindow) { break; }
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
            }
            break;
        }
        case WM_HSCROLL:
            if (settingsWindow && LOWORD(wParam) != TB_ENDTRACK) {
                if (settingsWindow->OnTrackbarChange) {
                    settingsWindow->OnTrackbarChange(hwnd, GetDlgCtrlID((HWND)lParam),
                        SendMessage((HWND)lParam, TBM_GETPOS, 0, 0));
                }
            }
            break;
        case WM_NOTIFY: {
            LPNMHDR pnmh = (LPNMHDR)lParam;
            if (settingsWindow && pnmh->idFrom == IDC_HOTKEYS_LIST) {
                switch (pnmh->code) {
                    case NM_DBLCLK: {
                        LPNMITEMACTIVATE pnmia = (LPNMITEMACTIVATE)lParam;
                        if (pnmia->iItem != -1 && settingsWindow->OnActionActivated) {
                            settingsWindow->OnActionActivated(hwnd, pnmia->iItem);
                        }
                        break;
                    }
                    case NM_CUSTOMDRAW: {
                        // Tint custom action rows so they stand apart from the built-in ones
                        auto* draw = reinterpret_cast<LPNMLVCUSTOMDRAW>(lParam);
                        LRESULT result = CDRF_DODEFAULT;

                        if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) {
                            result = CDRF_NOTIFYITEMDRAW;
                        } else if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT &&
                                   settingsWindow->IsCustomActionRow(static_cast<int>(draw->nmcd.dwItemSpec))) {
                            draw->clrTextBk = CustomActionRowColor;
                        }

                        SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, result);
                        return TRUE;
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

    const int titleHeight = Scale(20, dpi_);
    const int gbp = GroupBoxPadding();
    SetWindowPos(hwndContentDialog_, NULL,
               gbp, titleHeight + gbp,
               groupBoxRect.right - (gbp * 2),
               groupBoxRect.bottom - titleHeight - (gbp * 2),
               SWP_NOZORDER | SWP_NOACTIVATE);
}

static void InitializeActionsList(HWND hwndList) {
    if (!hwndList) {
        return;
    }

    SendMessage(hwndList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
                LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_LABELTIP);

    // Columns share the client width: no horizontal scrolling, long commands get ellipsized
    // and hovering unfolds the full text
    RECT clientRect;
    GetClientRect(hwndList, &clientRect);
    const int available = clientRect.right - GetSystemMetrics(SM_CXVSCROLL);

    const struct { const wchar_t* title; int percent; } columns[] = {
        {L"Action", 34},
        {L"Hotkey", 30},
        {L"Command", 0},  // takes what is left
    };

    LVCOLUMNW lvc = {};
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    int used = 0;

    for (int i = 0; i < static_cast<int>(std::size(columns)); i++) {
        lvc.iSubItem = i;
        lvc.pszText = const_cast<wchar_t*>(columns[i].title);
        lvc.cx = columns[i].percent ? available * columns[i].percent / 100 : available - used;
        used += lvc.cx;
        SendMessage(hwndList, LVM_INSERTCOLUMNW, i, (LPARAM)&lvc);
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

void SettingsWindow::RebuildFonts() {
    if (hButtonFont_)   { DeleteObject(hButtonFont_);   hButtonFont_   = nullptr; }
    if (hVersionFont_)  { DeleteObject(hVersionFont_);  hVersionFont_  = nullptr; }
    if (hGroupBoxFont_) { DeleteObject(hGroupBoxFont_); hGroupBoxFont_ = nullptr; }

    hButtonFont_ = CreateFontW(
        Scale(-11, dpi_), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Ms Shell Dlg"
    );

    hVersionFont_ = CreateFontW(
        Scale(-8, dpi_), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Ms Shell Dlg"
    );

    hGroupBoxFont_ = CreateFontW(
        Scale(-12, dpi_), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"MS Shell Dlg"
    );

    if (hwndOkButton_)     { SendMessage(hwndOkButton_,     WM_SETFONT, (WPARAM)hButtonFont_,   TRUE); }
    if (hwndCancelButton_) { SendMessage(hwndCancelButton_, WM_SETFONT, (WPARAM)hButtonFont_,   TRUE); }
    if (hwndVersionLabel_) { SendMessage(hwndVersionLabel_, WM_SETFONT, (WPARAM)hVersionFont_,  TRUE); }
    if (hwndGroupBox_)     { SendMessage(hwndGroupBox_,     WM_SETFONT, (WPARAM)hGroupBoxFont_, TRUE); }
}

LRESULT SettingsWindow::OnDpiChanged(WPARAM wParam, LPARAM lParam) {
    dpi_ = HIWORD(wParam);
    const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
    SetWindowPos(hwnd_, nullptr,
        suggested->left, suggested->top,
        suggested->right  - suggested->left,
        suggested->bottom - suggested->top,
        SWP_NOZORDER | SWP_NOACTIVATE);
    RebuildFonts();
    return 0;
}

