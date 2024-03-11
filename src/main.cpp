#include "main.hpp"
#include <tchar.h>
#include <commctrl.h>
#include "Resources/Resource.h"
#include "AudioManager.hpp"
#include "View/MainWindow.h"

//#endregion


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    MSG callbackMsg;

    const auto mutex =  CreateMutex(nullptr, FALSE, MutexName);

    // App is running - shutdown duplicate
    if (GetLastError() == ERROR_ALREADY_EXISTS || GetLastError() == ERROR_ACCESS_DENIED) {
        return 0;
    }

    CoInitialize(nullptr); // Initialize COM
   // InitWindow(hInstance);
    auto* mw = new MainWindow(hInstance);
    mw->InitWindow(L"Test");
    SetWindowPos(mw->hWnd,nullptr, 10, 10,0,0, SWP_NOSIZE | SWP_NOZORDER);
    ShowWindow(mw->hWnd,nCmdShow);

    while(GetMessage(&callbackMsg, nullptr, 0, 0))
    {
        TranslateMessage(&callbackMsg);
        DispatchMessage(&callbackMsg);
    }

    ReleaseMutex(mutex);
    return 0;
}