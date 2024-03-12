#include "MainWindow.h"

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
    this->settings = new SettingsWindow(hInstance, &hWnd);
    mainWindow = this;
}

bool MainWindow::InitWindow()
{
    static WNDCLASSEXW wndClass;
    appIcon = LoadIcon(hInst,(LPCTSTR)MAKEINTRESOURCE(IDI_APP));
    mutedIcon = LoadIcon(hInst,(LPCTSTR)MAKEINTRESOURCE(IDI_MIC_MUTED));
    unmutedIcon = LoadIcon(hInst,(LPCTSTR)MAKEINTRESOURCE(IDI_MIC));

    wndClass.cbSize =			sizeof(WNDCLASSEXW);
    wndClass.style = CS_VREDRAW | CS_HREDRAW | CS_DROPSHADOW;
    wndClass.cbClsExtra =		0;
    wndClass.cbWndExtra =		0;
    wndClass.hInstance =		hInst;
    wndClass.hIcon =			appIcon;
    wndClass.hbrBackground =	(HBRUSH)GetStockObject(WHITE_BRUSH);
    wndClass.lpszClassName =	name;
    wndClass.hIconSm		 =	appIcon;
    wndClass.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
            { return mainWindow->WindowHandler(hwnd,msg,wp,lp); };

    if(!RegisterClassExW(&wndClass)) {
        return false;
    }

    windowSize = config->indicatorSize*WND_PADDING;
    CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
            name,name,
            WS_POPUP,
            config->windowPosX,config->windowPosY,
            windowSize,windowSize,
            nullptr,nullptr,hInst,nullptr);

    return true;
}

bool MainWindow::InitTrayIcon() {
    trayIcon.cbSize = sizeof(NOTIFYICONDATAW);
    trayIcon.hWnd = (HWND)hWnd;
    trayIcon.uID = IDI_MIC;
    trayIcon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    wcscpy(trayIcon.szTip, (std::wstring(name) + L" | " + audioManager->GetDefaultMicName()).c_str());
    trayIcon.hIcon = audioManager->IsMicMuted() ? mutedIcon : unmutedIcon;
    trayIcon.uCallbackMessage = WM_SHELLICON;

    // Display tray icon
    return Shell_NotifyIconW(NIM_ADD, &trayIcon);
}

void MainWindow::OnCreate(WPARAM wParam, LPARAM lParam)
{
    // Window transparency
    SetLayeredWindowAttributes(hWnd, RGB(0, 0, 0), 220, LWA_ALPHA  | LWA_COLORKEY);
    HRGN windowRegion = _getWindowRegion();
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
    HRGN windowRegion = _getWindowRegion();
    BYTE iconSize = config->indicatorSize;

    FillRgn(hdc, windowRegion, CreateSolidBrush(WND_BACKGROUND));

    DrawIconEx(hdc, windowSize / 2 - iconSize / 2, windowSize / 2 - iconSize / 2,
               appIcon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
    EndPaint(hWnd, &ps);
    DeleteObject(windowRegion);
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

    if(wParam == ID_APP_SETTINGS) {
        settings->Show();
    }
}


HRGN MainWindow::_getWindowRegion() const {
    return CreateRoundRectRgn(0, 0, windowSize, windowSize,
                       WND_CORNER_RADIUS, WND_CORNER_RADIUS);
}




