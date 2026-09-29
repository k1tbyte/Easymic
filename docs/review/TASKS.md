# Review tasks

Status after the fix pass. Everything below was checked with a mingw syntax pass over the whole tree, a mingw
link of every source set (app and tests), native runs of the Win32-free tests (`RouterTest`, `TypedWordTest`,
`UserRulesTest`, `hold_fuzz`), and equivalence programs where a table or a function was rewritten. Nothing was
built with MSVC or run on Windows: see "Needs a Windows run".

## Done

- [x] **UP-1** layer timer only while a layer asks (mic: listening or a peak phase left).
- [x] **IN-1** delivered-key bitset per level; `router_stuck` scenarios and a send-back case are in `RouterTest`.
- [x] **UP-2** config saved through `.tmp` and `MoveFileExW`; an unparseable file is renamed `.bad`.
- [x] **UP-3** Cancel never saves; `SkipVersion` writes only its own field.
- [x] **FT-3, FT-4, FT-5** no own-token fallback; `_quote` quotes `&()^%!,;=`; capture drains past the display limit and cuts on a code point (`CommandLine.{hpp,cpp}`, `CommandLineTest`).
- [x] **FT-1, FT-2** `vd.*` on their own pool lane; one mutex around the animated switch.
- [x] **FT-13, FT-14** re-init flag cleared first; session notification registered before enumerating.
- [x] **KE-1, KE-2, KE-4** pack errors logged; restore skips an unchanged resolve; `Judge::Use` clears the kept verdict.
- [x] **UP-6, UP-7** `/DELAYLOAD` for wininet, version and oleacc; version read on first use; overlay font validated at the first non-empty slot; GDI+ without its background thread.
- [x] **UP-4, UP-5, UP-8, UP-9, UP-10, UP-11** `IsDialogMessage` for settings; rename-swap update; shutdown order; `OverlayWindow.hpp` split out of `BaseWindow`; shadow HWND reused; one `NIM_MODIFY`.
- [x] **UP-12, UP-13, UP-14 (local), UP-15, UP-16** dialog edits a `Binding`; `DialogProc` split; `Controls::Text` and `Controls::Panel`; `WinMain` split; `OrderedInsert.hpp`; text layer caches its font.
- [x] **FT-6, FT-7, FT-9, FT-10, FT-12, FT-16, FT-18 (part), FT-20** guarded volume write; drag limits once; settle loop; `WindowCatalog::AppWindows`; one axis code path in `Tiles`; stale comments gone.
- [x] **AC-2, AC-3, AC-5, AC-6, AC-8, AC-10 to AC-18, KE-6** `WordTracker`/`WordHold`/`WordState`, `Runtime`/`Decide`/`AutocorrectLog`, `AppFilter`, honest skips in `AutocorrectTest`, `--bench` gone.
- [x] **AC-4, KE-14, FT-11** `TypedWordTest`, `PackTest`, `TargetTest`, `TilesTest`, `CommandLineTest`.
- [x] **KE-5, KE-7 to KE-11, KE-13, KE-15, KE-16 (part), KE-17** `PackSide`, `Iso639`, `Detector` split, `wstring_view`, langpack command table, layout ring cached.
- [x] **IN-3, IN-4, IN-5 (per-app), IN-7 (types), IN-8, IN-10, IN-11, IN-12** `_onDown` split, mask helpers in `KeyChord`, `Hold::Flush`, `.cpp` bodies, generated `KeyNames` table (identical to the old one over 5.2M comparisons), message ids apart.
- [x] **IN-13 (part)** `tests/Check.hpp`, `add_unit_test()` and `ctest` (`build.ps1 -Test` is 6 lines shorter), one architecture check per build.
- [x] **UP-19** architecture script also guards Core to UI/Features and UI to Features; `Core/Overlay.hpp` forward-declares GDI+.
- [x] **UP-20, comments** comment lines 2017 to 619 (11.0% to 3.4%).
- [x] **Folders** `Platform/{Diagnostics,Gfx,Windowing,System}`, `Desktops/{Settings,Placement}`, `Keyboard/{Layout,Settings}`, `Autocorrect/{Tracker,Rules}`.

## Not done, with the reason

- [ ] **IN-2** hotkey lookup guard: 13-26 ns per key, measure end to end first.
- [ ] **IN-6, UP-17** shared `ActionFn` per press: allocation is allowed for hand-off, measure first.
- [ ] **IN-9** tap-only keeps the mouse hook up: measure input-thread wakeups first.
- [ ] **KE-3** `EVENT_OBJECT_CREATE` hook: count events per second in Chromium and Electron first.
- [ ] **KE-16** FNV basis: changes every hash, needs a `PackVersion` bump.
- [ ] **UP-18** WAV resampling: quality call, no code.
- [ ] **AC-1** verdict slot: needs a lock or an allocation on the input thread, the Post hand-off was chosen for that.
- [ ] **AC-9** kept-verdict log through the hold: would change the behavior the log observes.
- [ ] **FT-8 (part)** adjacent-desktop call saves nothing without new step variants.
- [ ] **FT-15 (mutex)** contended only at startup. **FT-19** every smaller correct option races or adds an ACL surface.
- [ ] **UP-14 (slice move)** `UpdateManager` depends on UI code. **UP-10** shellcode copy and `Sleep(100)` untouched.
- [ ] **UP-5** hash check. **IN-13** probe P/Invoke dedupe and a Linux job for `RouterTest` (no PowerShell here).
- [ ] **Core/** regroup: 15 kernel files, named in the docs, left flat.

## Needs a Windows run

`.\build.ps1 -Test`; then by hand: hotkeys and hold under real typing; autocorrect fix, undo and the password guard;
config save and `.bad`; settings Tab, Enter, Esc and Cancel; elevation prompt decline; update swap (plain and
elevated); overlay drag and click-through; UIAccess overlay after settings close; `vd.*` and `Placer` on real
desktops; a `{stdout}` command with `R&D` in a folder name; tray digit icons above 12; delay-loaded DLLs start.
