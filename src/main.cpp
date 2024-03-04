#include <tchar.h>
#include <mmdeviceapi.h>
#include <stdexcept>
#include <endpointvolume.h>
#include <commctrl.h>
#include "main.hpp"
#include "Resources/Resource.h"

//#region <== Prototypes ==>

void OnShellIconEvent(LPARAM);
void OnCommandEvent(WPARAM);
BOOL IsMicMuted(IAudioEndpointVolume*);
void SwitchMicState();
IAudioEndpointVolume* GetDefaultMicVolume();
void UpdateHotkeyType();
void InitHotkey();
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void SetMouseHotkey(int);
void ChangeWaveVolume(Resource*, BYTE);
void LoadResource(LPCSTR , LPCSTR , BYTE** , DWORD* );

//#endregion

//#region <== Globals ==>

HWND hWnd;
HWND settingsHwnd;

HINSTANCE hInst;
NOTIFYICONDATA structNID;
HHOOK hMouseHook;
bool preventCancel = false;

HICON micIcon;
HICON micMutedIcon;

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

//#endregion

//#region <== Registry ==>

bool IsInAutoStartup()
{
    return RegGetValue(HKEY_CURRENT_USER,AutoStartupHKEY,
                       AppName,RRF_RT_REG_SZ,
                       nullptr,nullptr,nullptr) == ERROR_SUCCESS;
}

bool AddToAutoStartup()
{
    HKEY hkey;
    auto result = RegOpenKeyEx(HKEY_CURRENT_USER,AutoStartupHKEY,0,
                               KEY_WRITE,&hkey);

    if(result != ERROR_SUCCESS)
        return false;

    char szFileName[MAX_PATH];

    GetModuleFileName(nullptr, szFileName, MAX_PATH);
    return RegSetValueEx(hkey, AppName, 0, REG_SZ, reinterpret_cast<const BYTE *>(szFileName),
                         strlen(szFileName)) == ERROR_SUCCESS;
}

bool RemoveFromAutoStartup()
{
    return RegDeleteKeyValue(HKEY_CURRENT_USER,AutoStartupHKEY,AppName);
}

//#endregion

//#region <== UI ==>

void CenterWindow(HWND hwnd)
{
    HWND hwndOwner = GetDesktopWindow();
    RECT rcDlg, rcOwner, rc;

    GetWindowRect(hwndOwner, &rcOwner);
    GetWindowRect(hwnd, &rcDlg);
    CopyRect(&rc, &rcOwner);

    OffsetRect(&rcDlg, -rcDlg.left, -rcDlg.top);
    OffsetRect(&rc, -rc.left, -rc.top);
    OffsetRect(&rc, -rcDlg.right, -rcDlg.bottom);

    SetWindowPos(hwnd,HWND_TOP,
                 rcOwner.left + (rc.right / 2),
                 rcOwner.top + (rc.bottom / 2),0, 0,SWP_NOSIZE
    );
}

void UpdateVolume(BYTE volume)
{

}

bool InitWindow()
{
    WNDCLASSEX wndClass;

    // loading state icons
    micIcon =  LoadIcon(hInst,(LPCTSTR)MAKEINTRESOURCE(IDI_MIC));
    micMutedIcon = LoadIcon(hInst,(LPCTSTR)MAKEINTRESOURCE(IDI_MIC_MUTED));
    auto appIcon = LoadIcon(hInst,(LPCTSTR)MAKEINTRESOURCE(IDI_APP));

    wndClass.cbSize =			sizeof(WNDCLASSEX);
    wndClass.style = CS_HREDRAW | CS_VREDRAW;
    wndClass.lpfnWndProc =	WndProc;
    wndClass.cbClsExtra =		0;
    wndClass.cbWndExtra =		0;
    wndClass.hInstance =		hInst;
    wndClass.hIcon =			appIcon;
    wndClass.hCursor =		LoadCursor(nullptr, IDC_ARROW);
    wndClass.hbrBackground =	(HBRUSH)GetStockObject(WHITE_BRUSH);
    wndClass.lpszClassName =	AppName;
    wndClass.hIconSm		 =	appIcon;

    if(!RegisterClassEx(&wndClass)) {
        return false;
    }

    // Creating the hidden window
    hWnd = CreateWindowEx(
            WS_EX_CLIENTEDGE,
            AppName,
            AppName,
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            nullptr,
            nullptr,
            hInst,
            nullptr);

    return hWnd;
}

bool InitTrayIcon()
{
    structNID.cbSize = sizeof(NOTIFYICONDATA);
    structNID.hWnd = (HWND)hWnd;
    structNID.uID = IDI_MIC;
    structNID.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    strcpy(structNID.szTip, AppName);
    structNID.hIcon = IsMicMuted(GetDefaultMicVolume()) ? micMutedIcon : micIcon;
    structNID.uCallbackMessage = WM_USER_SHELLICON;

    // Display tray icon
    return Shell_NotifyIcon(NIM_ADD, &structNID);
}

void InitSettingsElements(HWND& hDlg)
{
    CenterWindow(hDlg);

    if(config.keybdHotkey != 0) {
        UnregisterHotKey(hWnd,UID);
    }

    if(hMouseHook != nullptr) {
        UnhookWindowsHookEx(hMouseHook);
    }

    if(IsInAutoStartup()) {
        SendMessage(GetDlgItem(hDlg,ID_AUTOSTARTUP), BM_SETCHECK, BST_CHECKED, 0);
    }

    SetMouseHotkey(config.mouseHotkeyIndex);
    UpdateHotkeyType();

    SendDlgItemMessageW(hDlg,ID_HOTKEY_KEYBD,HKM_SETHOTKEY,config.keybdHotkey,0 );

    HWND hwndTrack = GetDlgItem(hDlg, VOLUME_TRACKBAR);
    SendMessage(hwndTrack, TBM_SETRANGE,
                (WPARAM) TRUE,                   // redraw flag
                (LPARAM) MAKELONG(0, 100));  // min. & max. positions

    SendMessage(hwndTrack, TBM_SETPAGESIZE,
                0, (LPARAM) 1);                  // new page size

    SendMessage(hwndTrack, TBM_SETPOS,(WPARAM) TRUE,(LPARAM) config.volume);
}

void ApplyConfigChanges()
{
    if(config.Equals(configTemp)) {
        return;
    }

    if(config.volume != configTemp.volume) {
        UpdateVolume(configTemp.volume);
    }

    config = configTemp;
    Config::Save(&config, configPath);
}

//#region <== Handlers ==>

LRESULT CALLBACK WndProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
    if(Message == WM_TASKBAR_CREATE && !Shell_NotifyIcon(NIM_ADD, &structNID)) {
        DestroyWindow(hWnd);
        return -1;
    }

    switch(Message)
    {
        case WM_DESTROY:
            Shell_NotifyIcon(NIM_DELETE, &structNID);	// Remove Tray Item
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
            if (wParam == UID)
            {
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

                RegisterHotKey(hWnd, UID,
                               (HIBYTE (config.keybdHotkey) & 2) | ((HIBYTE (config.keybdHotkey) & 4) >> 2) | ((HIBYTE (config.keybdHotkey) & 1) << 2),
                               LOBYTE (config.keybdHotkey));
                SwitchMicState();
            }

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
                configTemp.volume = SendMessage(GetDlgItem(hDlg, VOLUME_TRACKBAR), TBM_GETPOS, 0, 0);
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

                EndDialog(hDlg, LOWORD(wParam));
                return TRUE;
            }

            switch(LOWORD(wParam))
            {
                case IDOK:
                    ApplyConfigChanges();
                    EndDialog(hDlg, LOWORD(wParam));
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

        case WM_DESTROY:
            InitHotkey();
            settingsHwnd = nullptr;
            break;

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
            DialogBox(hInst, MAKEINTRESOURCE(IDD_SETTINGS), hWnd, SettingsHandler);
            break;
    }
}

void OnShellIconEvent(LPARAM lParam)
{
    POINT lpClickPoint;
    switch(LOWORD(lParam))
    {
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
        RegisterHotKey(hWnd, UID,
                       (HIBYTE (config.keybdHotkey) & 2) | ((HIBYTE (config.keybdHotkey) & 4) >> 2) | ((HIBYTE (config.keybdHotkey) & 1) << 2),
                       LOBYTE (config.keybdHotkey));

    }
}

//#endregion

//#region <== Windows audio ==>

IAudioEndpointVolume* GetDefaultMicVolume()
{
    CoInitialize(nullptr); // Initalize COM
    IMMDeviceEnumerator* deviceEnumerator;
    HRESULT result = CoCreateInstance(
            __uuidof(MMDeviceEnumerator), nullptr,
            CLSCTX_INPROC_SERVER,__uuidof(IMMDeviceEnumerator),
            (LPVOID *)&deviceEnumerator
    );

    if(result != S_OK) {
        throw std::runtime_error("Bad CoCreateInstance result");
    }

    IMMDevice* defaultMicDevice;
    IAudioEndpointVolume* micVolume;

    // Getting the default mic device
    deviceEnumerator->GetDefaultAudioEndpoint(EDataFlow::eCapture, ERole::eCommunications,
                                              &defaultMicDevice);
    deviceEnumerator->Release();

    defaultMicDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL,
                               nullptr, (void**)&micVolume);
    defaultMicDevice->Release();

    return micVolume;
}

BOOL IsMicMuted(IAudioEndpointVolume* audioEndpoint)
{
    BOOL wasMuted;
    audioEndpoint->GetMute(&wasMuted);
    return wasMuted;
}

void SwitchMicState()
{
    IAudioEndpointVolume* audioEndpoint = GetDefaultMicVolume();
    BOOL isMuted = IsMicMuted(audioEndpoint);

    if(isMuted)
    {
        structNID.hIcon = micIcon;

        PlaySound((LPCSTR) unmuteSound.buffer,hInst,
                  SND_ASYNC | SND_MEMORY);
        audioEndpoint->SetMute(FALSE, nullptr);
    }
    else
    {
        structNID.hIcon = micMutedIcon;
        PlaySound((LPCSTR) muteSound.buffer,hInst,
                  SND_ASYNC | SND_MEMORY);
        audioEndpoint->SetMute(TRUE, nullptr);
    }

    Shell_NotifyIcon(NIM_MODIFY, &structNID);
}

//#endregion


void LoadResource(LPCSTR resourceName, LPCSTR resourceType, BYTE** buffer, DWORD* size)
{
    HRSRC hResInfo = FindResource(hInst,resourceName, resourceType);
    HANDLE hRes = LoadResource(hInst, hResInfo);
    *buffer = (BYTE*)LockResource(hRes);
    *size = SizeofResource(hInst, hResInfo);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    MSG callbackMsg;
    hInst = hInstance;

    const auto mutex =  CreateMutex(nullptr, FALSE, MutexName);

    // App is running - shutdown duplicate
    if (GetLastError() == ERROR_ALREADY_EXISTS || GetLastError() == ERROR_ACCESS_DENIED) {
        return 0;
    }

    CoInitialize(nullptr); // Initalize COM

    if(!InitWindow() || !InitTrayIcon()) {
        MessageBox(nullptr, "Window creation failed!", "Error",  MB_OK);
        return -1;
    }


    LoadResource(MAKEINTRESOURCE(IDR_UNMUTE), _T("WAVE"),
                &unmuteSound.buffer, &unmuteSound.fileSize);
    LoadResource(MAKEINTRESOURCE(IDR_MUTE), _T("WAVE"),
                 &muteSound.buffer, &muteSound.fileSize);
    
    configPath = Config::GetConfigPath();
    Config::Load(&config, configPath);
    InitHotkey();

    if(config.volume != 100) {
        UpdateVolume(config.volume);
    }

 //   DialogBox(hInst, MAKEINTRESOURCE(IDD_SETTINGS), hWnd, SettingsHandler);
    while(GetMessage(&callbackMsg, nullptr, 0, 0))
    {
        TranslateMessage(&callbackMsg);
        DispatchMessage(&callbackMsg);
    }

    ReleaseMutex(mutex);
    return 0;
}

/*void ChangeWaveVolume(Resource* sound, BYTE volumePercent)
{
    BYTE* pDataOffset = (sound->buffer + 40);
    float volume = (float)volumePercent / 100;

    __int16 * p = (__int16 *)(pDataOffset + 8);
    for (int i = 80 / sizeof(*p); i < sound->fileSize / sizeof(*p); i++) {
        p[i] = (float)p[i] * volume;
    }
}*/