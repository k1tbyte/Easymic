# Known bugs

Defects that are real and understood but deliberately not fixed yet, with the reason. A entry
leaves this file either fixed or reclassified as intended behaviour - never by being forgotten.

Fixed defects are not recorded here; git history is the record for those.

---

## Chord release fires against the wrong mask

**Where:** `src/Core/Hotkeys/HotkeyService.cpp`, `_onKeyRelease`
**Severity:** minor - wrong action on an unusual release order

`_onKeyRelease` raises the action *before* it updates `_sequenceMask`, so a release is evaluated
against the combination that was still complete when the key went up. That is correct for the
last key of a chord and wrong for every key before it.

With `Ctrl+A` held and a release binding on `Ctrl+A`:

- release `A` first: fires against `Ctrl+A`. Correct.
- release `Ctrl` first: fires against `Ctrl+A` - also correct - and then `_sequenceMask` drops to
  `A`. Releasing `A` afterwards fires against `A`, so a release binding on plain `A` triggers
  even though the user never pressed `A` alone.

**Why it waits:** the fix is to remember which mask a key was pressed under and evaluate the
release against that, which means per-key state the hook proc has to write on every press. That
is the hot path, and the payoff is one release order almost nobody uses. Revisit when the
keyboard and mouse procs merge - the bookkeeping is cheaper to add once, in one place.

---

## A modifier alias is rewritten, not preserved

**Where:** `src/Core/Hotkeys/KeyNames.cpp`, `AppConfig::Load`
**Severity:** cosmetic, and deliberate

`Parse` accepts `LCTRL`, `LSHIFT` and `LALT` as aliases of the unprefixed left-key names, but
`Format` only ever walks `ModifiersOrderedList`, so `Format(Parse("LCTRL"))` is `"CTRL"`. A
`config.json` hand-edited to say `LCTRL + M` comes back as `CTRL + M`.

That rewriting is now done on load rather than left until the next Apply, because two spellings
of one combination is worse than one surprising spelling: `ClearHotkey` compares trigger names,
so `LCTRL + A` and `CTRL + A` would look like different combinations to the settings UI and the
same one to `RegisterHotkey`, and the second binding would quietly fail to register behind the
first.

**Why it stays:** it is what a canonical form is for. Recorded so nobody reports it twice.

---

## A hand-edited `Overlay.Size` of 0 leaves nothing to drag

**Where:** `src/Core/AppConfig.hpp`, `OverlaySettings::Size`
**Severity:** minor - only reachable by editing `config.json` by hand

The settings trackbar is bound to 10..32 and `Load` clamps nothing, so a `config.json` that says
`"Size": 0` gives `OverlaySlots::PillHeight(0) == 0`, every layer measures 0 wide, and the
overlay is hidden - including the preview the settings window relies on for drag-to-position. The
user then cannot reposition the overlay from the UI, only by editing the file again.

Before step 8 the same config produced a 0x0 window that was shown rather than hidden, so this is
the same defect wearing a different shape, not a regression.

**Why it waits:** `Size` is not special. `Volume`, `BellVolume`, `VolumeThreshold`,
`MultiPressWindowMs` and `PosX`/`PosY` are all equally unvalidated, and clamping one of them
because a review happened to name it would leave the other five. The whole set belongs to one
pass over `AppConfig::Load`, which is where this tree already canonicalises a hand-edited value.

---

## `.idea` run configuration points at the old target

**Where:** `.idea/workspace.xml`, `TARGET_NAME="Easymic"`
**Severity:** local only - the file is gitignored

The CMake target is `EasyLauncher` after the step 0 rename, so CLion's Run button resolves
nothing. `build.ps1` is unaffected.

**Fix:** recreate the run configuration by hand once. Nothing to commit.
