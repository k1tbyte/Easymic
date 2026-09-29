# Review tasks

Ordered by value over cost. Size: S under 30 lines, M under 150, L more. IDs point into the area reports.

## Now: bugs, data, idle

- [ ] **UP-1** S - `OverlaySurface.hpp:112`: drop `slot.Width > 0 ||`; `MicLayer::_measure` returns `.WantsTick = _listening() || _peakPhase`. Done when a muted pill leaves no `WM_TIMER` (Spy++ or wakeup count).
- [ ] **IN-1** S - router-wide delivered bitset, `Consume` on an already delivered down takes no ownership. Add the two `router_stuck` scenarios to `RouterTest.cpp`.
- [ ] **UP-2** S - `File::Write` to `.tmp` then `MoveFileExW(REPLACE_EXISTING|WRITE_THROUGH)`; on parse failure rename the old file to `.bad`.
- [ ] **UP-3** S - no `Save` on declined elevation; save only right before elevating; defer `SkipVersion` while settings are open.
- [ ] **FT-3** S - drop the `ShellExecuteExW` fallback in `CommandRunner.cpp:248-261`, log instead.
- [ ] **FT-4** S - quote on any of `` &()^%!,;= `` or space in `_quote`; test.
- [ ] **FT-5** S - keep draining after 4 KB, cut on a code-point boundary, make docs say 10 s.
- [ ] **FT-2** S - one mutex around `Switch`.
- [ ] **KE-4** S - clear `_kept` in `Judge::Use` or own the locale string in `Verdict`.
- [ ] **FT-13, FT-14** S - clear `_reinitPending` before `_initDevice`; register the session notification before enumerating.

## Next: footprint and latency

- [ ] **UP-6, UP-7** S - `/DELAYLOAD:wininet.dll;version.dll`; validate the overlay font at the first non-empty slot; GDI+ `SuppressBackgroundThread`. Measure private bytes before and after.
- [ ] **KE-3** S - count `EVENT_OBJECT_CREATE` callbacks per second in Chromium and Electron; drop the hook if high (foreground event plus `Requested()`).
- [ ] **KE-2, KE-1** S - skip `Autocorrect::Resolve` when settings and layouts are unchanged; pass `&error` into `Pack::Load` and log it.
- [ ] **FT-1** S - run `vd.*` bodies on the pool like `Placer::_submit`, or coalesce `Switch`.
- [ ] **FT-6, FT-7, FT-8** S - guard `_adjustVolume`; build editor `Limits` once per drag; `GetAdjacentDesktop`, one id fetch per loop.
- [ ] **UP-4** S - `IsDialogMessage` on the settings handle in the message loop.
- [ ] **UP-5** M - rename-swap update instead of PowerShell; verify a hash.
- [ ] **UP-11** S - one `NIM_MODIFY` per refresh, skipped when unchanged.
- [ ] **AC-1** M - judge writes the verdict into a slot; post only for `Early.WrongLayout`.
- [ ] **IN-2** S - 256-bit "vk ends a mask" guard in `Publish`, only if an end-to-end measurement asks for it.

## Next: simplify and decompose

- [ ] **AC-2, AC-3, AC-7** M - split `WordTracker::_onKey` and `_apply`; `struct Held`, `struct Erased`; hold lifecycle to its own file (the file is at 497 of 500 lines).
- [ ] **UP-12** M - `ActionDialog` edits a `Binding` copy; `DialogProc` (194 lines) into Fill and Read: about -50 lines.
- [ ] **KE-5** M - `PackSide _sides[2]` in `KeyboardPage.cpp`: about -40 lines, `optional` for "not found".
- [ ] **IN-3, IN-4** M - `KeyChord` owns mask helpers; split `_raiseAction`.
- [ ] **KE-7, KE-9, UP-13** M - one `Iso639(HKL)`, `Controls::Text`, `Controls::Panel`; delete the `Console` copies of `Str` helpers.
- [ ] **UP-9** M - move geometry and shadow window out of `BaseWindow` into `OverlaySurface`: -80 lines.
- [ ] **KE-6** S - move `KeyboardExclusions::Filter` next to `WordTracker`; break the root/`Autocorrect` include cycle.
- [ ] **KE-8, FT-9, FT-12** M - split `Detector::Detect`; one settle helper in `_placeNew`; transpose in `Tiles`.
- [ ] **IN-5, IN-7, IN-8, IN-10, IN-11, IN-12** S each - per-app vector, one `move_only_function`, `Hold::Flush`, headers to `.cpp`, `KeyNames` table, message ids.

## Later: structure and tests

- [ ] Folder moves listed in `README.md` (Platform, Desktops, Keyboard root, Autocorrect, Core).
- [ ] **UP-19** extend `check-architecture.ps1` to Core, UI and the `Hotkeys/X` include rule; run it once per build.
- [ ] **AC-4, AC-5, KE-14, FT-11** tests: Win32-free tracker transitions; count a skipped detection or UI test as a failure; `Pack::Load` rejections; `_quote`, `_flatten`, `Tiles::HitTest`.
- [ ] **IN-13** `add_unit_test()`, shared `Check`, `-Test` loop, shared probe P/Invoke file, optional Linux job for `RouterTest`.
- [ ] **UP-20, FT-16** trim comments that restate code (densest files in `README.md`); fix the stale claims listed in FT-16.
- [ ] **KE-16** fix the FNV basis at the next `PackVersion` bump.
