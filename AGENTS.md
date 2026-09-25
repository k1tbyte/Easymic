# EasyLauncher

Native WinAPI C++23 tray app: global mic control via low-level keyboard/mouse hooks.
Priorities in order: input latency and idle footprint (this thing runs all day in the background),
binary size, simplicity.

## Build

```powershell
.\build.ps1                      # MinSizeRel -> cmake-build-minsizerel/EasyLauncher.exe
.\build.ps1 -Config Debug -Run
.\build.ps1 -Clean
```

The script imports the MSVC environment itself (`cl.exe` is never in the global PATH).
Toolchain: global cmake + ninja, MSVC from Visual Studio, resources via `rc.exe` from the Windows SDK.

`UPX not found` during a release build is only a warning - the binary is just left uncompressed
(`winget install UPX.UPX` to enable the POST_BUILD compression step).

## Layout

The app is a runner of triggers and actions. The kernel knows nothing about any feature;
features register into it.

- `src/Core/` - the kernel. `ActionRegistry` (id -> `ActionDesc`), `Dispatcher` (worker thread
  and the hop back to the UI thread), `Feedback` (sound and overlay text), `SettingsHost`,
  `Overlay` (id -> `OverlayLayer`, what a feature draws on the overlay), `AppConfig`, and
  `Hotkeys/` for the one trigger source there is so far - `HotkeyService` (LL
  hooks and key masks), `KeyNames` (mask <-> text), `HotkeyCapture`, `Bindings` (config ->
  registered hotkeys). Hook procs must stay O(1): actions run on the worker thread, never inside
  the proc. A file in `Core/` says `"Hotkeys/KeyNames.hpp"`, everyone else says the full
  `"Core/Hotkeys/KeyNames.hpp"`.
- `src/Platform/` - thin Win32 wrappers with no domain knowledge: `Str`, `Event`, `Logger`,
  `Registry`, `Win32Hook`, `Gdi` (rounded pill, text measure, centred draw), `TrayIconTheme`,
  `UACService`, `UIAccess/`, `CrashHandler`, `UpdateManager`, `Version`.
- `src/Features/` - vertical slices: `Microphone/` (with `Wasapi/`), `Keyboard/`, `Launcher/`.
  **A file under `Features/` may include `Core/` and `Platform/`, and nothing else** -
  `build.ps1` fails the build on a cross-slice include or a `src/UI` header, judging an include
  by where it resolves. Anything two features both need is a `Core/` concern.
- `src/UI/` - Win32 windows and their view models, `UI/Settings/` for the settings window,
  `UI/Overlay/` for the surface features draw into: the layout pass, the pill, the text layer.
  Settings pages are DIALOGEX resources in `src/Resources/Resource.rc`, loaded by
  `CreateDialogParamW`.
- `vendor/glaze` - submodule, JSON config serialization (`config.json`).

## Adding a feature

One folder under `src/Features/` and one line in the `Modules[]` table in `src/main.cpp`. The
module describes its actions as `ActionDesc` entries and registers them in `Register(Host&)`.

## Refactor in progress

`docs/ARCHITECTURE.md` holds the target architecture and the migration checklist. Steps 0-5, 7
and 8 are done; step 6 is not - the settings pages are still `IDD_SETTINGS_*` templates with one
`Initialize*Section` per page. Read it before touching settings. Steps 9-11 give each feature its
own settings page and let the user pick which feature owns the tray icon.

`docs/INPUT.md` holds the planned input pipeline and layout conversion (steps 13-16, not started).
Read it before touching hooks, hotkeys or `Features/Keyboard`.

`docs/known-bugs.md` holds the defects that are understood and deliberately not fixed yet.
