# Review tasks

Closed on Windows, 2026-09-29: MSVC MinSizeRel and Debug build, 11/11 tests in each, no skipped groups;
langpack and detection benchmark checks pass. The 125-case regression corpus passes with and without
local Punto rules. Update JSON types now have external linkage, fixing MSVC C7631 in Glaze.

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

## Measured decisions

MSVC 19.51, x64 `/O1`, median of seven batches. Probes use the real sources; timings are warm.
Scratch harnesses: `C:\Temp\easylauncher-perf-close-20260929` (`Build.ps1`, `*Bench.cpp`, `PassiveProbe.cpp`).
No input injection, foreground activation, real desktop switching or microphone writes.

- [x] **Text layout: cache.** Layout measure 19.28 -> 3.03 us; measure + bitmap render 24.36 -> 6.17 us.
  `Gdi::TextLayout` shares the cache between layout and notification pills, with invalidation on text,
  font or size change and release when hidden. Pixel tests cover those changes, width and reset.
- [x] **IN-2: keep lookup.** Actual Router + Hotkey down/up: 24.93 ns, guard 20.27 ns.
  Queued roundtrip: 9.26 vs 9.21 us, overlapping ranges. A second lookup index is not worth this gain.
- [x] **IN-6, UP-17: keep action ownership.** Copying a real feedback closure with two 80-byte strings:
  78.3 ns / 3 allocations, shared wrapper 5.9 ns / none. Dispatcher roundtrip 3.79 vs 3.52 us.
  The sub-microsecond gain per hotkey does not justify shared callback state and lifetime changes.
- [x] **AC-1: keep verdict Post.** Two 16-character verdict strings plus queue handoff/ack: 9.82 us,
  4 allocations including the strings. Do not move result copying or a new lock into the input hook.
- [x] **IN-9: keep tap-only mouse hook.** Passive idle: zero moves, buttons or keys in 30 s. Mouse-move
  proc body: 3.4 ns; queued invocation/ack proxy: 9.14 us. These are not a hardware LL-hook roundtrip
  or a 1000 Hz mouse trace. Dynamic hook changes reset the router; click-cancels-tap semantics stay.
- [x] **KE-3: keep layout notifications.** Passive desktop: 8 window-create callbacks in 30 s,
  0.781 ms total layout-read time. A private-desktop burst delivered 2,002 callbacks in 31.0 ms;
  process scoping delivered 1,001 in 24.1 ms (includes producer startup). A 40k unpaced burst lost
  delivery markers and was excluded from timing comparisons. No interactive Chromium/Electron
  workload was captured. Scoping/removal can miss external layout changes or cross-process focus;
  the measured idle load does not warrant changing that behavior.
- [x] **FT-8: keep desktop API.** Three real desktops, read-only: Current 276 us, Count 8.9 us,
  Names batch 257 us; cached connection lock + references 104 ns. No new step-specific API for this.
- [x] **FT-15: keep callback dispatch.** Real callback with a no-op subscriber: atomics 3.8 ns,
  atomics + Event lock 10.7 ns. This measures uncontended dispatch, not the UI post or startup contention.
- [x] **FT-19: keep bounded polling.** Empty `PeekNamedPipe` 1.32 us; at 50 polls/s that operation costs
  about 66 us/s during capture only, excluding wakeup overhead. Preserve the leaked-grandchild timeout;
  overlapped pipes are a larger change, and window-raising polling remains bounded to 3 s.
- [x] **KE-16: keep pack hashes.** Current basis 13.1 ns vs standard basis 13.5 ns for 16 characters.
  No speed benefit; changing it invalidates every existing pack.

## Deliberately outside this performance pass

- **UP-18** WAV resampling changes sound quality; keep the assets.
- **AC-9** Logging through the hold changes the behavior being observed; keep the existing path.
- **UP-14, Core regroup** Folder moves do not improve runtime; keep ownership and layout.
- **UP-10** Injection and its cold-path wait were not exercised or changed.
- **UP-5** Download hash verification remains a separate integrity task.
- **IN-13** Probe deduplication and Linux CI remain tooling work.

## Manual checks not run

Hotkeys and hold under real typing; autocorrect fix, undo and the password guard;
config save and `.bad`; settings Tab, Enter, Esc and Cancel; elevation prompt decline; update swap (plain and
elevated); overlay drag and click-through; UIAccess overlay after settings close; `vd.*` and `Placer` on real
desktops; a `{stdout}` command with `R&D` in a folder name; tray digit icons above 12; delay-loaded DLLs start.
