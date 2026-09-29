# Core/Input, Core/Hotkeys, tests, build

Files: `Core/Input/*`, `Core/Hotkeys/*`, `Core/Dispatcher.*`, `tests/`, `build.ps1`, `CMakeLists.txt`.

## Verdict

The design does what `input.md` claims. Reviewed against code: hook procs do a table lookup and a
function-pointer call, the mouse hook is up only for buttons, `WM_TIMER` exists only while a hold is up,
state is owned by one thread. Findings are one edge-case bug, mask-layout knowledge spread over four
files, and a long `_raiseAction`.

## Findings

### IN-1 [Med][Bug] `Input/Router.cpp:104-142` (mock)
A stage enabled while a key is held, or a router `Reset` (hook installed, desktop switch) while it is held,
forgets `Down[vk]`. The next autorepeat looks like a fresh down; a stage that answers `Consume` takes
ownership and swallows the up. The app saw the down and never the up, so the key is stuck until the next physical press.
Repro: `mocks/router_stuck.cpp`, both scenarios print `app saw down without up: 1`.
Fix: a router-wide `_delivered` bitset (set when a down is returned false, cleared on any up, not cleared by `Enable`).
A `Consume` on a down with `_delivered[vk]` swallows that event only and takes no ownership. About 8 lines plus a test in `RouterTest.cpp`.
Realistic trigger: applying hotkeys in settings while holding Enter, or config change while a bound key repeats.

### IN-2 [Low][Perf] `Hotkeys/HotkeyService.cpp:244-253`, `156-159` (mock)
Every non-repeat key down does two `unordered_map<uint64_t,...>::find`, even when no binding ends in that key.
12-26 ns per pair in the mock (MSVC's FNV-1a over 8 bytes is not cheaper). Only a precomputed guard skips them: a
`std::bitset<256>` of "vk ends a mask, or is a modifier of a modifier-only mask", built in `Publish`, 1.8 ns.
Marginal against the hook round trip (tens of microseconds): add it only if an end-to-end measurement asks for it.

### IN-3 [Med][Structure] `Hotkeys/HotkeyService.cpp:244-298`
`_raiseAction` is 55 lines doing four things: foreground lookup, per-app match, pending-press resolution, dispatch plus release claims.
Name says "raise action" but it is `_onKeyDown`. Split into `_foregroundExe(apps)`, `_resolvePending(...)`, `_dispatch(matches)`.
After the two lookups, `if (!singleEntry && !sequenceEntry && !_pendingMask) return false;` is exactly equivalent to what follows (checked: with no entry and no pending press the rest only builds empty `Match`es) and flattens the function.
`single`, `sequence`, `counting`, `entry`, `MaskEntry`, `Match` overlap in meaning; `Match::At` hides a two-scope fallback.

### IN-4 [Med][DRY] `Hotkeys/KeyChord.hpp`, `KeyNames.cpp:156-203`, `HotkeyService.cpp:186-196,245`
The mask layout (byte 0 modifiers, bytes 1-7 keys, newest in byte 1) is decoded in four places:
`KeyChord::Press/Release`, `HasMouseButton`, `KeyNames::Format` loop, `HotkeyService::_contains` and `vk << 8`.
Fix: `KeyChord.hpp` owns `SingleKey(vk)`, `Contains(mask, vk)` and a `Keys(mask)` range; the others call them. -25 lines, one place to change the layout.

### IN-5 [Low][Simplicity] `Hotkeys/HotkeyService.cpp:24-40`
`MaskEntry.apps` is an `unordered_map<string, Scope, AppHash, equal_to<>>` with a hand-written transparent hash, per mask, for a handful of apps.
A sorted or plain `vector<pair<string, Scope>>` with linear `find_if` removes `AppHash`, about 10 lines, and the per-mask hash node allocations at idle.
Same for `Table::Hotkeys` (tens of masks): a sorted vector with `lower_bound` is smaller and cache-friendlier.

### IN-6 [Low][Perf] `Feedback.hpp:68-83`, `HotkeyService.cpp:113-124`, `Dispatcher.cpp:96-104`
`Dispatcher::Post(binding.onPress)` copies a `std::function` whose closure is about 144 bytes (function 64, two strings, volume, this): one heap allocation plus string copies per press, inside the hook.
`input.md` allows allocation "to hand work off", so this is acceptable; if touched, store `shared_ptr<const ActionFn>` in `HotkeyBinding` and post that (see UP-17).

### IN-7 [Low][Simplicity] `Input/Input.hpp:88`, `Input.cpp:44-53,380-391`
Two type-erasers: `Post` takes `move_only_function`, `Edit`, `EditJob`, `_landed`, `HotkeyBinding` take `std::function`.
`Edit` copies its closures (the tracker copies a 224-byte `Word` into it, AC-6). Use `move_only_function` in `Edit`/`EditJob`/`_landed`: one erasure instantiation less and no copy requirement.
`Post` also does `new Work(std::move(work))` around an already type-erased object; a raw pointer round trip is enough.

### IN-8 [Low][Readability] `Input/Input.cpp:103-108`, `Hold.cpp:74-85`
`_release()` calls `Hold::Tick(UINT64_MAX)` twice. It works because `_deadline = now + TimeoutMs` wraps to 149 and the second tick then passes; an unsigned overflow the reader has to prove. Add `Hold::Flush()` that spills the ring and forgets in-flight events. Removes the `phase` loop and the comment.

### IN-9 [Low][Idle] `Hotkeys/HotkeyService.cpp:361`
`tapOnly` sets `WantsButtons`, so `WH_MOUSE_LL` stays up all session and the proc runs for every mouse move (early return, but the call itself is a cross-thread hook round trip at 500-1000 Hz on a HIGHEST priority thread). Assumption, not measured. Measure input-thread wakeups with a tap-only binding first. Dynamic install is blocked by `Router::Reset` on every hook change.

### IN-10 [Low][Structure] `Hotkeys/HotkeyCapture.hpp`, `Bindings.hpp`
`HotkeyCapture` is a header with `inline` globals in `Detail::` while its sibling `HotkeyService` is `.hpp/.cpp` with an anonymous namespace; every includer pulls `windows.h`, `<atomic>`, `<functional>`. `Bindings::Apply` is a 51-line `inline` function in a header that includes config, feedback, foreground. Move both bodies to `.cpp`. Same scaling for `Apply`'s four-way handler `if/else`: flatten with early `continue`.

### IN-11 [Low][Readability] `Hotkeys/KeyNames.cpp`
55% comment lines: 200 trailing `// VK_x 0xNN` that restate the table. `A-Z`, `0-9`, `F1-F24`, `NumPad0-9` can be generated in a `constexpr` builder, leaving about 60 named entries: -120 lines. `Format` builds two strings and inserts at the front in a loop (48 lines); collect names into a small array and join (about 20 lines). Not hot (config load and UI).

### IN-12 [Low][Structure] message ids
`HotkeyCapture::WM_CAPTURE_DONE = WM_APP + 1` equals `MainWindow::WM_SHOW_NOTIFICATION`; `WM_APP + 2` is both `WM_OVERLAY_RELAYOUT` and `ActionDialog::WM_AUTOBIND`. Safe today only because each goes to a different window (checked by grep). One `WindowMessages.hpp` with distinct values removes the trap.

### IN-13 [Low][Test/DRY] `tests/`, `build.ps1`, `CMakeLists.txt`
- `_failures` and `Check` are copied in four test files; `build.ps1 -Test` repeats run-and-throw five times (loop over names); `CMakeLists.txt` repeats include, `NOMINMAX UNICODE _UNICODE`, `/utf-8` per test (one `add_unit_test()` function).
- `HotkeyProbe.ps1` and `AutocorrectDeathProbe.ps1` share about 15 `DllImport`s and the window enumeration helper: extract `tests/Probe.Native.ps1`.
- There is no CI (`.github` absent). `RouterTest` and `Hold` have no Win32 calls: they compile and pass on Linux with a 13-line shim (checked), so a cheap job could guard the pipeline.

## Keep

- `Router` plus `Hold` as Win32-free units with `RouterTest`; the random test above found nothing.
- Hooks on demand, moves and wheel skipped by `switch`, hold timer only while a hold exists.
- `KeyChord` as one `uint64_t`; per-level owners (`Owners`) for remap-safe ownership.
