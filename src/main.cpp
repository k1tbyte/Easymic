#include <iostream>

#include "definitions.h"
#include "config.hpp"
#include "AudioManager.hpp"
#include "Resources/Resource.h"
#include "SettingsWindow/SettingsWindow.hpp"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    MSG callbackMsg;

    const auto mutex =  CreateMutex(nullptr, FALSE, MutexName);

    // App is running - shutdown duplicate
    if (GetLastError() == ERROR_ALREADY_EXISTS || GetLastError() == ERROR_ACCESS_DENIED) {
        return 0;
    }

    CoInitialize(nullptr); // Initialize COM

    Config config;
    Config::Load(&config,Config::GetConfigPath());

    auto manager = AudioManager();
    manager.Init();

   // InitWindow(hInstance);
    auto settingsWindow = std::make_unique<SettingsWindow>(hInstance);

    // Configure settings window
    SettingsWindow::Config windowConfig;
    windowConfig.dialogResourceId = IDD_SETTINGS;

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