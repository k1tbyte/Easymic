# Keyboard engine, settings, tools

Files: `Keyboard/Convert/**`, `Keyboard/Settings/**`, Keyboard root, `tools/Keyboard/langpack/**`, `KeyboardConfigTest`, `KeyboardUiTest`. Reviewer: area subagent; KE-1, KE-2, KE-3, KE-16 re-checked.

## Verdict

Nothing here runs in a hook, `Rules` and `Pack` lookups are microseconds (a mock of `Rules::_match` with 40k rules cost about 1 us per miss, so it is not flagged) and there are no timers.
The weak spots are diagnosability (silent pack failures), work redone on every settings close, and duplicated or over-shaped code in `KeyboardPage`, the tools and the settings panels.

## Findings

### KE-1 [Med][Bug] `KeyboardPacks.hpp:60,84`, `Convert/Pack.cpp:54-120` (checked)
`Pack::Load` builds seven distinct error strings; production never passes `error`. After a `PackVersion` bump or a corrupt pack, `Autocorrect.cpp:84` logs "needs both packs in packs/" though the files exist, and `Discover()` drops invalid packs silently.
Fix: pass `&error` in `KeyboardPacks::Load` and `Discover`, `LOG_ERROR` it.

### KE-2 [Med][Perf] `Keyboard.cpp:21-37`, `MainWindow.cpp:303-313` (checked)
`RestoreConfig` runs on every settings close, Cancel included, and reaches `Autocorrect::Resolve`: `InputLanguage::Installed()`, a `LayoutTable` per installed layout (about 440 `ToUnicodeEx` each), a re-map of both packs and a re-parse of every `.rules` file, on the UI thread (`KeyboardPacks.hpp:88`).
Fix: skip `Resolve` when `*_settings == lastResolved` (`operator==` exists) and the layout list is unchanged. Rules file size is an assumption.

### KE-3 [Med][Idle] `LayoutLayer.cpp:58-62` (hook set checked, rate assumption)
`ShowLayout` installs two system-wide `WINEVENT_OUTOFCONTEXT` hooks, one on `EVENT_OBJECT_CREATE`: every object creation in every process queues a callback into the UI thread and the `OBJID_WINDOW` filter runs only after delivery. Opt-in feature; Chromium and Electron with accessibility on are the likely worst case.
Fix: count events per second there first; if high, drive `_read()` from `EVENT_SYSTEM_FOREGROUND` plus the existing `Requested()` path.

### KE-4 [Low][Bug] `Convert/Detector.hpp:37-38`, `Autocorrect/Judge.cpp:42,107-111`
`Verdict` holds `string_view`s into `Pack::_locale`, which lives in `Runtime`. `Judge::Use` resets `_ready` but not `_kept`; a verdict swapped into `_kept` before settings close is logged by `_log` after `Runtime` is replaced and `Autocorrect.cpp:137` reads freed heap. Reasoning only, not reproduced; needs `LogDecisions`.
Fix: `char[16]` or `std::string` locale in `Verdict`, or clear `_kept` in `Use`.

### KE-5 [Med][Simplicity] `KeyboardPage.cpp:22-29,58-60,81-82,124,138-139`
A and B are duplicated by hand over 17 file-scope globals (`_packsA/B`, `_packTitlesA/B`, `_packItemsA/B`, `_defaultA/B`, `_shownPair*`, two `*Fn` getters); `_findPack` returns `size+1` for "missing"; two forward declarations exist only for ordering.
Fix: `struct PackSide` and `_sides[2]`, `_findPack` returns `optional<size_t>`: about -40 lines. Clear the vectors on Suspend.

### KE-6 [Med][Structure] `KeyboardExclusions.hpp:8,12-36`
Mixes CSV helpers (`Parse`/`Join`, used by settings and `Resolve`) with the input-thread `Filter`, which needs `Autocorrect::Runtime`. A root header includes `Autocorrect/Autocorrect.hpp` while `Autocorrect.cpp` and `WordTracker.cpp` include it back: root and `Autocorrect/` depend on each other. Settings reach `../Autocorrect/UserRules.hpp` by relative include.
Fix: `Filter` to `Autocorrect/AppFilter.hpp`; keep `Parse`/`Join` dependency-free.

### KE-7 [Med][DRY] `tools/.../Console.cpp:34-53,109-119`, `Main.cpp:46-57,66-86`
`Console::FromUtf8/ToUtf8` reimplement `Str::Utf8ToWide/WideToUtf8` (already linked through `Alphabet.cpp`); `ReadFileUtf8` reimplements `File::Read` and strips 3 bytes for any file starting with 0xEF where `Rules.cpp:86` checks the full BOM; `_loadInstalled` reimplements `InputLanguage::Installed`. The ISO 639 lookup (`GetLocaleInfoW(...LOCALE_SISO639LANGNAME...)`) has three copies: `KeyboardPacks.hpp:16`, `LayoutLayer.cpp:30-31`, `Main.cpp:46`.
Fix: `InputLanguage::Iso639(HKL)`, delete the `Console` copies: about -35 lines.

### KE-8 [Low-Med][Simplicity] `Convert/Detector.cpp:85-176`, `Detector.hpp:41`
`Detect` is 90 lines with three by-reference lambdas; `blocked()` assigns `v.Implausible` inside a condition (119-121) and runs `_plausible` twice on a rejected rule (174); line 125 mixes `&&` and `||` without parentheses; `Reason()` is a six-level ternary over five flags; `kNgramMinLetters` breaks the PascalCase used elsewhere.
Fix: `_byDictionary`, `_byRule`, `_byNgram` with early returns; one `enum Reason`.

### KE-9 [Low][DRY] `ExcludedApps.cpp:21`, `RuleDialog.cpp:20`, `UI/Settings/ActionDialog.cpp:34`
Window-text getter in three copies; panel class registration in `ExcludedApps.cpp:95-101`, `RulesPage.cpp:97-103`, `Desktops/Page.cpp:272`; `RulesPage` enables Edit/Remove in three places.
Fix: `Controls::Text(HWND)`, `Controls::RegisterPanel(name, proc)`, `_syncButtons(panel)` (see UP-13).

### KE-10 [Low][Perf] `Convert/Pack.cpp:12-24,131,147`, `Bloom.hpp:10,28`, `Detector.cpp:56,112`
`TrimWord` returns a `std::wstring` copy of a substring; `Score`, `Unseen`, `Fnv1a` take `const std::wstring&`, forcing copies (`Alphabet::Encode` already takes a view). Use `wstring_view`. MSVC SSO holds 7 wide characters: real saving from 8 letters up. Off the input thread.

### KE-11 [Low][Simplicity] `tools/.../Main.cpp:106-121,186-203,248-375,379-399`, `PackBuilder.cpp:45,119,153`
`layouts` and `lookup` subcommands are unused by any script or doc; `_runRepl` is 128 lines; `main` is a five-way `args[0] ==` chain; `_runRules` hard-codes `i == 3`; `_forEachWord` (a `std::function`) runs twice, parsing the text twice; Punto's `ps.dat` decode exists in `_runRules` and `punto/Sim.ps1`.
Fix: delete the two subcommands, parse once into a vector, command table: about -50 lines.

### KE-12 [Low][Structure] Keyboard root, `Keyboard.cpp:59`
12 files, four concerns (see the folder proposals in the README). `Make` sets `_needed` as a side effect and only works because `Bindings::Apply` runs before `Lifecycle::Restore` (`MainWindow.cpp:308-312`).

### KE-13 [Low][Perf] `InputLanguage.cpp:48-52,84-117`
Every layout-switch hotkey press calls `Installed()`: registry open, `RegQueryValueExW` and `LoadKeyboardLayoutW` per Preload entry, discarded `std::string` ids. Action worker, not the hook. Cache per `Restore`; layouts added mid-session appear after settings close.

### KE-14 [Low][Test]
No test of the `Pack::Load` rejections (zero bloom, oversized bloom, truncated file), `ValidPackFilename` (the path-traversal guard) or `KeyboardExclusions::Parse`. `KeyboardUiTest.cpp:122-125` passes when packs are absent and hard-codes control ids 1016, 1032, 1064, 1080 derived from `FirstId + index * IdsPerRow`. `KeyboardConfigTest.cpp:44-47` repeats line 40.

### KE-15 [Low][Readability] `KeyboardPacks.hpp:29-42`
`ValidPackFilename` guards `size<6`, `dot==npos`, `stem.empty()` that follow from `ends_with(L".pack")` plus a charset check; magic `100`, `-20.0` (`Pack.cpp:136`), threshold 0.35 in `Pack.hpp` and `PackBuilder.hpp`, alphabet cutoffs `10` and `100000`. Brace-less one-line `if`s in three files.

### KE-16 [Low][DRY] `Convert/Bloom.hpp:11`, `Rules.cpp:18`, `Pack.cpp:64` (checked)
`Fnv1a` basis `1469598103934665603` is one digit short of the FNV-1a basis `14695981039346656037` used in `Rules.cpp:18`; harmless, the step loop is written twice. Fixing it changes every hash, so do it at the next `PackVersion` bump. `Pack::_file` stays open though the mapping holds the file. Dev-only members in shipped classes: `Alphabet::Coverage`, `Alphabet::Empty` (unused), `Pack::MappedBytes`, `Pack::WordCount`; `PackHeader` has `Reserved` and a no-op `#pragma pack(8)`.

### KE-17 [Low][Perf] `Detector.cpp:88-109`, `Judge.cpp:73-77`
`Detect` renders and scores both sides (four allocations plus a `log` per trigram) before the cheap guards for leading `-`, digits, `letters == 0`; `Early` repeats the same `Render` and `Encode` per key. Off the hook. Move the guards above `Score`; share rendered texts between `Detect` and `Early`.

## Keep

- Sorted-hash `Rules` with incremental hash, no copies, early return on the `!open` whole-word case.
- `Pack` as one read-only mapping with shared `ComputeSections`/`PackHeader` for reader and builder; `Begins` before the expensive work in `Early`.
- `LayoutTable` keeps key positions, no reverse map; `Filter::Allowed` is O(1) after the first key per window.
