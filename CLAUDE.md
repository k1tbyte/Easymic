# EasyLauncher

Native WinAPI C++23 tray app: global mic control via low-level keyboard/mouse hooks.
Priorities in order: input latency and idle footprint (this thing runs all day in the background),
binary size, simplicity.

## Build

```powershell
.\build.ps1                      # MinSizeRel -> cmake-build-minsizerel/EasyLauncher.exe
.\build.ps1 -Config Debug -Run
.\build.ps1 -Clean
.\build.ps1 -Test                # also builds and runs tests/ (the input router harness)
.\build.ps1 -Tools               # also builds tools/langpack (pack builder, detection console)
.\tests\HotkeyProbe.ps1          # live hotkey check on MinSizeRel; stop a running instance first
```

The script imports the MSVC environment itself (`cl.exe` is never in the global PATH).
Toolchain: global cmake + ninja, MSVC from Visual Studio, resources via `rc.exe` from the Windows SDK.

`UPX not found` during a release build is only a warning - the binary is just left uncompressed
(`winget install UPX.UPX` to enable the POST_BUILD compression step).

## Layout

The app is a runner of triggers and actions. The kernel knows nothing about any feature;
features register into it.

- `src/Core/` - the kernel. `ActionRegistry` (id -> `ActionDesc`), `Dispatcher` (worker thread
  and the hop back to the UI thread), `Feedback` (sound and overlay text), `SettingsHost`
  (settings pages as `SettingsRow` tables), `Overlay` (id -> `OverlayLayer`, what a feature
  draws on the overlay), `Tray` (providers: who paints the tray icon, who adds a menu item),
  `Lifecycle` (settings taking the config and handing it back, as events), `AppConfig`,
  `Input/` (the input thread: LL hooks up only while an enabled stage wants them, and the stage
  pipeline every consumer of input sits in), and `Hotkeys/` for the one trigger source there is
  so far, a stage of `Input` - `HotkeyService` (key masks, multi-press), `KeyChord` (downs and
  ups -> a mask), `KeyNames` (mask <-> text), `HotkeyCapture`, `Bindings` (config -> registered
  hotkeys). A stage's `OnKey` must stay O(1): actions run on the worker thread, never inside the
  hook. A file in `Core/` says `"Hotkeys/KeyNames.hpp"`, everyone else says the full
  `"Core/Hotkeys/KeyNames.hpp"`.
- `src/Platform/` - thin Win32 wrappers with no domain knowledge: `Str`, `Event`, `Logger`,
  `Registry`, `Win32Hook`, `Gdi` (rounded pill, text measure, centred draw), `TrayIconTheme`,
  `LayeredWindow` (per-pixel alpha surface), `Controls` (child control in dialog units),
  `UACService`, `UIAccess/`, `CrashHandler`, `UpdateManager`, `Version`.
- `src/Features/` - vertical slices: `Microphone/` (with `Wasapi/`), `Keyboard/`, `Launcher/`,
  `Desktops/` (virtual desktops, Windows 11 24H2+). A feature may own a private window built
  from `Platform/` helpers.
  **A file under `Features/` may include `Core/` and `Platform/`, and nothing else** -
  `build.ps1` fails the build on a cross-slice include or a `src/UI` header, judging an include
  by where it resolves. Anything two features both need is a `Core/` concern.
- `src/UI/` - Win32 windows and their view models, `UI/Settings/` for the settings window,
  `UI/Overlay/` for the surface features draw into: the layout pass, the pill, the text layer.
  A settings page is a row table registered with `SettingsHost`; `UI/Settings/SettingsRows`
  turns it into controls inside the one empty `IDD_SETTINGS_PAGE`. The only other dialog
  templates are the settings frame, the action dialog and the update dialog.
- `vendor/glaze` - submodule, JSON config serialization (`config.json`).

## Adding a feature

One folder under `src/Features/` and one line in the `Modules[]` table in `src/main.cpp`. The
module describes its actions as `ActionDesc` entries and registers them in `Register(Host&)`, along
with its settings page, overlay layer and tray provider if it has them.

## Architecture

`docs/ARCHITECTURE.md` holds the architecture and the migration checklist, all of it done (steps
0-11). Read it before touching settings, the overlay or the tray.

`docs/INPUT.md` holds the input pipeline contract. Read it before touching hooks or hotkeys.

`docs/LAYOUT.md` holds layout conversion and autocorrect: how it works, the Punto Switcher
findings and the living plan. Read it before touching `Features/Keyboard` or `tools/langpack`;
`src/Features/Keyboard/README.md` is the short map: files, data, building packs, a new language.

`docs/known-bugs.md` holds the defects that are understood and deliberately not fixed yet.
