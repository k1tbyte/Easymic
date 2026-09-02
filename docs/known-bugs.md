# Known bugs

Defects that are real and understood but deliberately not fixed yet, with the reason. A entry
leaves this file either fixed or reclassified as intended behaviour - never by being forgotten.

Fixed defects are not recorded here; git history is the record for those.

---

## Chord release fires against the wrong mask

**Where:** `src/Core/HotkeyService.cpp`, `_onKeyRelease`
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

## A modifier alias is not printed back verbatim

**Where:** `src/Core/KeyNames.cpp`
**Severity:** cosmetic

`Parse` accepts `LCTRL`, `LSHIFT` and `LALT` as aliases of the unprefixed left-key names, but
`Format` only ever walks `ModifiersOrderedList`, so `Format(Parse("LCTRL"))` is `"CTRL"`.

The mask round-trip holds - `Parse(Format(m)) == m` for every valid mask - only the spelling is
canonicalised. It becomes visible in step 3, where the trigger is stored as a display name: a
config hand-edited to say `LCTRL + M` is rewritten as `CTRL + M` on the next Apply.

**Why it waits:** it is the intended behaviour of a canonical form. Recorded so that nobody
reports it twice.

---

## `.idea` run configuration points at the old target

**Where:** `.idea/workspace.xml`, `TARGET_NAME="Easymic"`
**Severity:** local only - the file is gitignored

The CMake target is `EasyLauncher` after the step 0 rename, so CLion's Run button resolves
nothing. `build.ps1` is unaffected.

**Fix:** recreate the run configuration by hand once. Nothing to commit.
