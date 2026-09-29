# EasyLauncher

Native WinAPI C++23 tray app: triggers (global hotkeys on low-level hooks) run actions - microphone,
keyboard layout and autocorrect, launcher, virtual desktops. Priorities in order: input latency and
idle footprint (it runs all day), binary size, simplicity.

## Build

```powershell
.\build.ps1                      # MinSizeRel -> cmake-build-minsizerel/EasyLauncher.exe
.\build.ps1 -Config Debug -Run   # Debug binds no hotkeys (APP_NO_GLOBAL_HOOKS): check hooks on MinSizeRel
.\build.ps1 -Test                # unit_tests via ctest: router and hold, keyboard config, autocorrect, user rules, typed word, packs, launcher command line, desktops, keyboard UI
.\build.ps1 -Tools               # tools/Keyboard/langpack
```

The script imports MSVC itself (`cl.exe` is never on PATH) and fails the build on a layering
violation (`scripts/check-architecture.ps1`).

## Layout

- `src/Core/` - the kernel, knows no feature: `ActionRegistry`, `Dispatcher` (action worker, `ToUi`),
  `Feedback`, `SettingsHost`, `Overlay`, `Tray`, `Lifecycle`, `AppConfig`, `Input/`, `Hotkeys/`.
- `src/Platform/` - thin Win32 wrappers used by more than one owner (a single owner keeps its own):
  `Diagnostics/`, `Gfx/`, `Windowing/`, `System/`, and the flat `Event`, `File`, `Str`.
  Includes none of `Core/`, `Features/`, `UI/`.
- `src/Features/<Slice>/` - `Microphone`, `Keyboard`, `Launcher`, `Desktops`. Includes only `Core/`
  and `Platform/`; what two slices share goes to `Core/`.
- `src/UI/` - `MainWindow` (frame: overlay window, tray, settings session), settings (row tables ->
  controls; the frame's pages in `SettingsPages`), the overlay surface and `UIAccess/`, `UpdateManager`.
- A new feature: one folder plus one line in `Modules[]` in `src/main.cpp`; `Register(Host&)` adds
  its `ActionDesc`s, settings page, overlay layer, tray provider.
- `tests/<Area>/`, `tools/<Area>/` - tests and dev tools, grouped like the slices (`Input`, `Keyboard`,
  `Launcher`, `Desktops`); `tests/Check.hpp` is the one `Check`.

## Rules

- Hook procs and a stage's `OnKey` stay O(1); actions never run inside a hook.
- Config keys are permanent: renaming an `ActionDesc::Id` or a field is a migration.
- Inside `Core/` include `"Hotkeys/X.hpp"`; elsewhere spell `"Core/Hotkeys/X.hpp"`.
- Punto Switcher data is proprietary: never commit it or `.rules` built from it.
- Live probes (`tests/*/*Probe.ps1`) type into the foreground: idle desktop, no other EasyLauncher running.

## Details

- [Architecture](.claude/rules/architecture.md) - threads, settings, overlay, config
- [Input pipeline](.claude/rules/input.md) - stages, levels, hold and edits, hotkeys
- [Keyboard](.claude/rules/keyboard.md) - conversion, autocorrect, packs, langpack
