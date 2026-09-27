# Layout conversion and autocorrect

How the keyboard slice converts and autocorrects words, what Punto Switcher does, and the living plan
(bottom). The input pipeline under it (`Core/Input`, hold, edits) is `docs/INPUT.md`; the short file map
is `src/Features/Keyboard/README.md`. Earlier steps and their measurements are in git history (this file
before 2026-09-27).

## How it works now

- Stage `kbd.text`, order 300, on the input thread. A buffer of 64 `{vk, Shift, Caps}`, the spaces after
  the word (up to 8), the foreground window at word start and `Word.Layout`, the layout the word is on
  screen in: null until a Space or a conversion pins it, then read from the focused window. `OnKey` does
  no allocation and no table lookup, and always returns `Next`.
- Modifiers and Caps come from the stage's own stream (Caps seeded on enable). Backspace pops a space,
  then a key. Space ends the word but keeps it. The word clears on focus change, a mouse button, Enter,
  Tab, Esc, navigation keys, a Ctrl/Alt/Win chord and any key outside the typing set.
- Autocorrect: every key that changes the word wakes the judge (`Judge.*`, threadpool), which runs
  `Autocorrect::Decide` -> `Convert::Detect` for "the word ends here" and posts the verdict back. Space
  with a ready keep goes through unheld. A fix, or no verdict yet: `Input::Edit` (holds delivery) -> the
  lane checks for a password field (and decides) -> `Input::Post(apply)`. Keys typed during the hold are
  the next word.
- `Detect`, in order: rules (Punto's, below) > dictionary > ngram. A token with no letters (`1.`, `...`)
  is never converted. A word of 2 letters or less, or one both dictionaries know, is kept (`ambiguous`);
  if both know it and it has 2+ letters it waits in the run. The ngram decides only single words of 5+
  letters (3+ trigrams), against the pack's threshold.
- Rule guards: an `E` rule only cancels a rule; a rule never flips a word the dictionary knows (typed is a
  hit, the conversion a miss); a word with punctuation, or an anywhere rule, also needs the dictionary or
  the ngram to agree. Rules match the word rendered through the pair's tables (= key positions for US/RU)
  and are skipped when both sides share a script.
- The run (`TypedWord::Join`): an `Undecided` word moves into `_run` when the next word starts after 1-7
  spaces; any other clear drops it. A sure fix of the next word (dictionary, rule, learned) erases and
  retypes both; an ngram fix is a guess and does not carry it. The phrase becomes the current word, so
  converting again or undo acts on all of it.
- `apply` drops unless the hold id, focus and foreground are unchanged and every key renders in both
  layouts. The edit is Backspace x (keys + spaces), then the target rendering and the spaces as
  `KEYEVENTF_UNICODE`, wrapped in ups and downs of the held modifiers (a vkE8 tap first when Alt or Win
  is down). The layout switch is posted before `Commit`; a refused `Commit` switches back.
- The pair is two installed layouts bound by KLID. Any other installed layout types for the pair side of
  its script (uk -> ru side, fr -> en side); a conversion goes to the layout last typed in on the other
  side (`WordTracker::_last`), else the pair's. `InputLanguage::Installed()` lists Preload, then the
  session layouts Preload lacks (their id is the HKL in hex).
- Learning (`Learning.*`): `Learned.Always` / `Learned.Never` in the config, lowercase, ASCII punctuation
  trimmed, as typed on the side typed on. The user's words overrule `Detect` (reason `user`). Signals:
  convert or undo right after an auto-fix teaches never; converting a kept word teaches always; a judged
  word erased whole and retyped from the other side teaches the matching one. A phrase teaches nothing.
- `kbd.convert_word` converts the last word (again to convert back); `kbd.undo_auto_convert` undoes only
  the last auto-fix before the word changes and returns to the original layout. Exclusions suppress
  both. Enter and Tab clear the word (the line is usually sent by then).
- Guards: excluded apps, fullscreen windows (`SkipFullscreen`: rect equals its monitor's and not
  maximized, judged when the window takes the foreground) and unresolved foreground apps are not
  tracked. A fix asks `Accessibility::IsPassword` on the lane (`SkipPasswords`: MSAA `WM_GETOBJECT`, 50 ms
  timeout, the focused element's `STATE_SYSTEM_PROTECTED`, or `ES_PASSWORD` on a plain edit).
- Feedback: an auto-fix plays `FixSound` and, with `FixNotification`, posts `typed → fixed`. A hotkey edit
  announces only when `Commit` took a non-empty edit.
- `LogDecisions`: every verdict (word, reason, rule, margin), what was learned, why a Space was skipped,
  and how long each fix took from its Space until it landed or was dropped - off the input thread.
- Config `KeyboardSettings`: `PairA`/`PairB`, `PackA`/`PackB` (empty = `<locale>.pack`), `AutoCorrect`,
  `Exclude`, `Threshold` (hundredths, 0 = pack), `LogDecisions`, `Learned`, `SkipPasswords`,
  `SkipFullscreen`, `FixSound`, `FixSoundVolume`, `FixNotification`.

## Punto Switcher, reverse engineered

Personal build, never distributed: Punto's data may be used here, never in a public release.

### Files

- `PuntoSwitcherSetup.exe /extract <dir>` gives `PuntoSwitcher.msi`; `msiexec /a PuntoSwitcher.msi /qn
  TARGETDIR=<dir>` lays the files out without installing. Local copy: `cmake-build-tools/punto-extract/`
  (not in git).
- `punto.exe` (x86 MFC, the engine), `pshook.dll`/`pshook64.dll` (the hooks), `ps64ldr.exe`, `layouts.exe`
  (maps other layouts onto RU/EN), `Data\ps.dat`, `Data\triggers.dat`, `Data\translit-*.dat`,
  `Data\default-conf.json`.
- Tools: rizin (`aaa` on punto.exe takes ~30 s; save a project with `Ps`). Registered-message handlers are
  found through the MFC message map: search the bytes of the message id variable's address, the entry's
  `pfn` follows it.

### How Punto switches (traced)

- `pshook64.dll` installs `WH_KEYBOARD_LL` (proc `0x180002100`). Per key down it raises its process to
  `HIGH_PRIORITY_CLASS` and the thread to `TIME_CRITICAL`, then calls
  `SendMessageTimeoutW(punto window, WM_LC_KEYPRESS_INFO, focus, vk|shift|caps|scan|injected)`,
  timeout from shared memory (default 2000 ms). A non-zero answer eats the key.
- `punto.exe` handles it synchronously (`0x438530` -> engine `0x41b650`): the key joins the buffer, the
  rules run, and when the word must switch the answer is 1. The key that completed the rule never
  reaches the app: Punto erases what was typed, switches the layout and retypes, that key included.
  So `yfd` becomes `нав` on the 3rd key and the rest of the word is typed natively.
- The retype is virtual keys, not Unicode: `VK_BACK` x n in one `SendInput`, then one `SendInput` per
  key, all tagged `0x87654321` in `dwExtraInfo`, which the hook skips.
- Navigation, Esc, F-keys, Ctrl+V/X/Z/Y/A send `WM_LC_CLEARBUF` (the buffer resets), as ours clears.
- A branch for `Chrome_RenderWidgetHostHWND` sleeps 200 ms inside the handler (not traced further).
- No language history was found on the decision path: a word or its prefix decides.

### `ps.dat`: the rules

- XOR 0xAA on every byte except CR and LF, then CP1251 lines; first line `PSVersion=20170627`, then
  37,747 rules. `langpack rules <out> <ps.dat> [triggers.dat]` writes `packs/punto.rules` (UTF-8,
  `[_FLAGS ]pattern`); the exe never reads Punto's format.
- The pattern is written in the characters of the layout it was typed in (in effect key positions): a
  Cyrillic pattern was typed in RU and meant English, a Latin one the reverse.

  | Flag | Meaning | Examples |
  |---|---|---|
  | none, `A` | anywhere in the word | `yfd` нав, `ukz` гля, `rfr` как |
  | `B` | at the word begin | `vj` мо, `ghb` при, `ofc` щас |
  | `P` | the whole word | `ye` ну, `yf` на, `z` я |
  | `E` | exception: never switch | `EP chkdsk`, `EP мб`, `BE ;atvia` |
  | `C` | case-sensitive | `CP NE`, `EC CCTV` |
  | `D` | only as `_PD a..z`; skipped | |

- A space at a pattern's edge anchors it to the word edge. `triggers.dat` (plain CP1251, 4513 lines) is
  force-switch fragments, mostly code and HTML typed in the wrong layout; it loads as begin rules.
- `default-conf.json` per app: `use_paste` (edit through the clipboard: Telegram, WhatsApp, Viber),
  `unhook` (games), `use_hotkey_switching` (switch by emulating the layout hotkey).
- Measured on the corpus of the user's messages: Punto's rules fix 56% of wrong-layout words before the
  word ends, at 70% of the word on average; false fixes on correct text: top 1000 English words 0, top
  25k 1.1%, Russian forms 0.5%.

## Decisions (fixed)

- The engine lives in `Features/Keyboard/Convert/`; `tools/langpack` (packs, rules import, console) is a
  separate target, never linked into the exe. Packs are external files in `packs/` found by the layout's
  language.
- The buffer holds key positions, never characters; converting renders the same positions in another
  table.
- Priority: learned words > rules > dictionary > ngram. Rule sources: Punto's as the base.
- No language context (removed 2026-09-27). In the app its preference could only be in [-1, 0]: it held
  the languages of words typed in the same layout and restarted on a layout change, so it could brake a
  fix and never help one. It blocked real fixes (`путырше` -> `genshit`) and the run mid-text, and on
  6000 dictionary forms and 188 out-of-vocabulary words it prevented no false fix.
- The next word decides a word of both languages (the run).
- Learned words live in the config, edited on the settings page under the same Apply/Cancel.

## Plan: smooth switching

Punto feels smoother, mostly in browsers. Why ours does not (2026-09-27):

1. The fix comes only on Space: the whole word and its space are erased and retyped. Punto fixes on the
   2nd-4th key, then the rest of the word is typed natively.
2. Every Space holds delivery until the lane answers, even when the answer is "keep".
3. The password check on a fix sends `WM_GETOBJECT` to the focused window. Chromium may switch its
   accessibility tree on for it, and a lane slower than the 150 ms hold makes `Commit` refuse: the fix
   is dropped silently. Unconfirmed; step 1's log measures it.

### Step 1 - remove the context, log fix timing

- [x] `LanguageContext`, `FeedContext`, `UseContext` and its settings row removed
- [x] `LogDecisions` logs `fix landed|dropped after N ms`
- [ ] A day of browser typing with the log on: are fixes dropped, and how slow is the lane in Chromium?
- **Result:** corpus and random forms unchanged (6/1 false, 2991/2999 fixed of 3000); `путырше` now
  becomes `genshit`; the run works mid-text. Build 1222 KB.

### Step 2 - judge while typing, Space without a hold

Detection on the input thread was rejected in review: a page fault in the mapped pack would stall all
desktop input.

- [x] `OnKey`, after a key changes the word, bumps its generation and copies the word into the judge's
  slot (`Judge::Typed`, SRW lock, no allocation); a pending flag coalesces a burst of keys into one run
  of a pre-created `PTP_WORK` on the latest word.
- [x] The judge runs `Decide` for "the word ends here" and posts `{generation, layout, verdict}`; an older
  run never replaces a newer one. `Learning::Current()` became an atomic `shared_ptr` for it.
- [x] Space with a ready keep of this generation and layout: no hold, `Judged` from the verdict (the run
  still works). A ready fix holds only for the password check and the edit. No verdict yet -> as before.
- [ ] The password check stays on the fix's hold: a browser has one focus window for every field, so a
  verdict cached per window could miss a password field. Step 1's log decides whether it must move.
- **Result:** a kept word's Space is no longer held. Build 1228 KB.

### Step 3 - switch mid-word

- The judge also answers "switch now?" for the prefix typed so far:
  - Punto's `B` and anywhere rules with their `E` exceptions (the rule guards still apply);
  - our own, working without Punto's data: no word of the source language starts with the prefix, while
    words of the target language do. The packs gain a prefix bloom (`langpack pack` inserts every prefix
    under its own key; the pack format version goes up). A typo (`тьак`) does not switch: `nmf` starts no
    English word.
  - at least 2 keys for a rule, 3 for the prefix test.
- On "switch now" for a word whose keys still start with the judged prefix, the input thread starts an
  edit: erase every key of the word on screen (more may have come since), type the target rendering as
  Unicode, switch the layout; keys held meanwhile replay in the new layout. The word is `Fixed`, pinned
  to the target layout; the rest is typed natively and judged again on Space.
- Undo: `kbd.undo_auto_convert` after a mid-word fix returns the whole word to the original layout.
- Measure: share of wrong-layout words fixed before their end and at which key, false mid-word switches
  on the corpus and on top 25k English / Russian forms, pack size growth.
- Known cost: a browser's inline autocomplete (address bar) takes the first Backspace for its selection.

### Step 4 - real-text bench

- [ ] `langpack` replays the decision log and what followed each decision (undo, convert, erase-retype) as
  the corpus; mixed text: words typed in the current layout, a fix or a miss moves the layout.

## Carried over, still open

- Conversion in Chromium, an Electron app and Windows Terminal verified live; an elevated window
  without SkipUac is left alone.
- `GetKeyState(VK_CAPITAL)` accuracy on the input thread; the hold timeout and the input thread
  priority - measure.
- The tracker staying off while a CJK IME is active: not built.
- ru pack (danakt) has no colloquial forms (`щас`, `чё`, `ща`): add a frequency or colloquial list.
- Not now: Punto's per-app edit modes (clipboard for Telegram, games unhooked), converting a selection,
  transliteration, downloading packs.

## Known limits (accepted)

- A Backspace replacement cannot prove the caret is still where the word ended (autocomplete popups,
  editors that reformat).
- Non-elevated hooks see nothing in elevated windows, and `SendInput` into them fails silently (UIPI).
- An app lagging behind its input queue reads the layout switch before keys typed ahead of the
  conversion, so those come out in the new layout.
- Same-script pairs (EN/DE, EN/PL) are out of reach, AltGr characters are not in the tables, and Punto's
  Latin rules assume US key positions (AZERTY, Dvorak match the wrong keys).
- A layout switched with the mouse between erasing a word and typing it again is not a retype.
- A password field without MSAA state is not detected; the exclusion list is the answer.
- Fullscreen is judged when a window takes the foreground; F11 browsers count as fullscreen.
- The run holds only text typed straight through; 8 spaces or more drop it.
