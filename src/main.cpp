#include "definitions.h"
#include "AppConfig.hpp"
#include "AudioManager.hpp"
#include "CrashHandler.hpp"
#include "MainWindow/MainWindow.hpp"
#include "ViewModel/MainWindowViewModel.hpp"
#include "Lib/HotkeyManager.hpp"
#include "Lib/Logger.hpp"
#include "Lib/Version.hpp"
#include "Lib/UpdateManager.hpp"
#include "Lib/UACService.hpp"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    auto *const mutex = CreateMutexW(nullptr, FALSE, MUTEX_NAME);

    // App is running - shutdown duplicate
    if (GetLastError() == ERROR_ALREADY_EXISTS || GetLastError() == ERROR_ACCESS_DENIED) {
        CloseHandle(mutex);
        return 0;
    }

    // "%s": the report is a runtime string and carries whatever an exception message had in it
    const CrashHandler::LogCallback logCallback = [](const std::string& report) {
        LOG_ERROR("%s", report.c_str());
    };

    if (!CrashHandler::Initialize({.logCallback = logCallback})) {
        LOG_ERROR("Failed to initialize CrashHandler");
        CloseHandle(mutex);
        return 1;
    }

    // Static: the update thread and the statics below hold references to it and outlive WinMain
    static AppConfig config = AppConfig::Load();

    if (config.IsSkipUACEnabled && !UAC::IsElevated() && UAC::IsSkipUACEnabled()) {
        if (UAC::RunWithSkipUAC()) {
            // Successfully started elevated instance, close this one
            ReleaseMutex(mutex);
            CloseHandle(mutex);
            return 0;
        }
        MessageBoxW(nullptr,
            L"Failed to start application with elevated privileges using UAC bypass. The application will continue to start normally, but some features may not work correctly.",
            L"UAC Bypass Failed",
            MB_OK | MB_ICONWARNING);
        LOG_WARNING("Skip UAC failed, continuing with normal startup");
    }

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ULONG_PTR gdiplusToken = 0;
    GdiplusStartupInput input;
    GdiplusStartup(&gdiplusToken, &input, nullptr);

    g_AppVersion = Version::GetCurrentVersion();
    LOG_INFO("Application version: %s", g_AppVersion.GetFullFormat().c_str());

    if (config.IsUpdatesEnabled) {
        static UpdateManager updateManager(config);
        updateManager.CheckForUpdatesAsync([](bool hasUpdate, const std::string& error) {
            if (!error.empty()) {
                if (error.find("is skipped") != std::string::npos) {
                    LOG_INFO("Update check: %s", error.c_str());
                } else {
                    LOG_WARNING("Update check failed: %s", error.c_str());
                }
                return;
            }

            if (hasUpdate) {
                LOG_INFO("Update available - showing notification");
                updateManager.ShowUpdateNotification();
            }
        });
    }

    static auto manager = AudioManager();
    if (!manager.Init()) {
        LOG_ERROR("AudioManager failed to initialize - continuing without audio control");
    }

    static MainWindow mainWindow(hInstance, config);
    mainWindow.AttachViewModel<MainWindowViewModel>(config, manager);

    if (!mainWindow.Initialize({})) {
        LOG_ERROR("Failed to initialize MainWindow");
        CloseHandle(mutex);
        return 1;
    }

    LOG_INFO("MainWindow initialized successfully");

    atexit([] {
        LOG_INFO("Application shutting down");
        mainWindow.Hide();
    });

    MSG callbackMsg;
    while (GetMessage(&callbackMsg, nullptr, 0, 0)) {
        TranslateMessage(&callbackMsg);
        DispatchMessage(&callbackMsg);
    }

    // The action worker outlives the message loop, and ~thread() on a joinable thread calls
    // std::terminate - every clean exit used to end in a crash report
    HotkeyManager::Dispose();

    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 0;
}
