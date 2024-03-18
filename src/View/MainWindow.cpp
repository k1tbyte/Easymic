#include "MainWindow.hpp"

static MainWindow* mainWindow;
#define WND_BACKGROUND RGB(24,27,40)
#define WND_CORNER_RADIUS 15
#define WND_PADDING 2


MainWindow::MainWindow(LPCWSTR name, HINSTANCE hInstance, Config* config,AudioManager* audioManager)
    : AbstractWindow(hInstance, &events)
{
    this->name = name;
    this->config = config;
    this->audioManager = audioManager;
    this->settings = new SettingsWindow(hInstance, &hWnd, config, audioManager,appIcon,
                                        [this](){this->Reinitialize(); });
    mainWindow = this;
}

bool MainWindow::InitWindow()
{
    static WNDCLASSEXW wndClass;
    appIcon = LoadIcon(hInst,(LPCTSTR)MAKEINTRESOURCE(IDI_APP));
    mutedIcon = LoadIcon(hInst,(LPCTSTR)MAKEINTRESOURCE(IDI_MIC_MUTED));
    unmutedIcon = LoadIcon(hInst,(LPCTSTR)MAKEINTRESOURCE(IDI_MIC));
    activeIcon = LoadIcon(hInst,(LPCTSTR)MAKEINTRESOURCE(IDI_MIC_ACTIVE));

    Utils::LoadResource(hInst, MAKEINTRESOURCE(IDR_UNMUTE), "WAVE",
                 &unmuteSound.buffer, &unmuteSound.fileSize);
    Utils::LoadResource(hInst, MAKEINTRESOURCE(IDR_MUTE), "WAVE",
                 &muteSound.buffer, &muteSound.fileSize);

    wndClass.cbSize =			sizeof(WNDCLASSEXW);
    wndClass.style = CS_VREDRAW | CS_HREDRAW | CS_DROPSHADOW;
    wndClass.cbClsExtra =		0;
    wndClass.cbWndExtra =		0;
    wndClass.hInstance =		hInst;
    wndClass.hIcon =			appIcon;
    wndClass.hbrBackground =	CreateSolidBrush(WND_BACKGROUND);
    wndClass.lpszClassName =	name;
    wndClass.hIconSm		 =	appIcon;
    wndClass.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
            { return mainWindow->WindowHandler(hwnd,msg,wp,lp); };

    if(!RegisterClassExW(&wndClass)) {
        return false;
    }

    windowSize = config->indicatorSize*WND_PADDING;
    this->hWnd = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
            name,name,
            WS_POPUP,
            config->windowPosX,config->windowPosY,
            windowSize,windowSize,
            nullptr,nullptr,hInst,nullptr);

    this->_initComponents();

#ifndef DEBUG
    HotkeyManager::Initialize([this]() { OnHotkeyPressed(); });
    HotkeyManager::RegisterHotkey(config->muteHotkey);
#endif

#ifdef DEBUG
   // settings->Show();
#endif

    return true;
}

bool MainWindow::InitTrayIcon() {
    trayIcon.cbSize = sizeof(NOTIFYICONDATAW);
    trayIcon.hWnd = (HWND)hWnd;
    trayIcon.uID = IDI_MIC;
    trayIcon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    wcscpy(trayIcon.szTip, (std::wstring(name) + L" | " + audioManager->GetDefaultMicName()).c_str());
    trayIcon.hIcon = micMuted ? mutedIcon : unmutedIcon;
    trayIcon.uCallbackMessage = WM_SHELLICON;

    // Display tray icon
    return Shell_NotifyIconW(NIM_ADD, &trayIcon);
}

//#region <== Window events ==>
void MainWindow::OnCreate(WPARAM wParam, LPARAM lParam)
{
    // Window transparency
    SetLayeredWindowAttributes(hWnd, RGB(0, 0, 0), 220, LWA_ALPHA  | LWA_COLORKEY);
    HRGN windowRegion = CreateRoundRectRgn(0, 0, windowSize, windowSize,WND_CORNER_RADIUS, WND_CORNER_RADIUS);
    SetWindowRgn(hWnd, windowRegion, TRUE);
    DeleteObject(windowRegion);
}

void MainWindow::OnDestroy(WPARAM wParam, LPARAM lParam)
{
    Shell_NotifyIconW(NIM_DELETE, &trayIcon);	// Remove Tray Item
    PostQuitMessage(0);
}

void MainWindow::OnPaint(WPARAM wParam, LPARAM lParam) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hWnd, &ps);
    BYTE iconSize = config->indicatorSize;
    HICON icon = micActive ? activeIcon : (micMuted ? mutedIcon : unmutedIcon);

    DrawIconEx(hdc, windowSize / 2 - iconSize / 2, windowSize / 2 - iconSize / 2,
               icon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
    EndPaint(hWnd, &ps);
}

void MainWindow::OnClose(WPARAM wParam, LPARAM lParam) {
    DestroyWindow(hWnd);
}

void MainWindow::OnShellIcon(WPARAM wParam, LPARAM lParam){
    if(LOWORD(lParam) != WM_LBUTTONDBLCLK && LOWORD(lParam) != WM_RBUTTONUP){
        return;
    }

    POINT lpClickPoint;
    GetCursorPos(&lpClickPoint);

    HMENU hMenu = LoadMenu(hInst, MAKEINTRESOURCE(IDR_TRAY_MENU));
    if (!hMenu) {
        return;
    }

    // Select the first submenu
    HMENU hSubMenu = GetSubMenu(hMenu, 0);
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
}

void MainWindow::OnCommand(WPARAM wParam, LPARAM lParam) {
    if(wParam == ID_APP_EXIT) {
        DestroyWindow(hWnd);
        return;
    }

    if(wParam == ID_APP_SETTINGS && !settings->IsShown()) {

        HotkeyManager::UnregisterHotkey();
        audioManager->DisposeMicSessionsWatcher();

        if(peakMeterActive) {
            KillTimer(hWnd,MIC_PEAK_TIMER);
            micActive = peakMeterActive = false;
            InvalidateRect(hWnd,nullptr,TRUE);
        }

        if(config->indicator != IndicatorState::Hidden) {
            this->Show();
        }

        settings->Show();
    }
}

void MainWindow::OnHotkeyPressed() {
    SwitchMicState(true);
}

void MainWindow::OnDragEnd(WPARAM wParam, LPARAM lParam) {
    RECT rect;
    GetWindowRect(hWnd, &rect);
    Config* configTemp = settings->GetTempSettings();
    configTemp->windowPosX = rect.left;
    configTemp->windowPosY = rect.top;
}

//#endregion

void MainWindow::SetMuteMode(BOOL muted)
{
    if (!config->muteZeroMode) {
        audioManager->SetMicState(muted);
        return;
    }

    audioManager->SetMicVolume(muted ? 0 : config->micVolume);
}

void MainWindow::SwitchMicState(bool playSound) {
    Resource* sound;
    if(settings->CurrentlyMuted()) {
        trayIcon.hIcon = unmutedIcon;
        sound = &unmuteSound;
        micMuted = false;
        SetMuteMode(FALSE);

        // If there are active sessions and the mode is shown only when muting
        // or the mode is shown when muting or talking
        if(hasActiveSessions && (config->indicator == IndicatorState::Muted ||
           (config->indicator == IndicatorState::MutedOrTalk && !micActive))) {
            this->Hide();
        }
    }
    else {
        trayIcon.hIcon = mutedIcon;
        sound = &muteSound;
        micMuted = true;
        SetMuteMode(TRUE);

        if(hasActiveSessions && (config->indicator != IndicatorState::Hidden)){
            this->Show();
        }
    }

    if(playSound) {
        PlaySound((LPCSTR) sound->buffer,hInst,SND_ASYNC | SND_MEMORY);
    }

    InvalidateRect(this->hWnd, nullptr, TRUE);
    Shell_NotifyIconW(NIM_MODIFY, &trayIcon);
}

void MainWindow::Reinitialize() {
    this->Hide();
    if(config->indicator == IndicatorState::Hidden) {
        return;
    }

    audioManager->StartMicSessionsWatcher([this](int count) {
        // !!! Important !!!
        // If we directly call this->Hide this->Show, strange things will
        // happen to the window (as with all the crap in WinApi)
        // Perhaps this has something to do with the fact that the calls are coming from different threads, idk
        if(count > 0) {

            if(!peakMeterActive && (config->indicator == IndicatorState::AlwaysAndTalk ||
                                    config->indicator == IndicatorState::MutedOrTalk)) {
                SetTimer(hWnd,MIC_PEAK_TIMER,150,nullptr);
                peakMeterActive = true;
            }

            // If mic not muted -> return
            if((config->indicator == IndicatorState::Muted || config->indicator == IndicatorState::MutedOrTalk) &&
                !this->micMuted) {
                SendMessage(hWnd,WM_SWITCH_STATE, 0, SW_HIDE);
                return;
            }

            SendMessage(hWnd, WM_SWITCH_STATE, 0, SW_SHOW);

            hasActiveSessions = true;
            return;
        }

        if(peakMeterActive) {
            KillTimer(hWnd,MIC_PEAK_TIMER);
            peakMeterActive = false;
        }

        SendMessage(hWnd,WM_SWITCH_STATE, 0, SW_HIDE);
        hasActiveSessions = false;
    });
}

void MainWindow::_initComponents() {
    if(config->bellVolume != 50) {
        audioManager->SetAppVolume(config->bellVolume);
    }

    micMuted = settings->CurrentlyMuted();

    if(config->micVolume == BYTE(-1)) {
        config->micVolume = audioManager->GetMicVolume();
    }
    else if((!config->muteZeroMode || !micMuted) &&
        config->micVolume != audioManager->GetMicVolume()) {
        audioManager->SetMicVolume(config->micVolume);
    }
    Reinitialize();
}

