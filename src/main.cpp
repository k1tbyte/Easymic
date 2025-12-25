#include "definitions.h"
#include "AppConfig.hpp"
#include "AudioManager.hpp"
#include "CrashHandler.hpp"
#include "MainWindow/MainWindow.hpp"
#include "ViewModel/MainWindowViewModel.hpp"
#include "Lib/Logger.hpp"
#include "Lib/Version.hpp"
#include "Lib/UpdateManager.hpp"
#include "Lib/UACService.hpp"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    MSG callbackMsg;

    auto *const mutex = CreateMutexW(nullptr, FALSE, MUTEX_NAME);

    // App is running - shutdown duplicate
    if (GetLastError() == ERROR_ALREADY_EXISTS || GetLastError() == ERROR_ACCESS_DENIED) {
        return 0;
    }

    CrashHandler::LogCallback logCallback = [](const std::string& msg) {
        LOG_ERROR(msg.c_str());
    };

    if (!CrashHandler::Initialize({ .logCallback = logCallback})) {
        LOG_ERROR("Failed to initialize CrashHandler");
        return 1;
    }

    AppConfig config = AppConfig::Load();

    if (config.IsSkipUACEnabled && !UAC::IsElevated() && UAC::IsSkipUACEnabled()) {
        if (UAC::RunWithSkipUAC()) {
            // Successfully started elevated instance, close this one
            ReleaseMutex(mutex);
            return 0;
        }
        MessageBoxA(nullptr,
            "Failed to start application with elevated privileges using UAC bypass. The application will continue to start normally, but some features may not work correctly.",
            "UAC Bypass Failed",
            MB_OK | MB_ICONWARNING);
        LOG_WARNING("Skip UAC failed, continuing with normal startup");
    }

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ULONG_PTR token_ = 0;
    GdiplusStartupInput input;
    GdiplusStartup(&token_, &input, nullptr);

    
    // Initialize global version
    g_AppVersion = Version::GetCurrentVersion();
    LOG_INFO("Application version: %s", g_AppVersion.GetFormatted().c_str());
    
    // Check for updates if enabled
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

    auto manager = AudioManager();
    manager.Init();
    LOG_INFO("AudioManager initialized successfully");

    static auto mainWindow = std::make_shared<MainWindow>(hInstance, config);
    mainWindow->AttachViewModel<MainWindowViewModel>(config, manager);

    if (!mainWindow->Initialize({})) {
        LOG_ERROR("Failed to initialize MainWindow");
        return 1;
    }
    
    LOG_INFO("MainWindow initialized successfully");

    atexit([]() {
        LOG_INFO("Application shutting down");
        mainWindow->Hide();
    });

    while (GetMessage(&callbackMsg, nullptr, 0, 0)) {
        TranslateMessage(&callbackMsg);
        DispatchMessage(&callbackMsg);
    }

    ReleaseMutex(mutex);
    return 0;
}
