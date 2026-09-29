# Feature slices: Desktops, Launcher, Microphone

Files: `Features/Desktops/**`, `Features/Launcher/**`, `Features/Microphone/**` (4,008 lines). Reviewer: area subagent; FT-3, FT-4 re-checked.

## Verdict

No idle-footprint defect: no timers, threads are pool waits plus a 150 ms peak tick that runs only for "Muted or talking".
Real problems: explorer RPCs on the single action worker, an unserialized animated switch, three `{stdout}` command-runner defects,
a stale-device race in `AudioManager`, and no tests for the pure logic. The rest is DRY and structure work.

## Findings

### FT-1 [Med][Perf] `Desktops/VirtualDesktops.cpp:297-306`
`Switch` calls `WaitForAnimationToComplete` on the single serial action worker, so a mute, push-to-talk release or volume hotkey queues behind a desktop animation. Every `vd.*` action is a blocking explorer RPC on that worker (`Dispatcher` is one thread and one queue; `CommandRunner.hpp:106` makes the same argument for `ShellExecute`).
Fix: run `vd.*` bodies on the pool as `Placer::_submit` does, or coalesce `Switch`.

### FT-2 [Med][Bug] `VirtualDesktops.cpp:297`
`_call` locks only the connection: wait then `SwitchDesktopWithAnimation` is check-then-act and runs concurrently from the action worker (`vd.switch`) and the pool (`Placer.cpp:104,110`, Follow). Concurrency confirmed by reading. That it crashes explorer is an assumption from MScholtes VirtualDesktop 1.21 notes ("simultaneous calls of animated switch function no longer cause Windows Explorer to crash").
Fix: one `static std::mutex` in `Switch`.

### FT-3 [Med][Bug, security] `Launcher/CommandRunner.cpp:248-261` (checked)
The comment above says the shell runs a bound app "as the user rather than with our token, which is what keeps a bound app off administrator rights". When `ShellLaunch::Run` fails (explorer restarting), the fallback `ShellExecuteExW` launches with our token: `ShellLaunch.hpp:152-155` documents that this hands admin rights to the app when we run elevated.
Fix: drop the fallback and log, or retry the shell: -12 lines.

### FT-4 [Med][Bug] `CommandRunner.cpp:54-56,165` (checked)
In `{stdout}` mode the line goes through `cmd /d /s /c`, but `_quote` quotes only when the value contains a space. A folder like `R&D`, `a^b` or `50%` splits or expands the command.
Fix: quote when the value contains any of `` &()^%!,;= `` or a space; harmless on the `ShellExecute` path. Test with the pure `_quote`.

### FT-5 [Med][Bug] `CommandRunner.cpp:189,213,126,98`
Capture stops at 4096 bytes and then `TerminateProcess` kills the command though only 120 chars are shown; it also kills at 10 s while `Launcher.cpp:27`, `CommandRunner.hpp:114` and the token text say "minutes" / "waits for it to finish". `_flatten` cuts at 120 bytes and the 4096 cut lands anywhere: Cyrillic can end mid code point, `_toUtf8` sees invalid UTF-8 and re-decodes the whole buffer as OEM: mojibake.
Fix: keep draining and discard the excess, cut on a code-point boundary, make docs match the code.

### FT-6 [Med][Perf] `Microphone/Microphone.cpp:75-79,122,320-323`
`_adjustVolume` sends `SetMasterVolumeLevelScalar` to the audio service from the UI thread on every session change (`Refresh`) and every level echo, even when the device is at the kept level.
Fix: guard with `GetVolumePercent() != _settings->Volume` (an atomic read): one audiodg RPC less per event, UI thread off a stalled audio service.

### FT-7 [Med][Perf] `Desktops/Editor/Editor.cpp:51-80,163`
Every `WM_MOUSEMOVE` during a drag runs `_limits()`: rebuilds obstacles and guides, `EnumDisplayMonitors`, `GetMonitorInfoW`; none changes during a drag. `Painter::Tiles` also builds a GDI+ font per paint (`Painter.cpp:107`).
Fix: build `Limits` once in `WM_LBUTTONDOWN`.

### FT-8 [Med][Perf] `VirtualDesktops.cpp:164-181,208,220`
`_indexOf` costs 2N RPCs (`GetAt` and `GetID` per desktop) on every `Current()` and `DesktopOf(window)`. `Page::_capture` (`Page.cpp:177`) pays it per window; `vd.switch next` is about 5+2N RPCs; `Tracker` pays it on every registry write.
Fix: `GetAdjacentDesktop` for next/prev; one id fetch per per-window loop. To check: the registry `VirtualDesktopIDs` blob beside the `CurrentVirtualDesktop` value `Tracker` already reads may give the index with zero RPCs.

### FT-9 [Med][Structure] `Desktops/Placer.cpp:75-118`
`_placeNew` repeats place then `MoveWindow` three times with tangled `follow/moved/followed` flags: about 12 explorer RPCs and 1-4 s of a pool thread per window. Race: two windows of one app both visible before either is counted (session restore) each see the other, `open` is 1 for both, both take `Places[1]` and `Places[0]` goes unused.
Fix: loop over `{0, 300, 700}` ms with one `_settle(window, where)`; serialize per exe or count only earlier windows.

### FT-10 [Med][DRY] `Launcher/ShellLaunch.cpp:82-95`
`Switchable()` is a second definition of "a window the user can switch to"; `WindowCatalog::AppWindows` already covers it and adds the cloak and APPWINDOW rules. Use it: -14 lines and a header declaration.

### FT-11 [Med][Test] `tests/`
Only `Input` and `Keyboard`. Nothing covers `Tiles::HitTest/Drag` (the header says it has no window), `_split/_quote/_expandDir/_flatten`, `Desktops::_parse`, `Placer::Load` (the k-th-rule mapping). FT-4 and FT-5 would be caught. Move the pure functions to internal headers and add cases.

### FT-12 [Low][DRY] `Desktops/Editor/Tiles.cpp:49-75,106-145`
Six mirrored X and Y loops (`_sweepX/_sweepY`, four in `fitX/fitY`): run the X code on a transposed `RECT`, about -40 lines. The `static std::vector` in `Drag` (182-189) and `Editor::_limits` is premature optimization and hidden state.

### FT-13 [Low][Bug] `Microphone/Wasapi/AudioManager.hpp:23-30,119-123`
`_reinitPending = false` is set after `_initDevice()` and `_defaultChanged()`; a default-device change in between hits `exchange(true)` and is dropped: the controller stays on the stale device. Clear the flag right after `Sleep(100)`.

### FT-14 [Low][Bug] `Wasapi/AudioDeviceController.cpp:90-109`
`WatchForSessions` enumerates sessions and only then calls `RegisterSessionNotification`; a session created in that gap is never watched, so "hide when inactive" can hide the pill while capturing. Register first, then enumerate.

### FT-15 [Low][Bug] `Wasapi/Callbacks.hpp:81-88`, `Microphone.cpp:94,122,168`
`OnNotify` calls `Event::operator()`, which takes a `std::mutex`, and the handler does `new std::function` plus `PostMessage`; the rules say atomics only. In practice only startup subscribe contends. `mic.toggle_mute` flips from the cached device state and `_shownMuted` locally, so two presses inside the echo latency leave the display wrong.

### FT-16 [Low][Readability]
- Launcher is 22% comment lines (160 of 723): `ShellLaunch.hpp` 32 of 47, `CommandRunner.cpp` 54 of 284. `Launcher.cpp:19-28` documents `MakeRun` but sits on `StdoutToken`.
- Stale claims: `WindowList.hpp:10-11` says the UI thread runs the LL hooks (`architecture.md`: the input thread); `Tracker.hpp:10` says COM is never used on the UI thread but `Page.cpp:305,177,321` do, so a hung explorer freezes settings; `VirtualDesktops.hpp:11` says the layout is picked by build but `.cpp:197` range-checks a single layout.

### FT-17 [Low][Structure]
`Desktops/` has 20 files (grouping in the README). `Desktops.cpp:89-131` `_remember` holds the inverse of Placer's k-th-window rule: move it next to `Placer`. The mic module keeps six state atomics (`Microphone.cpp:33-50`), three mirror the controller's own.

### FT-18 [Low][DRY]
"Desktop N" fallback name written three times (`VirtualDesktops.cpp:237,258`, `Page.cpp:54`); the `pid != self` filter in `Page.cpp:133-143` and `WindowCatalog::AppNames`; manual `CoInitialize`/`CoUninitialize` at `CommandRunner.cpp:232-264` duplicates `UACService::ComScope`; `RegisterClassW` hand-rolled three times, `Editor` re-registers on every `Run` (222).

### FT-19 [Low][Perf]
`_capture` polls at 20 ms (up to 50 wakeups/s for at most 10 s per `{stdout}` command); `RaiseNew` polls at 100 ms for up to 3 s. Only after a hotkey, idle cost zero. Overlapped `ReadFile` plus `WaitForMultipleObjects` removes the polling and the 4 KB coupling; `_capture` is 82 lines with 3 manual `CloseHandle`s.

### FT-20 [Low][Readability]
`Editor.cpp:172-179` nested ternary for the cursor; `Page.cpp:265-297` 20 hard-coded dialog-unit rects; `Tracker.cpp:35,81` hardcodes `12`, so desktop 13 and up gets a null tray icon: use `std::size(_digits)`.

## Keep

- `Tracker`'s registry watch with no polling; `VirtualDesktops::_acquire(stale)` reconnect-once.
- `AudioDeviceController::_later`: session work goes to the pool; `weak_ptr` plus `Detach` avoids releasing the last reference inside a callback.
- The overlay tick that stays off unless the layer wants it; no slice includes another slice.
