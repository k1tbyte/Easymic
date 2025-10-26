#include "MainWindow.hpp"
#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

static MainWindow* mainWindow;
static ULONG_PTR gdiplusToken;
#define WND_BACKGROUND RGB(24,27,40)
#define WND_CORNER_RADIUS 15


MainWindow::MainWindow(LPCWSTR name, HINSTANCE hInstance, Config& config, AudioManager& audioManager)
    : AbstractWindow(hInstance, &events), config(config), audioManager(audioManager)
{
    this->name = name;
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

    wndClass.cbSize        = sizeof(WNDCLASSEXW);
    wndClass.style         = CS_VREDRAW | CS_HREDRAW;
    wndClass.cbClsExtra    = 0;
    wndClass.cbWndExtra    = 0;
    wndClass.hInstance     = hInst;
    wndClass.hIcon         = appIcon;
    wndClass.hbrBackground = CreateSolidBrush(WND_BACKGROUND);
    wndClass.lpszClassName = name;
    wndClass.hIconSm       = appIcon;
    wndClass.lpfnWndProc   = [](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
            { return mainWindow->WindowHandler(hwnd,msg,wp,lp); };

    if(!RegisterClassExW(&wndClass)) {
        return false;
    }

    windowSize = config.indicatorSize * WND_PADDING;
    this->hWnd = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
            name,name,
            WS_POPUP,
            config.windowPosX,config.windowPosY,
            windowSize,windowSize,
            nullptr,nullptr,hInst,nullptr);

    if (config.excludeFromCapture) {
        SetWindowDisplayAffinity(hWnd, WDA_EXCLUDEFROMCAPTURE);
    }

    audioManager.OnCaptureStateChanged += [this](bool muted, float volumeLevel){
        if(muted != micMuted) {
            micMuted = muted;
            SendMessage(this->hWnd, WM_UPDATE_MIC,0, 1);
        }
    };

    audioManager.OnCaptureSessionPropertyChanged += [this](ComPtr<IAudioSessionControl>, EAudioSessionProperty property){
        if (property != State && property != Disconnected && property != Connected) {
            return;
        }

        this->OnSessionUpdate();
    };

    this->_initComponents();
    /*HotkeyManager::Initialize([this]() { OnHotkeyPressed(); });
    HotkeyManager::RegisterHotkey(config.muteHotkey);*/


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

    wcscpy(trayIcon.szTip, (std::wstring(name) + L" | " + audioManager.CaptureDevice()->GetDeviceName()).c_str());
    trayIcon.hIcon = micMuted || !audioManager.CaptureDevice()->IsInitialized() ? mutedIcon : unmutedIcon;
    trayIcon.uCallbackMessage = WM_SHELLICON;

    // Display tray icon
    return Shell_NotifyIconW(NIM_ADD, &trayIcon);
}

//#region <== Window events ==>
void MainWindow::OnCreate(WPARAM wParam, LPARAM lParam) {
    // Initialize GDI+
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);
}

void MainWindow::OnDestroy(WPARAM wParam, LPARAM lParam) {
    // Cleanup GDI+
    GdiplusShutdown(gdiplusToken);
    Shell_NotifyIconW(NIM_DELETE, &trayIcon);	// Remove Tray Item
    PostQuitMessage(0);
}

void MainWindow::OnPaint(WPARAM wParam, LPARAM lParam) {
    // Create a 32-bit bitmap with an alpha channel
    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = windowSize;
    bmi.bmiHeader.biHeight = -windowSize; // Negative value for top-down bitmap
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pvBits = nullptr;
    HBITMAP hBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pvBits, nullptr, 0);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);

    Graphics graphics(hdcMem);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    graphics.Clear(Color(0, 0, 0, 0));

    GraphicsPath path;
    REAL radius = WND_CORNER_RADIUS;
    REAL diameter = radius * 1.2f;
    RectF rect(0, 0, windowSize, windowSize);

    // rounded rectangle
    // left top
    path.AddArc(rect.X, rect.Y, diameter, diameter, 180, 90);
    // right top
    path.AddArc(rect.X + rect.Width - diameter, rect.Y, diameter, diameter, 270, 90);
    // right bottom
    path.AddArc(rect.X + rect.Width - diameter, rect.Y + rect.Height - diameter, diameter, diameter, 0, 90);
    // left bottom
    path.AddArc(rect.X, rect.Y + rect.Height - diameter, diameter, diameter, 90, 90);
    path.CloseFigure();

    SolidBrush brush(Color(220, GetRValue(WND_BACKGROUND), GetGValue(WND_BACKGROUND), GetBValue(WND_BACKGROUND)));
    graphics.FillPath(&brush, &path);

    BYTE iconSize = config.indicatorSize;
    HICON icon = micActive ? activeIcon : (micMuted ? mutedIcon : unmutedIcon);
    Bitmap iconBitmap(icon);
    graphics.DrawImage(&iconBitmap, windowSize / 2 - iconSize / 2, windowSize / 2 - iconSize / 2, iconSize, iconSize);

    POINT ptSrc = {0, 0};
    RECT rcWnd;
    GetWindowRect(hWnd, &rcWnd);
    POINT ptDst = {rcWnd.left, rcWnd.top};
    SIZE sizeWnd = {windowSize, windowSize};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};

    UpdateLayeredWindow(hWnd, hdcScreen, &ptDst, &sizeWnd, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);

    // Cleanup
    SelectObject(hdcMem, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
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

    // Unregister hotkey and stop audio monitoring when settings window is opened
    if(wParam == ID_APP_SETTINGS && !settings->IsShown()) {

        HotkeyManager::UnregisterHotkey();
        audioManager.StopWatchingForCaptureSessions();

        if(peakMeterActive) {
            KillTimer(hWnd,MIC_PEAK_TIMER);
            micActive = peakMeterActive = false;
            InvalidateRect(hWnd,nullptr,TRUE);
        }

        this->Show();
        settings->Show();
    }
}

void MainWindow::OnHotkeyPressed() {
    SwitchMicState();
}

void MainWindow::OnDragEnd(WPARAM wParam, LPARAM lParam) {
    RECT rect;
    GetWindowRect(hWnd, &rect);
    Config* configTemp = settings->GetTempSettings();
    configTemp->windowPosX = rect.left;
    configTemp->windowPosY = rect.top;
}

//#endregion


inline void MainWindow::SwitchMicState() {
    audioManager.CaptureDevice()->SetMute(!micMuted);
}



void MainWindow::Reinitialize() {
    if(config.indicator != IndicatorState::Hidden) {
        audioManager.WatchForCaptureSessions();
        hasActiveSessions = audioManager.CaptureDevice()->GetActiveSessionsCount() > 0;
        if (hasActiveSessions) {
            this->OnSessionUpdate();
        }
    }

    SendMessage(hWnd,WM_UPDATE_STATE, 0, 0);
}

void MainWindow::_initComponents() {
    /*
    if(config->bellVolume != 50) {
        audioManager->SetAppVolume(config->bellVolume);
    }
    */
    const auto& mic = audioManager.CaptureDevice();

    this->micMuted = mic->IsMuted();
    const auto volume = mic->GetVolumePercent();
    if(config.micVolume == BYTE(-1)) {
        config.micVolume = volume;
    }
    else if(config.micVolume != volume) {
        mic->SetVolumePercent(config.micVolume);
    }
    Reinitialize();
}
