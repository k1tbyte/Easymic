#include "definitions.h"
#include "AppConfig.hpp"
#include "CrashHandler.hpp"
#include "MainWindow.hpp"
#include "MainWindowViewModel.hpp"
#include "Core/Feedback.hpp"
#include "Core/Host.hpp"
#include "Core/Hotkeys/HotkeyService.hpp"
#include "Features/Keyboard/Keyboard.hpp"
#include "Features/Launcher/Launcher.hpp"
#include "Features/Microphone/Microphone.hpp"
#include "Logger.hpp"
#include "Version.hpp"
#include "UpdateManager.hpp"
#include "UACService.hpp"

/// Every feature there is. Adding one is this line plus its own folder - nothing else in the app
/// knows the list, and the order here is the order the "Add action" menu shows them in.
constexpr void (*Modules[])(Host&) = {
    &Mic::Register, &Keyboard::Register, &Launcher::Register,
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    auto *mutex = CreateMutexW(nullptr, FALSE, MUTEX_NAME);

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

    if (config.Core.SkipUac && !UAC::IsElevated() && UAC::IsSkipUACEnabled()) {
        // The elevated instance claims this very name, and the name lives as long as a handle is
        // open - hand it over before starting it, or it shuts itself down as a duplicate
        CloseHandle(mutex);
        mutex = nullptr;

        if (UAC::RunWithSkipUAC()) {
            return 0;
        }

        // Handoff failed - take the name back, unless the elevated instance got it anyway
        mutex = CreateMutexW(nullptr, FALSE, MUTEX_NAME);
        if (GetLastError() == ERROR_ALREADY_EXISTS || GetLastError() == ERROR_ACCESS_DENIED) {
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
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&gdiplusToken, &input, nullptr);

    g_AppVersion = Version::GetCurrentVersion();
    LOG_INFO("Application version: %s", g_AppVersion.GetFullFormat().c_str());

    if (config.Core.Updates) {
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

    // Static for the same reason the config is: an action registered here outlives WinMain, and
    // every module holds on to what the host carries
    static Feedback feedback(config);
    static Host host{config, feedback, hInstance};

    // Before the window: restoring the config resolves every binding through the registry, and an
    // empty one would drop them all without saying so
    // Before the modules, so a page a module contributes lands after the frame's own middle
    // pages and still ahead of About, which pins itself last
    SettingsWindowViewModel::RegisterPages();

    for (const auto& registerModule : Modules) {
        registerModule(host);
    }

    static MainWindow mainWindow(hInstance, config);
    mainWindow.AttachViewModel<MainWindowViewModel>(config, feedback);

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
    HotkeyService::Dispose();

    CloseHandle(mutex);
    return 0;
}
