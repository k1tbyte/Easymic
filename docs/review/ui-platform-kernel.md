# UI, Platform, Core kernel, build

Files: `src/UI/**`, `src/Platform/**`, `src/Core/*`, `main.cpp`, `definitions.h`, `CMakeLists.txt`, `build.ps1`, `scripts/check-architecture.ps1`. Reviewer: area subagent; UP-1, UP-2, UP-3, UP-4, UP-17 re-checked.

## Verdict

Kernel and overlay are lean and layered well, and idle is clean except one polling leak. The real defects are data safety
(config write, a Cancel that saves), the update path, and no keyboard handling in the settings window. Most simplification is in
the settings UI (an `ActionEdit` mirror, a 194-line `DialogProc`) and `BaseWindow`, not the kernel.

## Findings

### UP-1 [Med][Idle] `Overlay/OverlaySurface.hpp:112`, `Microphone/MicLayer.cpp:17,59,70` (checked)
`wanted = layer.TickMs && layer.Tick && (slot.Width > 0 || slot.WantsTick)`: the layer timer runs whenever the pill is on screen. With the default `Pill = Muted` and the mic muted, `MicLayer` ticks every 150 ms (about 6.7 wakeups/s) for as long as the pill shows: while a capture session is active by default (`HideWhenInactive = true`), all day with it off. `_tick` does nothing unless `_peakPhase` is set. Contradicts `architecture.md` "idle means no timer".
Fix: drop `slot.Width > 0 ||`; `_measure` returns `.WantsTick = _listening() || _peakPhase`. Two lines.

### UP-2 [Med][Bug] `Platform/File.hpp:34-36`, `Core/AppConfig.cpp:30-39` (checked)
The config write is `CREATE_ALWAYS` in place. A crash mid-write leaves a partial file; `Load` then returns defaults and the next `Save` overwrites it: all bindings lost. A hand-edit typo does the same (`glz::read` fails, `return {}`).
Fix: write `.tmp`, `MoveFileExW(REPLACE_EXISTING|WRITE_THROUGH)`; on parse failure rename the old file to `.bad`. About 8 lines.

### UP-3 [Med][Bug] `Settings/SettingsPages.cpp:62-82`, `UpdateManager.cpp:269` (checked at `_elevateOrRevert`)
Cancel does not cancel. `Changed` fires on click (`SettingsRows.cpp:361`); `_requestElevation` and `_elevateOrRevert` call `cfg.Save()`, which writes the whole live draft, including other unsaved edits. Declining (line 80) saves too. `SkipVersion` saves the same live object. Cancel restores memory only.
Fix: no `Save` on decline; save only right before elevating; defer `SkipVersion` while settings are open.

### UP-4 [Med][Bug] `main.cpp:136-140` (checked: no `IsDialogMessage` in `src`)
`SettingsWindow` is modeless (`SettingsWindow.cpp:65`) and nothing calls `IsDialogMessage`: Tab, Enter and Esc do nothing there. Not run.
Fix: `MainWindow::PreTranslate(msg)` calling `IsDialogMessage` on the settings handle: about 5 lines.

### UP-5 [Med][Bug] `UpdateManager.cpp:205-243`
The script uses a fixed `Start-Sleep 2` with no `-ErrorAction Stop`. If exit takes longer, `Copy-Item` fails silently, `Remove-Item` deletes the download and `Start-Process` relaunches the old exe. No hash check on the download although the exe can run elevated via `SkipUAC` (assumption: no check elsewhere).
Fix: rename the running exe to `.old`, `MoveFileExW` the download in, `CreateProcess`, quit: about -25 lines, no PowerShell with ExecutionPolicy Bypass. Verify a SHA-256 asset.

### UP-6 [Med][Footprint] `CMakeLists.txt:59-73`, `Platform/Version.cpp:15-16`
All system libs are load-time imports: wininet (drags iertutil and more) maps at boot with updates off and is used once. `g_AppVersion` is a global that reads the exe version at every start, duplicate-instance exit included. Sizes unmeasured (assumption).
Fix: `/DELAYLOAD:wininet.dll;version.dll`, function-static version, optionally unload wininet after the check.

### UP-7 [Med][Footprint] `Overlay/OverlaySurface.hpp:43,56-66,168`, `main.cpp:73-74`
`_fontSource` starts empty, so the boot `Relayout` builds a `Gdiplus::FontFamily` though nothing is shown, forcing GDI+ font init at idle (assumption on cost: measure private bytes). The default `GdiplusStartupInput` keeps its background thread all day.
Fix: validate the font at the first non-empty slot; `SuppressBackgroundThread` with `NotificationHook`/`Unhook` around the loop.

### UP-8 [Med][Bug] `main.cpp:143-150`, `UpdateManager.cpp:49-51`
`updateManager->Stop()` joins a WinINet thread before `stopThreads()`. Timeouts cover connect and receive, probably not DNS (assumption). Quitting during a stalled check leaves the hooks installed with the window gone.
Fix: `Input::Stop` first; detach the check or skip the join.

### UP-9 [Med][Structure] `BaseWindow.hpp:41-69,153`
`BaseWindow` is an overlay base (position, size, shadow window, fluent `BaseWindow*` setters); `SettingsWindow` uses only `FromHandle`/`RegisterWindow`. In shadow mode `Close()` calls `DestroyWindow` on a foreign-process HWND (56, 153): it fails, `WM_DESTROY` never runs (`MainWindow.cpp:190`) and `main.cpp:131-134` patches it with an atexit hide.
Fix: geometry and shadow into `OverlaySurface` with `SetBounds(x, y, w, h)`; the base shrinks to about 40 lines (-80).

### UP-10 [Med][Perf] `Overlay/OverlaySurface.hpp:229-234,255-259`, `UIAccess/UIAccess.cpp:110-173,190-196`
Each settings close re-runs `GetOrCreateWindow`: process snapshot, `OpenProcess(QUERY_INFORMATION|VM_READ)` per process, `EnumWindows` per UIAccess pid and `Sleep(100)` on the UI thread. It injects into `uiAccessPids[0]`, arbitrary and maybe short-lived, and never checks the cached HWND with `IsWindow` (assumption: the overlay vanishes if the host exits). The shellcode copies 2048 bytes from a function start (`UIAccess.cpp:30,92`, `Injection.hpp:18`) into RWX memory: fragile under Debug thunks and looks like malware to AV.
Fix: keep the HWND across sessions with an `IsWindow` check, `QUERY_LIMITED_INFORMATION`, one `EnumWindows`.

### UP-11 [Low][Perf] `MainWindow.cpp:242-254`, `TrayIcon.hpp:36-52`
`RefreshTray` does two `NIM_MODIFY` calls (icon, then tooltip) per call, unconditionally, on every mic state change and desktop switch (`Microphone.cpp:280`, `Tracker.cpp:61`). One `Update(icon, tip)`, skipped when unchanged.

### UP-12 [Med][DRY] `Settings/ActionDialog.hpp:15-46`, `ActionList.cpp:109-175`, `ActionDialog.cpp:174-367`
`ActionEdit` mirrors `Binding` and `HotkeyTrigger` (10 fields) and `_edit` copies field by field both ways (about 65 lines); `DialogProc` is 194 lines; `CanonicalApp` runs in `ActionDialog.cpp:333`, `ActionList.cpp:152` and `AppConfig.cpp:20,50`.
Fix: the dialog edits a `Binding` copy plus a small layout struct; split `DialogProc` into Fill and Read; normalise on `Load` only so `Save() const` and Core drops its `Platform/Foreground` include. About -50 lines (more with the mirror).

### UP-13 [Low][DRY] `Desktops/Page.cpp:265-283`, `Keyboard/Settings/ExcludedApps.cpp:95-108`, `RulesPage.cpp:97-110`
Rule of Three reached: panel class registration, `Controls::Create(page, page, ... WS_EX_CONTROLPARENT)` and a child lambda, three times. `Controls::Panel(page, cell, id)` in `Platform/Controls.hpp`: about -25 lines. `SettingsRows::Control` (`SettingsRows.cpp:184`) is a one-line wrapper.

### UP-14 [Low][Structure] `main.cpp:30-154`, `UI/UpdateManager.*`
`UpdateManager` is a service in `src/UI`, wired by hand instead of `Modules[]`; its `std::function` callback has one use; `SkipVersion`/`DownloadAndInstall` are public but internal; it includes `Settings/DialogControls.hpp` only for `CenterOnScreen` (`UpdateManager.cpp:285`). `WinMain` is 125 lines.
Fix: a `Features/Updates` slice with `Register(Host&)`, an extracted `HandOffToElevated()`, `find_if` instead of copying assets: about -30 lines.

### UP-15 [Low][Simplicity] YAGNI list
`MainWindow::WindowConfig` is always `{}` (`main.cpp:106`, `MainWindow.hpp:19`); `GetCommandLineArguments` (`UACService.cpp:169,298`) - the app takes no args and the join loses quoting; `Event`/`IEvent` (mutex, vector) serves one subscriber (`SettingsWindow.hpp:30,52`); `FallbackVersion` (`Version.cpp:12`) is a 6th copy of 1.3.0.0; 13 `LOG_INFO/LOG_WARNING` sites compile to nothing (`definitions.h:33-45`); `GITHUB_*` aliases `DEV_NAME`/`REPO_NAME` (`definitions.h:23`); the `upper_bound` insert is copied 3 times (`Overlay.hpp:66`, `Tray.hpp:43`, `SettingsHost.hpp:108`).

### UP-16 [Low][Perf] `Overlay/TextLayer.hpp:26,32`, `Platform/Gdi.hpp:35-72`
Every relayout re-measures via `GetDC`, `Graphics`, `FontFamily`, `GraphicsPath::AddString`; `Render` builds another `Font`, `StringFormat`, brush; mic ticks call `Overlay::Changed` while a notification is up; `Feedback::Post` (`Feedback.hpp:116`) allocates a wstring that `PostNotification` copies (`MainWindow.cpp:83`); `RefreshPos` passes `SWP_FRAMECHANGED` each time (`BaseWindow.hpp:149`).
Fix: cache the font and metrics in `ShowText`; pass the wstring by value.

### UP-17 [Low][Perf] `Core/Feedback.hpp:68-83`, `Hotkeys/HotkeyService.cpp:123` (checked; same as IN-6)
`Wrap` closure is about 144 bytes (function 64, two strings), above MSVC's 56-byte `std::function` buffer (assumption); `Dispatcher::Post(binding.onPress)` copies it per press on the input thread: 1-3 mallocs. Post a `shared_ptr<const ActionFn>` after measuring.

### UP-18 [Low][Footprint] `Platform/Logger.cpp:3,24`, `Settings/DialogControls.hpp:4,117`, `Resources`
`<filesystem>` for `file_size`/`remove`/`filename` despite the size rule (unmeasured): `GetFileAttributesExW`/`DeleteFileW` do it. Embedded WAVs total 202 KB; mute and unmute are stereo 44.1 kHz (156 KB): mono 22 kHz saves about 118 KB with no code (quality is a judgment call).

### UP-19 [Low][Structure] `scripts/check-architecture.ps1:62-81`, `CMakeLists.txt:33,41`, `build.ps1:49`
The script guards only `Features` to {other slice, UI} and `Platform` to {Core, Features, UI}. It does not guard Core to UI/Features, UI to Features (clean today per grep) or the `Hotkeys/X` include rule, and it misses `SettingsPages.cpp:12,243` writing `TextLayer::Text` (overlay internals), `Core/Overlay.hpp:8` putting `gdiplus.h` in every feature TU, `definitions.h` pulling `ComPtr` into every TU. The check runs three times per build.

### UP-20 [Low][Readability]
- `Platform` 19 flat files and `Core` 14: groupings in the README.
- Generic names: `MainWindow` (overlay host, tray, settings launcher), `DialogControls` (sound picker plus single-consumer helpers).
- Comments are 14% of lines in this area (805), 30-60% in headers (`Dispatcher.hpp` 20 of 32). Restating: `Version.hpp:30`, `Version.cpp:15`, `main.cpp:34`, `UpdateManager.hpp:11`, `AudioFileValidator.hpp:24-32`.

## Keep

- Function-pointer POD registries and `Modules[]` (`main.cpp:26`).
- Per-layer timer, the `Foreground` snapshot with its generation check, `Dispatcher`'s drop-on-Stop and try/catch, `CrashHandler`'s re-entrancy guard.
