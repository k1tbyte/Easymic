#include "main.hpp"
#include <tchar.h>
#include <commctrl.h>
#include "Resources/Resource.h"
#include "AudioManager.hpp"

//#region <== Prototypes ==>

void OnShellIconEvent(LPARAM);
void OnCommandEvent(WPARAM);
void UpdateHotkeyType();
void InitHotkey();
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void SetMouseHotkey(int);

//#endregion

//#region <== Globals ==>

HWND hWnd;
HWND settingsHwnd;

HINSTANCE hInst;
NOTIFYICONDATAW structNID;
HHOOK hMouseHook;
bool preventCancel = false;

HICON micIcon;
HICON micMutedIcon;
HICON appIcon;

Config config;
Config configTemp;
const char* configPath;

const Hotkey mouseHotkeys[] =  {
        { "NONE",0,0 },
        { "MBUTTON",VK_MBUTTON,WM_MBUTTONUP },
        { "RBUTTON", VK_RBUTTON, WM_RBUTTONUP},
        { "LBUTTON", VK_LBUTTON, WM_LBUTTONUP },
        { "XBUTTON1", VK_XBUTTON1, WM_XBUTTONUP },
        { "XBUTTON2", VK_XBUTTON2, WM_XBUTTONUP }
};

Resource muteSound;
Resource unmuteSound;
AudioManager* audioManager = new AudioManager();

//#endregion

BOOL IsMuteMode()
{
    bool isMuteState = audioManager->IsMicMuted();

    if (!config.muteVolumeZero) {
        return isMuteState;
    }

    // Если режим мута громкостью и замучен физически - необходимо размутить
    if(isMuteState) {
        audioManager->SetMicState(false);
    }

    return audioManager->GetMicVolume() == 0;
}

void SetMuteMode(BOOL muted)
{
    if (!config.muteVolumeZero) {
        audioManager->SetMicState(muted);
        return;
    }

    audioManager->SetMicVolume(muted ? 0 : config.micVolume);
}

void SwitchMicState()
{
    BOOL muted;
    if(IsMuteMode())
    {
        structNID.hIcon = micIcon;
        PlaySound((LPCSTR) unmuteSound.buffer,hInst,SND_ASYNC | SND_MEMORY);
        SetMuteMode(muted = FALSE);
    }
    else
    {
        structNID.hIcon = micMutedIcon;
        PlaySound((LPCSTR) muteSound.buffer,hInst,SND_ASYNC | SND_MEMORY);
        SetMuteMode(muted = TRUE);
    }

    if (config.indicatorActive) {
        ShowWindow(hWnd, muted ? SW_SHOW : SW_HIDE);
    }
    Shell_NotifyIconW(NIM_MODIFY, &structNID);
}



//#region <== Registry ==>

bool IsInAutoStartup()
{
    return RegGetValueW(HKEY_CURRENT_USER,AutoStartupHKEY,
                       AppName,RRF_RT_REG_SZ,
                       nullptr,nullptr,nullptr) == ERROR_SUCCESS;
}

bool AddToAutoStartup()
{
    HKEY hkey;
    auto result = RegOpenKeyExW(HKEY_CURRENT_USER,AutoStartupHKEY,0,
                               KEY_WRITE,&hkey);

    if(result != ERROR_SUCCESS)
        return false;

    char szFileName[MAX_PATH];

    GetModuleFileName(nullptr, szFileName, MAX_PATH);
    return RegSetValueExW(hkey, AppName, 0, REG_SZ, reinterpret_cast<const BYTE *>(szFileName),
                         strlen(szFileName)) == ERROR_SUCCESS;
}

bool RemoveFromAutoStartup()
{
    return RegDeleteKeyValueW(HKEY_CURRENT_USER,AutoStartupHKEY,AppName);
}

//#endregion

//#region <== UI ==>


bool InitWindow()
{
    WNDCLASSEXW wndClass;

    // loading state icons
    micIcon =  LoadIcon(hInst,(LPCTSTR)MAKEINTRESOURCE(IDI_MIC));
    micMutedIcon = LoadIcon(hInst,(LPCTSTR)MAKEINTRESOURCE(IDI_MIC_MUTED));
    appIcon = LoadIcon(hInst,(LPCTSTR)MAKEINTRESOURCE(IDI_APP));

    wndClass.cbSize =			sizeof(WNDCLASSEXW);
    wndClass.style = CS_VREDRAW | CS_HREDRAW | CS_DROPSHADOW;
    wndClass.lpfnWndProc =	WndProc;
    wndClass.cbClsExtra =		0;
    wndClass.cbWndExtra =		0;
    wndClass.hInstance =		hInst;
    wndClass.hIcon =			appIcon;
    wndClass.hbrBackground =	(HBRUSH)GetStockObject(WHITE_BRUSH);
    wndClass.lpszClassName =	AppName;
    wndClass.hIconSm		 =	appIcon;

    if(!RegisterClassExW(&wndClass)) {
        return false;
    }

    // Creating the hidden window
    hWnd = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
            AppName,AppName,
            WS_POPUP,
            0,0,
            32,32,
            nullptr,nullptr,hInst,nullptr);

    return hWnd;
}

bool InitTrayIcon()
{
    structNID.cbSize = sizeof(NOTIFYICONDATAW);
    structNID.hWnd = (HWND)hWnd;
    structNID.uID = IDI_MIC;
    structNID.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    auto micName = std::wstring(AppName).append(L" | ").append(audioManager->GetDefaultMicName());
    wcscpy(structNID.szTip, micName.c_str());
    structNID.hIcon = IsMuteMode() ? micMutedIcon : micIcon;
    structNID.uCallbackMessage = WM_USER_SHELLICON;

    // Display tray icon
    return Shell_NotifyIconW(NIM_ADD, &structNID);
}

void InitSettingsElements(HWND& hDlg)
{
    Utils::CenterWindowOnScreen(hDlg);
    SendMessage(hDlg, WM_SETICON, ICON_BIG, (LPARAM)appIcon);

    if(config.keybdHotkey != 0) {
        UnregisterHotKey(hWnd,UID);
    }

    if(hMouseHook != nullptr) {
        UnhookWindowsHookEx(hMouseHook);
    }

    if(IsInAutoStartup()) {
        SendMessage(GetDlgItem(hDlg,ID_AUTOSTARTUP), BM_SETCHECK, BST_CHECKED, 0);
    }

    if(config.keybdHotkeyAvail) {
        SendMessage(GetDlgItem(hDlg,HOTKEY_AVAIL), BM_SETCHECK, BST_CHECKED, 0);
    }

    if(config.muteVolumeZero) {
        SendMessage(GetDlgItem(hDlg,MUTE_MODE), BM_SETCHECK, BST_CHECKED, 0);
    }

    if(config.indicatorActive) {
        SendMessage(GetDlgItem(hDlg,MUTE_INDICATOR), BM_SETCHECK, BST_CHECKED, 0);
        ShowWindow(hWnd, SW_SHOW);
    }


    SetMouseHotkey(config.mouseHotkeyIndex);
    UpdateHotkeyType();

    SendDlgItemMessageW(hDlg,ID_HOTKEY_KEYBD,HKM_SETHOTKEY,config.keybdHotkey,0 );

    HWND hwndGroupBox = GetDlgItem(hDlg, SOUNDS_GROUPBOX);
    SetWindowTextW(hwndGroupBox,
                   std::wstring(L"Sounds - ").append(audioManager->GetDefaultMicName())
                   .c_str());

    Utils::InitTrackbar(GetDlgItem(hDlg, BELL_VOLUME_TRACKBAR),1,
                     (LPARAM) MAKELONG(0, 100),
                     config.volume);

    Utils::InitTrackbar(GetDlgItem(hDlg, MIC_VOLUME_TRACKBAR),1,
                     (LPARAM) MAKELONG(0, 100),
                     config.micVolume);
}

void ApplyConfigChanges()
{
    if(config.Equals(configTemp)) {
        return;
    }

    if(config.volume != configTemp.volume) {
        audioManager->SetAppVolume(configTemp.volume);
    }

    if(config.muteVolumeZero != configTemp.muteVolumeZero) {
        config.muteVolumeZero = configTemp.muteVolumeZero;
        SwitchMicState();
    }
    if(config.micVolume != configTemp.micVolume &&
            (!configTemp.muteVolumeZero || (configTemp.muteVolumeZero && !IsMuteMode()))){
        audioManager->SetMicVolume(configTemp.micVolume);
    }

    if(!configTemp.indicatorActive && configTemp.indicatorActive != config.indicatorActive && IsMuteMode()) {
        ShowWindow(hWnd, SW_HIDE);
    }

    config = configTemp;
    Config::Save(&config, configPath);
}

//#region <== Handlers ==>

LRESULT CALLBACK WndProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
    if(Message == WM_TASKBAR_CREATE && !Shell_NotifyIconW(NIM_ADD, &structNID)) {
        DestroyWindow(hWnd);
        return -1;
    }

    switch(Message)
    {
        case WM_CREATE:
        {
            // Устанавливаем прозрачность окна
            SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 220, LWA_ALPHA  | LWA_COLORKEY);
            RECT rc;
            GetClientRect(hwnd, &rc);
            HRGN hRgnUpper = CreateRoundRectRgn(rc.left, rc.top, rc.right, rc.bottom, 15, 15);
            SetWindowRgn(hwnd, hRgnUpper, TRUE);
            break;
        }

        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc;
            RECT rc;

            hdc = BeginPaint(hwnd, &ps);
            GetClientRect(hwnd, &rc);

            int width = rc.right - rc.left;
            int height = rc.bottom - rc.top;

            HRGN hRgnUpper = CreateRoundRectRgn(rc.left, rc.top, rc.right, rc.bottom, 15, 15);
            FillRgn( hdc, hRgnUpper, CreateSolidBrush(RGB(24,27,40)));
            DeleteObject(hRgnUpper);

            DrawIconEx(hdc,width / 2 - 16 / 2,
                       height / 2 - 16 / 2,
                       micIcon,16,16,0,nullptr,DI_NORMAL);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DESTROY:
            Shell_NotifyIconW(NIM_DELETE, &structNID);	// Remove Tray Item
            PostQuitMessage(0);							// Quit
            break;

        case WM_USER_SHELLICON:			// sys tray icon Messages
            OnShellIconEvent(lParam);
            break;

        case WM_CLOSE:
            DestroyWindow(hWnd);	// Destroy Window
            break;

        case WM_COMMAND:
            OnCommandEvent(wParam);
            break;

        case WM_HOTKEY:
        {
            if (wParam != UID){
                break;
            }

            if(!config.keybdHotkeyAvail) {
                SwitchMicState();
                break;
            }

            INPUT ip;
            ip.type = INPUT_KEYBOARD;
            ip.ki.wScan = 0;
            ip.ki.time = 0;
            ip.ki.dwExtraInfo = 0;
            ip.ki.wVk = config.keybdHotkey;
            UnregisterHotKey(hWnd,UID);
            ip.ki.dwFlags = 0;                   //Prepares key down
            SendInput(1, &ip, sizeof(INPUT));    //Key down
            ip.ki.dwFlags = KEYEVENTF_KEYUP;     //Prepares key up
            SendInput(1, &ip, sizeof(INPUT));    //Key up
            RegisterHotKey(hWnd, UID, HKModifier(config.keybdHotkey),LOBYTE (config.keybdHotkey));
            SwitchMicState();

            break;
        }

        case WM_NCHITTEST:
        {
            LRESULT position = DefWindowProc(hWnd, Message, wParam, lParam);
            return position == HTCLIENT ? HTCAPTION : position;
        }

        case WM_EXITSIZEMOVE: {
            RECT rect;
            GetWindowRect(hWnd, &rect);
            configTemp.windowPosX = rect.left;
            configTemp.windowPosY = rect.top;
            break;
        }

        default:
            return DefWindowProc(hwnd, Message, wParam, lParam);
    }
    return 0;		// Return 0 = Message successfully proccessed
}

INT_PTR CALLBACK SettingsHandler(HWND hDlg, UINT message, WPARAM wParam, [[maybe_unused]] LPARAM lParam)
{
    switch (message)
    {
        case WM_INITDIALOG:
            settingsHwnd = hDlg;
            configTemp = config;
            InitSettingsElements(hDlg);
            return (INT_PTR)TRUE;

        case WM_HSCROLL:
            if (LOWORD(wParam) == TB_ENDTRACK) {
                configTemp.volume = SendMessage(GetDlgItem(hDlg, BELL_VOLUME_TRACKBAR), TBM_GETPOS, 0, 0);
                configTemp.micVolume = SendMessage(GetDlgItem(hDlg, MIC_VOLUME_TRACKBAR), TBM_GETPOS, 0, 0);
            }
            break;

        case WM_COMMAND:
            if (LOWORD(wParam) == IDCANCEL)
            {
                if(preventCancel)
                {
                    preventCancel = false;
                    return FALSE;
                }

                DestroyWindow(hDlg);
            }

            switch(LOWORD(wParam))
            {
                case IDOK:
                    ApplyConfigChanges();
                    DestroyWindow(hDlg);
                    break;

                case RADIO_KEYBD:
                    configTemp.isMouseHotkeyMode = false;
                    UpdateHotkeyType();
                    break;

                case RADIO_MOUSE:
                    configTemp.isMouseHotkeyMode = true;
                    UpdateHotkeyType();
                    break;

                case ID_HOTKEY_KEYBD:
                    configTemp.keybdHotkey = (WORD) SendMessage(
                            GetDlgItem(hDlg, ID_HOTKEY_KEYBD), HKM_GETHOTKEY, 0, 0
                    );
                    break;

                case ID_AUTOSTARTUP: {
                    WPARAM param = BST_UNCHECKED;
                    if (IsInAutoStartup()) {
                        RemoveFromAutoStartup();
                    }
                    else {
                        AddToAutoStartup();
                        param = BST_CHECKED;
                    }

                    SendMessage(GetDlgItem(hDlg, ID_AUTOSTARTUP), BM_SETCHECK, param, 0);
                    break;
                }

                case HOTKEY_AVAIL:
                    configTemp.keybdHotkeyAvail = !configTemp.keybdHotkeyAvail;
                    break;

                case MUTE_MODE:
                    configTemp.muteVolumeZero = !configTemp.muteVolumeZero;
                    break;

                case MUTE_INDICATOR:
                    configTemp.indicatorActive = !configTemp.indicatorActive;
                    ShowWindow(hWnd, configTemp.indicatorActive ? SW_SHOW : SW_HIDE);

                    break;

                case ID_HOTKEY_MOUSE_BTN:

                    while(settingsHwnd)
                    {
                        if(GetAsyncKeyState(VK_ESCAPE) < 0) {
                            preventCancel = true;
                            SetMouseHotkey(0);
                            goto exit;
                        }

                        for (int i = 0; i < sizeof(mouseHotkeys) / sizeof(Hotkey); ++i) {
                            if(GetAsyncKeyState(mouseHotkeys[i].VK) < 0) {
                                SetMouseHotkey(i);
                                goto exit;
                            }
                        }

                        Sleep(1);
                    }
                exit: break;
            }
            break;

        case WM_DESTROY: {
            InitHotkey();
            SetWindowLongPtr(hWnd, GWL_EXSTYLE, GetWindowLongPtr(hWnd, GWL_EXSTYLE) | WS_EX_TRANSPARENT);
            settingsHwnd = nullptr;

            if(!IsMuteMode()) {
                ShowWindow(hWnd,SW_HIDE);
            }

            break;
        }

        default:
            return FALSE;

    }

    return FALSE;
}

void OnCommandEvent(WPARAM wParam)
{
    switch(LOWORD(wParam))
    {
        case ID_APP_EXIT:
            DestroyWindow(hWnd);
            break;
        case ID_APP_SETTINGS:
            if(settingsHwnd != nullptr)
            {
                SetForegroundWindow(settingsHwnd);
                return;
            }

            LONG_PTR dwExStyle = GetWindowLongPtr(hWnd, GWL_EXSTYLE);
            dwExStyle &= ~WS_EX_TRANSPARENT; //Deleting WS_EX_TRANSPARENT
            SetWindowLongPtr(hWnd, GWL_EXSTYLE, dwExStyle);
            ShowWindow(CreateDialog(hInst, MAKEINTRESOURCE(IDD_SETTINGS), hWnd, SettingsHandler), SW_SHOW);
            break;
    }
}

void OnShellIconEvent(LPARAM lParam)
{
    POINT lpClickPoint;
    switch(LOWORD(lParam))
    {
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONUP:
            HMENU hMenu, hSubMenu;
            // get mouse cursor position x and y as lParam has the Message itself

            GetCursorPos(&lpClickPoint);

            hMenu = LoadMenu(hInst, MAKEINTRESOURCE(IDR_TRAY_MENU));
            if (!hMenu) {
                return;
            }

            // Select the first submenu
            hSubMenu = GetSubMenu(hMenu, 0);
            if(hSubMenu) {
                // Display menu
                SetForegroundWindow(hWnd);
                TrackPopupMenu(hSubMenu,
                               TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_BOTTOMALIGN,
                               lpClickPoint.x, lpClickPoint.y, 0, hWnd, nullptr);
                SendMessage(hWnd, WM_NULL, 0, 0);
            }

            // Kill off objects we're done with
            DestroyMenu(hMenu);
            break;
    }
}

//#endregion

//#endregion

//#region <== Hotkeys ==>

void UpdateHotkeyType()
{
    // if mouse hotkeys -> disable keyboard and check mouse
    if(configTemp.isMouseHotkeyMode)
    {
        EnableWindow(GetDlgItem(settingsHwnd,ID_HOTKEY_MOUSE_BTN),true);
        EnableWindow(GetDlgItem(settingsHwnd,ID_HOTKEY_KEYBD),false);
        CheckRadioButton(settingsHwnd,RADIO_KEYBD,RADIO_MOUSE, RADIO_MOUSE);
        return;
    }

    EnableWindow(GetDlgItem(settingsHwnd,ID_HOTKEY_KEYBD),true);
    EnableWindow(GetDlgItem(settingsHwnd,ID_HOTKEY_MOUSE_BTN),false);
    CheckRadioButton(settingsHwnd,RADIO_KEYBD,RADIO_MOUSE, RADIO_KEYBD);
}

LRESULT CALLBACK OnMouseHotkeyPressed(int code, WPARAM wParam, LPARAM lParam)
{
    if (code >= 0 && wParam == mouseHotkeys[config.mouseHotkeyIndex].WM)
    {
        auto* event = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
        auto data = (event->mouseData >> 16);
        if(data == 0 || data == mouseHotkeys[config.mouseHotkeyIndex].VK-4)
        {
            SwitchMicState();
        }
    }

    return CallNextHookEx(nullptr, code, wParam, lParam);
}

void SetMouseHotkey(int hotkeyIndex)
{
    SendMessage(
            GetDlgItem(settingsHwnd,ID_HOTKEY_MOUSE_BTN),
            WM_SETTEXT, 0, (LPARAM) mouseHotkeys[hotkeyIndex].Name
    );
    configTemp.mouseHotkeyIndex = hotkeyIndex;
}

void InitHotkey()
{
    if (config.isMouseHotkeyMode && config.mouseHotkeyIndex != 0) {
        hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, OnMouseHotkeyPressed, nullptr, 0);
        return;
    }

    if(!config.isMouseHotkeyMode && config.keybdHotkey != 0) {
        RegisterHotKey(hWnd, UID,HKModifier(config.keybdHotkey),LOBYTE (config.keybdHotkey));

    }
}
//#endregion

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    MSG callbackMsg;
    hInst = hInstance;

    const auto mutex =  CreateMutex(nullptr, FALSE, MutexName);

    // App is running - shutdown duplicate
    if (GetLastError() == ERROR_ALREADY_EXISTS || GetLastError() == ERROR_ACCESS_DENIED) {
        return 0;
    }

    CoInitialize(nullptr); // Initialize COM

    if(!InitWindow() || !audioManager->Init()) {
        MessageBox(nullptr, "Initialization failed!", "Error",  MB_OK);
        return -1;
    }

    configPath = Config::GetConfigPath();
    Config::Load(&config, configPath);

    Utils::LoadResource(hInstance,MAKEINTRESOURCE(IDR_UNMUTE), _T("WAVE"),
                &unmuteSound.buffer, &unmuteSound.fileSize);
    Utils::LoadResource(hInstance,MAKEINTRESOURCE(IDR_MUTE), _T("WAVE"),
                 &muteSound.buffer, &muteSound.fileSize);

    SetWindowPos(hWnd,nullptr, config.windowPosX, config.windowPosY,0,0, SWP_NOSIZE | SWP_NOZORDER);

    if(config.volume != 100) {
        audioManager->SetAppVolume(config.volume);
    }

    if(config.micVolume == BYTE(-1)) {
        config.micVolume = audioManager->GetMicVolume();
    }
    else if(!config.muteVolumeZero && config.micVolume != audioManager->GetMicVolume()) {
        audioManager->SetMicVolume(config.micVolume);
    }

    InitTrayIcon();
    InitHotkey();

    if(IsMuteMode() && config.indicatorActive)
    {
        ShowWindow(hWnd, nCmdShow);
    }

#ifdef DEBUG
  //  DialogBox(hInst, MAKEINTRESOURCE(IDD_SETTINGS), hWnd, SettingsHandler);
#endif

    while(GetMessage(&callbackMsg, nullptr, 0, 0))
    {
        TranslateMessage(&callbackMsg);
        DispatchMessage(&callbackMsg);
    }

    ReleaseMutex(mutex);
    return 0;
}