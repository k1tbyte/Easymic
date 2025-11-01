#include <iostream>

#include "definitions.h"
#include "AppConfig.hpp"
#include "AudioManager.hpp"
#include "MainWindow/MainWindow.hpp"
#include "Resources/Resource.h"
#include "SettingsWindow/SettingsWindow.hpp"
#include "ViewModel/MainWindowViewModel.hpp"
#include "ViewModel/SettingsWindowViewModel.hpp"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    MSG callbackMsg;

    auto *const mutex =  CreateMutexW(nullptr, FALSE, MutexName);

    // App is running - shutdown duplicate
    if (GetLastError() == ERROR_ALREADY_EXISTS || GetLastError() == ERROR_ACCESS_DENIED) {
        return 0;
    }

    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    ULONG_PTR token_ = 0;
    GdiplusStartupInput input;
    GdiplusStartup(&token_, &input, nullptr);

    AppConfig config;
    AppConfig::Load(&config,AppConfig::GetConfigPath());

    auto manager = AudioManager();
    manager.Init();

    auto mainWindow = std::make_shared<MainWindow>(hInstance, config);
    mainWindow->AttachViewModel<MainWindowViewModel>(config, manager);

    if (!mainWindow->Initialize({})) {
        return 1;
    }
    mainWindow->Show();

    auto settingsWindow = std::make_shared<SettingsWindow>(hInstance);
    settingsWindow->AttachViewModel<SettingsWindowViewModel>(manager);

    // Configure settings window
    SettingsWindow::Config windowConfig;
    windowConfig.parentHwnd = mainWindow->GetHandle();

    // Initialize
    if (!settingsWindow->Initialize(windowConfig)) {
        return 1;
    }

    settingsWindow->Show();
    while(GetMessage(&callbackMsg, nullptr, 0, 0))
    {
        TranslateMessage(&callbackMsg);
        DispatchMessage(&callbackMsg);
    }

    ReleaseMutex(mutex);
    return 0;
}
