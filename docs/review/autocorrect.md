# Autocorrect

Files: `Features/Keyboard/Autocorrect/**`, `tests/Keyboard/{AutocorrectTest,UserRulesTest}.cpp`. Reviewer: area subagent; AC-1 re-checked.

## Verdict

The hot path is sound: no timers or idle wakeups, no pack access on the input thread, heap use only where work is handed off.
The costs are one `Post` plus allocations per typed key, and a 497-line tracker with 19 mutable globals that no test can drive.
No reachable correctness bug found.

## Findings

### AC-1 [Med][Perf] `Judge.cpp:79-88` (checked)
Every coalesced run ends in `Input::Post` (heap `Work`, a `Result` with two `Verdict`s and six wstrings); the input thread then dispatches it and frees the old `_ready`. Per typed key.
Fix: the pool writes the verdict into a slot under `_lock`; `Ready()` copies it on Space; post only when `Early.WrongLayout`. Saves one queue message, about 2 allocations and 3 frees per key.

### AC-2 [Med][Structure] `WordTracker.cpp:165-281`
`_onKey` is 117 lines, 4 levels deep (Backspace at 206, Space at 226, typing at 259), returns `Next` six times and never anything else.
Fix: `void _track()` plus a one-line wrapper; extract `_onBackspace`, `_onSpace`, `_onTypingKey` with guard clauses.

### AC-3 [Med][Structure] `WordTracker.cpp:44-66,283-298,300-447`
497 of 500 lines and 19 globals. Seven `_held*` fields are reset by hand in `_reset` (16 lines) and `_onHold` (13).
Fix: `struct Held`, `struct Erased`, reset is `x = {}` (-15 lines); move the hold lifecycle (`_onHold` to `_autoResult`, `ConvertWord`, `Undo`, about 150 lines) to its own file.

### AC-4 [Med][Test] `tests/Keyboard/*`
Nothing exercises `WordTracker`, `Judge` coalescing or ordering, `WordEdit::Replace`, `Learning`. The tracker calls Win32 directly (`GetForegroundWindow` 180, `FocusedWindow` 228, `Input::Edit` 129).
Fix: a Win32-free key-to-word transition layer with window and layout injected, tested like `RouterTest`: Backspace, erase and retype, undo, run join.

### AC-5 [Med][Test] `AutocorrectTest.cpp:111-114`
`_detect()` prints "detection skipped" and returns; the test exits 0 without en-US and ru-RU layouts. False green.
Fix: count the skip as a failure, or drive `LayoutTable` from a fake.

### AC-6 [Low-Med][Perf] `WordTracker.cpp:126-149`
The hold lambda copies the whole `Word` (about 224 bytes) even when `ready` is set, and the `Verdict` twice (`optional(*ready)` at 130, `*ready` at 132). `std::function` forces a copyable closure while `Input::Post` takes `move_only_function` (IN-7).
Fix: capture per branch; `Input::Edit` takes `move_only_function`.

### AC-7 [Low-Med][Simplicity] `WordTracker.cpp:351-407,444,494,467-473`
`_apply(id, autoSpace, requested, taught)` is 57 lines with three call shapes and an unreadable bool. `_autoResult` and `UndoAutoConvert` call `Commit(id, {})` though `Input.cpp:247` already queues one; `ConvertWord` relies on that and the others do not. `typed` (374) is rendered only to check `empty()`.
Fix: `_fixHeld`, `_convertHeld`, `_undoHeld`; drop the redundant commits (assumption: the release is identical).

### AC-8 [Low][Perf] `Judge.cpp:20,67,76,121`, `WordTracker.cpp:105-107`
`Snapshot.Layout` is dead: `_judging()` requires `!_word.Layout` before `Typed`, so the ternary at 67 and the check at 76 are constant. `Early` runs for every key after `_word.EarlyAt` is set and `_onEarly` discards it (154).
Fix: `EarlyAt` flag instead of `Layout`; run `Early` only when `MidWord && !EarlyAt`.

### AC-9 [Low-Med][Simplicity] `Judge.cpp:41-43,48-56,133-143`, `Judge.hpp:32`
`_kept`, `_logWork`, `_log`, `LogKept` (about 25 lines plus lock swaps) exist to log kept verdicts off-thread for the opt-in `LogDecisions`.
Fix: with `LogDecisions` on, send Space through the hold path, which already logs at `WordTracker.cpp:137`; costs about 1-3 ms held latency in diagnostic mode only.

### AC-10 [Low][Simplicity] `Autocorrect.hpp:26`, `Autocorrect.cpp:71-73`, `KeyboardExclusions.hpp:48`
`Excluded` is an `unordered_set` filled one by one from `Parse()`, which already returns a sorted unique vector. Keep the vector, `ranges::binary_search`: -4 lines and one container instantiation less.

### AC-11 [Low][Simplicity] `Accessibility.cpp:12-32,64-68,78-89`
The runtime `Api` struct and `_passwordEdit` fallback guard against a system DLL failing to load; `VariantClear` is avoidable; `!window` is checked after `_api()`; `CoInitializeEx`/`CoUninitialize` run per fix.
Fix: `/DELAYLOAD:oleacc.dll`, drop `Api` (about -25 lines), `CoIncrementMTAUsage` once (assumption on teardown cost).

### AC-12 [Low][Perf] `WordEdit.cpp:9-13,48-50`
`MapVirtualKeyW` per event, and Backspace is the same VK N times: 2N calls for an N-key word, made while typing is held. Build the pair once, `insert(end, n, pair)`.

### AC-13 [Low][Readability] `WordEdit.hpp:11-21`, `WordEdit.cpp:5-7`
Masks `0x03 0xFC 0xF0 0x0F` and `MaskVk = 0xE8` depend on `ModifierVks` order with no compile-time link; `ModifierBit` is a linear `ranges::find` on every key event, ups included.
Fix: named bit constants with derived masks and a constexpr 256-entry table like `TypingKeys` (this is on the input thread, O(8) today).

### AC-14 [Low][Bug, latent] `TypedWord.hpp:61-70`
`Type(BACK)` at `Count 0` and `Spaces 0` wraps the `uint8_t` to 255 and returns true; callers never reach that state today. Line 70 `return word.Spaces < MaxSpaces && ++word.Spaces;` mutates inside a condition. Guard the zero case, increment explicitly.

### AC-15 [Low][Structure] `Autocorrect.cpp:53-183`
One file mixes runtime build, decide, teach, password guard, five log functions and feedback. Every log function repeats `if (LogDecisions) Dispatcher::Post(Logger::Log)`; `Guarded` logs synchronously on the lane while the others post.
Fix: `AutocorrectLog` and `Runtime` files; folders `Tracker/` (WordTracker, TypedWord, WordEdit) and `Rules/` (UserRules, Learning).

### AC-16 [Low][DRY] `UserRules.cpp:20-27,68-100`, `Learning.cpp:19-23,32-34`
The `typed` lambda is in `Answer` (71-74) and `Refused` (97-100); `Compiled` converts UTF-8 twice per rule; `Answer` scans `_patterns` once per `always` value; `Learning::Teaching` mirrors `WordRule`.
Fix: one lowercase-cache helper, `Teach(WordRule)`, patterns sorted by `Always`.

### AC-17 [Low][Test] `UserRulesTest.cpp:97-133`
`--bench` and `_oldEntry` (a reimplementation of the removed matcher) are about a quarter of the file: a one-off measurement. Delete or move to `tools/`.

### AC-18 [Low][Readability]
`_typed` means "word changed" (`_wordChanged`), `_gen` is opaque (`_wordVersion`); `Input::Verdict`, `Convert::Verdict`, `Judgement` sit side by side; `WordTracker.cpp` has 36 comment lines, several restating code (`_onTarget` 314, `_typed` 116); brace-less one-line `if`s in `UserRules.cpp` and `KeyboardExclusions.hpp`.

## Keep

- `_pending` coalescing plus the generation check in `Judge`: a burst becomes one run and stale results cannot overwrite newer ones.
- Word as key positions plus the `Judge::Register` callback: no pack access on the input thread, no dependency cycle.
- `Runtime.Packs` as `unique_ptr`: a failed load releases the mapping.
