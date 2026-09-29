# Performance and cleanliness review

Base: `b57e231`. Read-only review, no source changed, nothing built with MSVC (Linux sandbox).
Method: four area reviewers read every file in their area; I reviewed `Core/Input`, `Core/Hotkeys`,
tests and build myself and ran mocks with g++ (`mocks/`, a 13-line `windows.h` shim).

Evidence tags: **mock** reproduced by a program in `mocks/`; **checked** I re-read the code and it holds;
**reported** the area reviewer's reading, not re-checked; **assumption** not measured.

## Verdict

The hot path is in good shape. Hooks are on demand, `OnKey` is O(1), the router and hold survive a
20,000-run random test with no lost, duplicated or reordered event, and no stage touches a pack on the
input thread. The problems sit around it:

- one idle timer that the docs say must not exist, while the muted pill shows (UP-1),
- one stuck-key edge case in the router (IN-1),
- data-safety gaps in config save, Cancel and update (UP-2, UP-3, UP-5),
- settings UI code that is 2-3x the size it needs (UP-12, KE-5, KE-9),
- `WordTracker.cpp` at 497 of the 500-line limit with 19 mutable globals (AC-2, AC-3),
- flat folders: Platform 19 files, Desktops 20, Keyboard root 12, Autocorrect 15.

Per-key CPU in the hotkey stage is about 13-26 ns (mock) against a hook round trip of tens of
microseconds, so micro-optimising it is not where the latency is. Simplifying it is still worth doing (IN-3, IN-4).

## Fix first

| # | ID | Kind | What | Size |
|---|---|---|---|---|
| 1 | UP-1 | idle | Layer timer ticks every 150 ms (6.7 wakeups/s) while the muted pill is visible | 2 lines |
| 2 | IN-1 | bug | A stage enabled or reset while a key is held can swallow that key's up: key stuck (mock) | ~8 lines |
| 3 | UP-2 | data | Config write truncates in place; a crash loses every binding | ~8 lines |
| 4 | UP-3 | data | Cancel in settings still saves (elevation prompt, skip version) | S |
| 5 | FT-3 | security | `{stdout}` launch falls back to our own token when the shell launch fails | -12 lines |
| 6 | KE-3 | idle | System-wide `EVENT_OBJECT_CREATE` WinEvent hook while "show layout" is on | measure |
| 7 | UP-6, UP-7 | footprint | wininet and GDI+ font init at boot with updates off and nothing shown | S |
| 8 | FT-1, FT-2 | latency | Desktop switch animation blocks the serial action worker; concurrent animated switch | S |
| 9 | KE-2, KE-1 | perf, diag | Every settings close (Cancel too) re-resolves packs and rules on the UI thread; pack errors are silent | S |
| 10 | FT-4, FT-5 | bug | `{stdout}` quoting (`R&D`, `50%`) and 4 KB kill / 10 s limit contradict the docs | S |
| 11 | AC-1 | perf | One `Input::Post` plus allocations per typed key | M |
| 12 | UP-4, UP-5 | ux, bug | No Tab/Enter/Esc in settings; update script races the exit | S |

Checklist with acceptance notes: [TASKS.md](TASKS.md).

## Reports

| File | Area | IDs |
|---|---|---|
| [input-hotkeys.md](input-hotkeys.md) | `Core/Input`, `Core/Hotkeys`, tests, build | IN |
| [autocorrect.md](autocorrect.md) | `Features/Keyboard/Autocorrect` | AC |
| [keyboard-engine.md](keyboard-engine.md) | `Convert/`, `Settings/`, Keyboard root, `tools/langpack` | KE |
| [features.md](features.md) | Desktops, Launcher, Microphone | FT |
| [ui-platform-kernel.md](ui-platform-kernel.md) | `UI/`, `Platform/`, `Core/*`, `main.cpp`, CMake | UP |

## Mocks

```
g++ -std=c++23 -O2 -Imocks mocks/hotkey_lookup.cpp
g++ -std=c++23 -Imocks -Isrc/Core mocks/router_stuck.cpp src/Core/Input/Router.cpp
g++ -std=c++23 -O1 -Imocks -Isrc/Core mocks/hold_fuzz.cpp src/Core/Input/Hold.cpp
g++ -std=c++23 -Imocks -Isrc/Core tests/Input/RouterTest.cpp src/Core/Input/Router.cpp src/Core/Input/Hold.cpp
```

| Mock | Result |
|---|---|
| `hotkey_lookup` | two hash lookups per key down 12-26 ns; a 256-bit "vk ends a mask" guard 1.8 ns |
| `router_stuck` | stage enabled or router reset mid-press, then a consumed autorepeat: app sees the down, never the up |
| `hold_fuzz` | 20,000 random runs of type, commit, echo, time: 0 lost, 0 duplicated, 0 reordered, hold always ends |
| `RouterTest` | existing tests pass on Linux with the shim |

## Size metrics

- 18.4k lines of `.cpp/.hpp/.h` in `src`, `tools`, `tests`; 820 functions, 20 over 50 lines, 9 over 80.
- Longest: `ActionDialog::DialogProc` 194, `langpack _runRepl` 128, `WinMain` 125, `WordTracker::_onKey` 117,
  `SettingsRows::Build` 104.
- Comments are 11% of lines. Densest: `KeyNames.cpp` 55% (200 trailing `// VK_x 0xNN`), `Input.hpp` 45%,
  `InputLanguage.hpp` 44%, `ActionRegistry.hpp` 40%, `Overlay.hpp` 35%.
- Heaviest nesting: `ActionDialog.cpp` (112 of 377 lines indented 20+ columns), `Editor.cpp` (49 of 274).

## Folder proposals

Only where a folder has several unrelated concerns. Moves are mechanical; the layering script covers `Features/`.

- `Platform/` (19): `Windows/` (Foreground, WindowCatalog, Controls), `Gfx/` (Gdi, LayeredWindow, TrayIconTheme),
  `Diagnostics/` (Logger, CrashHandler), `Elevation/` (UACService).
- `Features/Desktops/` (20): `Shell/` (VirtualDesktops), `Windows/` (WindowList, Placer), `Settings/`
  (Page, Preview, Painter, Editor); `Desktops.cpp` and `Tracker` stay.
- `Features/Keyboard/` root (12): `Layout/` (InputLanguage, LayoutLayer), `Settings/` (KeyboardPage), `KeyboardPacks` next to `Convert/`.
- `Features/Keyboard/Autocorrect/` (15): `Tracker/` (WordTracker, TypedWord, WordEdit), `Rules/` (UserRules, Learning).
- `Core/` (14): `Config/` (AppConfig*), `Registries/` (ActionRegistry, Tokens, Overlay, Tray, SettingsHost).
