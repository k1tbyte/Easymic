# Easymic Architecture

This document provides an overview of the Easymic application architecture for developers.

## Overview

Easymic is a Windows desktop application for global microphone control, built using:
- **Language:** C++23
- **UI Framework:** Native WinAPI
- **Build System:** CMake
- **Serialization:** Glaze (for JSON/BEVE config)

## Project Structure

```
src/
├── main.cpp              # Application entry point
├── definitions.h         # Global definitions and macros
├── AppConfig.hpp         # Configuration management
├── Audio/                # Audio device management
│   ├── AudioManager.hpp              # High-level audio management
│   ├── AudioDeviceController.hpp     # Device-specific controls
│   ├── AudioFileValidator.hpp        # WAV file validation
│   └── EventHandlers/                # COM callback implementations
├── Lib/                  # Utility libraries
│   ├── Event.hpp         # Event/delegate pattern implementation
│   ├── Logger.*          # File-based logging with UI events
│   ├── HotkeyManager.*   # Global keyboard/mouse hook management
│   ├── CrashHandler.*    # Exception and crash handling
│   ├── UpdateManager.*   # GitHub release update checking
│   ├── UACService.*      # UAC elevation and bypass
│   ├── Version.*         # Version management from resources
│   └── UIAccess/         # UIAccess window injection
├── View/                 # Window classes
│   ├── Core/
│   │   ├── BaseWindow.hpp    # Base class for all windows
│   │   └── WindowRegistry.hpp # HWND -> Window* mapping
│   ├── Components/
│   │   ├── TrayIcon.hpp      # System tray icon management
│   │   ├── GdiRenderer.hpp   # GDI+ rendering utilities
│   │   └── LayeredWindow.hpp # Layered window rendering
│   ├── MainWindow/       # Main indicator window
│   └── SettingsWindow/   # Settings dialog
├── ViewModel/            # View logic (MVVM-like)
│   ├── ViewModel.hpp             # Base ViewModel interface
│   ├── MainWindowViewModel.hpp   # Main window logic
│   └── SettingsWindowViewModel.* # Settings window logic
└── Resources/            # Windows resources
    ├── Resource.h        # Resource IDs
    └── *.rc              # Resource scripts
```

## Key Design Patterns

### MVVM-like Architecture

Views (Windows) are separated from their logic (ViewModels):

```cpp
// View creates and attaches ViewModel
auto mainWindow = std::make_shared<MainWindow>(hInstance, config);
mainWindow->AttachViewModel<MainWindowViewModel>(config, manager);
```

ViewModels inherit from `BaseViewModel<T>` and implement `Init()`.

### Event System

Custom event system for decoupled communication:

```cpp
// Declaration
Event<bool, float>& OnCaptureStateChanged;

// Subscribe
int id = event += [](bool muted, float volume) { /* handler */ };

// Unsubscribe
event -= id;

// Invoke
event(true, 0.5f);
```

### Window Registry

Singleton pattern for HWND-to-Window mapping:

```cpp
WindowRegistry::Instance().Register(hwnd, this);
BaseWindow* window = WindowRegistry::Instance().Get(hwnd);
```

### COM Event Handlers

Audio events use custom COM callback classes:
- `VolumeEventsHandler` - Volume/mute changes
- `AudioDeviceEventsHandler` - Device changes
- `SessionCreateEventsHandler` - New audio sessions
- `SessionStateEventsHandler` - Session state changes

## Audio Architecture

The audio system uses Windows Core Audio APIs:

```
AudioManager (orchestrator)
├── AudioDeviceController (capture - microphone)
│   ├── IAudioEndpointVolume (volume/mute control)
│   ├── IAudioMeterInformation (peak meter)
│   └── IAudioSessionManager2 (session management)
└── AudioDeviceController (render - speakers)
```

## Configuration

Configuration uses Glaze for binary serialization:

```cpp
// Save
glz::write_file_beve(config, GetConfigPath(), std::string{});

// Load
glz::read_file_beve(config, GetConfigPath(), std::string{});
```

Config file: `conf.b` (BEVE format) in application directory.

## Hotkey System

Global keyboard/mouse hooks capture input:

```cpp
// Register hotkey
HotkeyManager::RegisterHotkey(keysMask, {
    .onPress = []() { /* pressed */ },
    .onRelease = []() { /* released */ }
});

// Key mask format: modifiers (byte 0) + keys (bytes 1-7)
uint64_t mask = Keys::make(Keys::MOD_LCTRL, 'M');
```

## Special Features

### UIAccess Window Injection

For "always on top" over fullscreen apps, the indicator can inject a window into a UIAccess process:

```cpp
// Creates window in elevated UIAccess process
HWND shadowHwnd = UIAccessManager::GetOrCreateWindow(key, exStyle, style);
```

### UAC Bypass

Uses Windows Task Scheduler for silent elevation:

```cpp
UAC::EnableSkipUAC();   // Create scheduled task
UAC::RunWithSkipUAC();  // Run via scheduled task
```

## Thread Safety

- `AudioManager` uses `std::atomic` for reinitialization flags
- `WindowRegistry` uses `std::mutex` for thread-safe access
- `Logger` uses `std::mutex` for file operations
- Audio callbacks may fire on different threads

## Resource Management

- COM objects use `Microsoft::WRL::ComPtr` for automatic release
- Windows handles use RAII wrappers where appropriate
- Fonts and brushes are tracked and deleted in `OnDestroy`
