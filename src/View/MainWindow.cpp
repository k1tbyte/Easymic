#include "MainWindow.h"

static MainWindow* instance;

LRESULT CALLBACK WindowHandler(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if(message == WM_PAINT) {
        printf("SUCK");
    }
    if(auto it {instance->events.find(message)};
        it != std::end(instance->events))
    {
        (instance->*(it->second))(wParam,lParam);
        return 0;
    }
    return DefWindowProc(hwnd, message, wParam, lParam);
}

MainWindow::MainWindow(HINSTANCE hInstance)
{
    hInst = hInstance;
    instance = this;
}

bool MainWindow::InitWindow(LPCWSTR name)
{
    static WNDCLASSEXW wndClass;
    appIcon = LoadIcon(hInst,(LPCTSTR)MAKEINTRESOURCE(IDI_APP));

    wndClass.cbSize =			sizeof(WNDCLASSEXW);
    wndClass.style = CS_VREDRAW | CS_HREDRAW | CS_DROPSHADOW;
    wndClass.cbClsExtra =		0;
    wndClass.cbWndExtra =		0;
    wndClass.hInstance =		hInst;
    wndClass.hIcon =			appIcon;
    wndClass.hbrBackground =	(HBRUSH)GetStockObject(WHITE_BRUSH);
    wndClass.lpszClassName =	name;
    wndClass.hIconSm		 =	appIcon;
    wndClass.lpfnWndProc =	WindowHandler;

    if(!RegisterClassExW(&wndClass)) {
        return false;
    }

    // Creating the hidden window
    hWnd = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
            name,name,
            WS_POPUP,
            10,10,
            32,32,
            nullptr,nullptr,hInst,nullptr);

    return true;
}

void MainWindow::OnCreate(WPARAM wParam, LPARAM lParam)
{
    // Устанавливаем прозрачность окна
    SetLayeredWindowAttributes(hWnd, RGB(0, 0, 0), 220, LWA_ALPHA  | LWA_COLORKEY);
    RECT rc;
    GetClientRect(hWnd, &rc);
    HRGN hRgnUpper = CreateRoundRectRgn(rc.left, rc.top, rc.right, rc.bottom, 15, 15);
    SetWindowRgn(hWnd, hRgnUpper, TRUE);
}

void MainWindow::OnDestroy(WPARAM wParam, LPARAM lParam)
{
    PostQuitMessage(0);
}

void MainWindow::OnPaint(WPARAM wParam, LPARAM lParam) {
    PAINTSTRUCT ps;
    HDC hdc;
    RECT rc;

    hdc = BeginPaint(hWnd, &ps);
    GetClientRect(hWnd, &rc);

    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    HRGN hRgnUpper = CreateRoundRectRgn(rc.left, rc.top, rc.right, rc.bottom, 15, 15);
    FillRgn( hdc, hRgnUpper, CreateSolidBrush(RGB(24,27,40)));
    DeleteObject(hRgnUpper);

    DrawIconEx(hdc,width / 2 - 16 / 2,
               height / 2 - 16 / 2,
               appIcon,16,16,0,nullptr,DI_NORMAL);
    EndPaint(hWnd, &ps);
}

void MainWindow::OnClose(WPARAM wParam, LPARAM lParam) {
    DestroyWindow(hWnd);
}

