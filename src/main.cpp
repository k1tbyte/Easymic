#include "definitions.h"
#include "AppConfig.hpp"
#include "Diagnostics/CrashHandler.hpp"
#include "MainWindow.hpp"
#include "Settings/SettingsPages.hpp"
#include "Core/Dispatcher.hpp"
#include "Core/Feedback.hpp"
#include "Core/Host.hpp"
#include "Core/Hotkeys/HotkeyCapture.hpp"
#include "Core/Hotkeys/HotkeyService.hpp"
#include "Core/Input/Input.hpp"
#include "Windowing/Foreground.hpp"
#include "Features/Desktops/Desktops.hpp"
#include "Features/Keyboard/Keyboard.hpp"
#include "Features/Launcher/Launcher.hpp"
#include "Features/Microphone/Microphone.hpp"
#include "UpdateManager.hpp"
#include "System/UACService.hpp"

#include <algorithm>
#include <gdiplus.h>
#include <memory>

/// The order is the "Add action" menu's.
constexpr void (*Modules[])(Host&) = {
    &Mic::Register, &Keyboard::Register, &Launcher::Register, &Desktops::Register,
};

namespace {

    HANDLE _claimInstance() {
        HANDLE mutex = CreateMutexW(nullptr, FALSE, MUTEX_NAME);
        const DWORD error = GetLastError();
        if (error == ERROR_ALREADY_EXISTS || error == ERROR_ACCESS_DENIED) {
            CloseHandle(mutex);
            return nullptr;
        }
        return mutex;
    }

    bool _handOffToElevated(HANDLE& mutex, const AppConfig& config) {
        if (!config.Core.SkipUac || UAC::IsElevated() || !UAC::IsSkipUACEnabled()) {
            return false;
        }

        // The elevated instance claims this very name, which lives as long as a handle is open
        CloseHandle(mutex);
        if (UAC::RunWithSkipUAC()) {
            return true;
        }

        mutex = _claimInstance();
        if (!mutex) {
            return true;
        }

        MessageBoxW(nullptr,
            L"Failed to start application with elevated privileges using UAC bypass. The application will continue to start normally, but some features may not work correctly.",
            L"UAC Bypass Failed",
            MB_OK | MB_ICONWARNING);
        return false;
    }

    /// No GDI+ background thread: the hook it returns wraps the message loop instead.
    Gdiplus::GdiplusStartupOutput _startGdiplus() {
        ULONG_PTR token = 0;
        const Gdiplus::GdiplusStartupInput input(nullptr, TRUE, FALSE);
        Gdiplus::GdiplusStartupOutput output{};
        Gdiplus::GdiplusStartup(&token, &input, &output);
        return output;
    }

    void _registerModules(Host& host, AppConfig& config) {
        // Ahead of the modules: their pages land after the frame's own and before About
        SettingsPages::Register(config);

        HotkeyCapture::Register();
        HotkeyService::Register();
        for (const auto& registerModule : Modules) {
            registerModule(host);
        }
    }

    /// ~thread() on a joinable thread terminates, so every exit path stops both.
    void _stopThreads() {
        Input::Stop();
        Dispatcher::Stop();
    }

    void _runMessageLoop(const MainWindow& window, const Gdiplus::GdiplusStartupOutput& gdiplus) {
        ULONG_PTR hookToken = 0;
        gdiplus.NotificationHook(&hookToken);

        MSG message;
        while (GetMessage(&message, nullptr, 0, 0)) {
            if (!window.PreTranslate(message)) {
                TranslateMessage(&message);
                DispatchMessage(&message);
            }
        }

        gdiplus.NotificationUnhook(hookToken);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    HANDLE mutex = _claimInstance();
    if (!mutex) {
        return 0;
    }

    // "%s": the report carries whatever an exception message had in it
    CrashHandler::Install([](const char* report) { LOG_ERROR("%s", report); });

    // Static: registrations and the feedback object keep references to what they were given
    static AppConfig config = AppConfig::Load();
    if (_handOffToElevated(mutex, config)) {
        return 0;
    }

    UpdateManager::DeleteStaleExecutable();
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const Gdiplus::GdiplusStartupOutput gdiplus = _startGdiplus();

    static Feedback feedback(config);
    static Host host{config, feedback, hInstance};

    // Before the window: restoring the config resolves bindings through the registry, and an empty one drops them all
    _registerModules(host, config);

    // The dispatcher starts in the frame's RestoreConfig, as after every settings session
    Input::Start();

    static MainWindow mainWindow(hInstance, config, feedback);
    if (!mainWindow.Initialize()) {
        LOG_ERROR("Failed to initialize MainWindow");
        _stopThreads();
        CloseHandle(mutex);
        return 1;
    }

    std::unique_ptr<UpdateManager> updates;
    if (config.Core.Updates) {
        updates = std::make_unique<UpdateManager>(config);
        updates->CheckForUpdatesAsync();
    }

    _runMessageLoop(mainWindow, gdiplus);

    // Producers first: the hooks are what post to the worker and the window
    _stopThreads();
    Foreground::Stop();
    mainWindow.Close();

    CloseHandle(mutex);
    if (updates) {
        updates->RelaunchIfInstalled();
    }
    return 0;
}
