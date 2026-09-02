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

- `src/Core/` - the kernel. `ActionRegistry` (id -> `ActionDesc`), `Bindings` (config ->
  registered hotkeys), `HotkeyService` (LL hooks and key masks), `Dispatcher` (worker thread and
  the hop back to the UI thread), `KeyNames` (mask <-> text), `Feedback` (sound and overlay
  text), `AppConfig`. Hook procs must stay O(1): actions run on the worker thread, never inside
  the proc.
- `src/Platform/` - thin Win32 wrappers with no domain knowledge: `Str`, `Event`, `Logger`,
  `Registry`, `Win32Hook`, `UACService`, `UIAccess/`, `CrashHandler`, `UpdateManager`, `Version`.
- `src/Features/` - vertical slices: `Microphone/` (with `Wasapi/`), `Keyboard/`, `Launcher/`.
  **No file under `Features/X/` may include `Features/Y/`** - `build.ps1` fails the build on one.
  Anything two features both need is a `Core/` concern.
- `src/UI/` - Win32 windows and their view models, `UI/Settings/` for the settings window.
  Settings pages are DIALOGEX resources in `src/Resources/Resource.rc`, loaded by
  `CreateDialogParamW`.
- `vendor/glaze` - submodule, JSON config serialization (`config.json`).

## Adding a feature

One folder under `src/Features/` and one line in the `Modules[]` table in `src/main.cpp`. The
module describes its actions as `ActionDesc` entries and registers them in `Register(Host&)`.

## Refactor in progress

`docs/ARCHITECTURE.md` holds the target architecture and the migration checklist. Steps 0-5 are
done; step 6 is not - the settings pages are still `IDD_SETTINGS_*` templates with one
`Initialize*Section` per page. Read it before touching settings.

`docs/known-bugs.md` holds the defects that are understood and deliberately not fixed yet.
