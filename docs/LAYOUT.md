# Layout conversion and autocorrect

How the keyboard slice converts and autocorrects words, what Punto Switcher does, and the living plan
(bottom). The input pipeline under it (`Core/Input`, hold, edits) is `docs/INPUT.md`; the short file map
is `src/Features/Keyboard/README.md`. Earlier steps and their measurements are in git history (this file
before 2026-09-27).

## How it works now

- Stage `kbd.text`, order 300, on the input thread. A buffer of 64 `{vk, Shift, Caps}`, the spaces after
  the word (up to 8), the foreground window at word start and `Word.Layout`, the layout the word is on
  screen in: null until a Space or a conversion pins it, then read from the focused window. `OnKey`
  allocates only to hand a word off (an edit, a result), does no table lookup, and always returns `Next`.
- Modifiers and Caps come from the stage's own stream (Caps seeded on enable). Backspace pops a space,
  then a key. Space ends the word but keeps it. The word clears on focus change, a mouse button, Enter,
  Tab, Esc, navigation keys, a Ctrl/Alt/Win chord and any key outside the typing set.
- Autocorrect: every key that changes the word wakes the judge (`Judge.*`, threadpool), which runs
  `Autocorrect::Decide` -> `Convert::Detect` for "the word ends here" and posts the verdict back. Space
  with a ready keep goes through unheld. A fix, or no verdict yet: `Input::Edit` (holds delivery) -> the
  lane checks for a password field (and decides) -> `Input::Post(apply)`. Keys typed during the hold are
  the next word. A word already converted (pinned mid-word, or re-entered after its Space) is left alone.
- Mid-word (`AutoCorrect = MidWord`): the judge also runs `Autocorrect::Early` -> `Convert::Early` on the
  word so far and calls `WordTracker::_onEarly` when it switches; that version of the word takes the same
  hold and password check, then the word so far is erased and retyped in the other layout. Keys typed
  during the hold are its rest (spaces its spaces) and replay in the new layout. Once per word.
- `Early`: 2+ keys, letters on both sides (signs only as letters, a trailing sign waits a key), and the
  typed text's first 4 letters start no word of its language (`Pack::Begins`: the bloom also keeps every
  word start of 2-4 letters) - a word that leaves its language later is a typo. Then with frequency
  analysis: the converted start begins a word of the other language, its open-ended ngram wins by 1+,
  and 4+ keys or a Punto `B`/`A` rule (open: `P` and end-anchored patterns wait, a whole-word exception
  holds). Without it a rule alone decides. A learned Never word the text may still become, or a refused
  start, stops it.
- `Detect`, in order: rules (Punto's, below) > dictionary > ngram. A token with no letters (`1.`, `...`)
  is never converted. A word of 2 letters or less, or one both dictionaries know, is kept (`ambiguous`);
  if both know it and it has 2+ letters it waits in the run. The ngram decides only single words of 5+
  letters on both sides (3+ trigrams; `и.т.д` makes `b/n/l`, 3 letters), against the pack's threshold.
- Rule guards: an `E` rule only cancels a rule; a rule never flips a word the dictionary knows (typed is a
  hit, the conversion a miss); an anywhere rule also needs the ngram to agree, and a word with a sign the
  dictionary - unless the sign is a letter of the other side (`bv,f` = имба): it converts to a letter, the
  word ends in a letter, has 3+ letters and does not start with a lone letter (`e.g`, `:p`, `ofc.`, `ц.у`
  stay). Rules match the word rendered through the pair's tables (= key positions for US/RU) and are
  skipped when both sides share a script.
- `FrequencyAnalysis` off (for testing Punto and a dictionary): rules unguarded but for known words, then
  the dictionary, no ngram. Without rules that leaves the dictionary alone (7% of rare English fixed).
- The run (`TypedWord::Join`): an `Undecided` word moves into `_run` when the next word starts after 1-7
  spaces; any other clear drops it. A sure fix of the next word (dictionary, rule, learned) erases and
  retypes both; an ngram fix is a guess and does not carry it. The phrase becomes the current word, so
  converting again or undo acts on all of it.
- `apply` drops unless the hold id, focus and foreground are unchanged and every key renders in both
  layouts. The edit is Backspace x (keys + spaces), then the target rendering and the spaces as
  `KEYEVENTF_UNICODE`, wrapped in ups and downs of the held modifiers (a vkE8 tap first when Alt or Win
  is down). The layout switch is posted before `Commit`; a refused `Commit` switches back.
- The pair is two installed layouts bound by KLID. Any other installed layout of a side's script (uk -> ru
  side, fr -> en side) is never judged: the side's pack would read uk's і as ы and fix the word. It still
  converts by hand. A conversion always goes to the other side's pair layout: sent to the last one typed in, a
  fix left the user in uk, unjudged. `InputLanguage::Installed()` lists Preload, then the
  session layouts Preload lacks (their id is the HKL in hex).
- Learning (`Learning.*`): `Learned.Always` / `Learned.Never` in the config, lowercase, ASCII punctuation
  trimmed, as typed on the side typed on. The user's words overrule `Detect` (reason `user`). Signals:
  convert or undo right after an auto-fix teaches never; converting a kept word teaches always; a judged
  word erased whole and retyped from the other side teaches the matching one. A phrase teaches nothing.
  Overruling a mid-word fix refuses the start it fired on: `kube*` in Never stops only mid-word switches.
- `kbd.convert_word` converts the last word (again to convert back); `kbd.undo_auto_convert` undoes the
  last auto-fix and what was typed after it (letters, spaces, Backspace): that whole text goes back to the original
  layout, only the fixed word teaches. A caret move, a chord, a layout switch or the next fix ends it. Exclusions suppress
  both. Enter and Tab clear the word (the line is usually sent by then).
- Guards: excluded apps, fullscreen windows (`SkipFullscreen`: rect equals its monitor's and not
  maximized, judged when the window takes the foreground) and unresolved foreground apps are not
  tracked. A fix asks `Accessibility::IsPassword` on the lane (`SkipPasswords`: MSAA `WM_GETOBJECT`, 50 ms
  timeout, the focused element's `STATE_SYSTEM_PROTECTED`, or `ES_PASSWORD` on a plain edit).
- Feedback: an auto-fix plays `FixSound` and, with `FixNotification`, posts `typed → fixed`. A hotkey edit
  announces only when `Commit` took a non-empty edit.
- `LogDecisions`: every verdict (word, reason, rule, margin), what was learned, why a Space was skipped,
  and how long each fix took from its Space (or mid-word verdict, `FIX early`) until it landed or was
  dropped - off the input thread.
- Config `KeyboardSettings`: `PairA`/`PairB`, `PackA`/`PackB` (empty = `<locale>.pack`), `AutoCorrect`
  (`Off` / `Space` / `MidWord`),
  `Exclude`, `Threshold` (hundredths, 0 = pack), `FrequencyAnalysis`, `LogDecisions`, `Learned`, `SkipPasswords`,
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
- Punto's rules keep our guards (measured 2026-09-27; fixed / false switches). Without them (rules decide,
  the dictionary vetoes known words, no ngram) the slang comes back but rare English words break
  (`ashtray` -> `фырекфн`, 51 of 2000); a 100k English dictionary still leaves 47. Never flipping any known
  word drops real text to 92%. Only the sign guard was narrowed (a sign as a letter, above); a review
  caught `e.g`, `i'd`, `:p`, `@foo`, `ц.у` flipping under a looser first try. 60 such tokens stay put.

  | Set | Punto | ours | ours + rules | Punto + dictionary |
  |---|---|---|---|---|
  | real text, 325 | 98% / 3 | 91% / 0 | 98% / 0 | 99% / 2 |
  | ru random forms, 2000 | 100% / 9 | 100% / 0 | 100% / 0 | 100% / 0 |
  | en random words, 2000 | 98% / 51 | 98% / 2 | 100% / 6 | 98% / 51 |
  | en top 25k, 2000 | 97% / 15 | 99% / 0 | 99% / 5 | 99% / 5 |
  | en brands and terms, 108 | 96% / 7 | 88% / 2 | 97% / 2 | 95% / 7 |
  | ru slang, 80 | 93% / 2 | 64% / 2 | 83% / 3 (74% / 4 before) | 94% / 3 |
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

- [x] `Convert::Early` on the word so far (above); `Autocorrect::Early` adds the user's refusals.
- [x] Packs keep every word start of 2-4 letters in the bloom (format 2, old packs are rejected).
- [x] The judge calls `WordTracker::_onEarly` for a switch; the version it judged takes the Space fix's
  hold, password check and edit; keys typed during the hold are the word's rest; the word is pinned.
- [x] Undo and convert after a mid-word fix refuse its start (`kube*`); undo lasts through the word's rest.
- [x] `AutoCorrect` became `Off` / `Space` / `MidWord`, one combo on the page.
- [ ] Live: Chromium, an Electron app, Windows Terminal; typing fast through the hold.
- Known cost: a browser's inline autocomplete (address bar) takes the first Backspace for its selection.
- **Result** (langpack on the simulation's sets; mid-word fixed of wrong-layout words, at share of the word
  / false switches of words typed right). Build 1239 KB; en.pack +28 KB, ru.pack +51 KB.

  | Set | ours | ours + rules | Punto + dictionary |
  |---|---|---|---|
  | ru real text, 268 | 54% at 65% / 0 | 74% at 62% / 0 | 80% at 63% / 0 |
  | ru forms, 2000 | 93% at 41% / 0 | 93% at 33% / 0 | 95% at 32% / 0 |
  | ru slang, 80 | 51% / 0 | 68% / 0 | 88% / 0 |
  | ru forms with a typo, 2000 | 70% / 1 | 79% / 26 | 94% / 214 |
  | en top 25k, 2000 | 93% at 58% / 0 | 97% at 42% / 0 | 97% at 43% / 0 |
  | en rare words, 2000 | 73% / 1 | 90% / 2 | 97% / 29 |
  | en brands and terms, 108 | 53% / 0 | 70% / 2 | 94% / 6 |
  | en words with a typo, 2000 | 55% / 3 | 77% / 22 | 92% / 321 |

  Tried in a simulation first: the start test from key 3 switched `kubernetes`, `jira`, `vscode` and 5%
  of typo'd English words; the ngram margin and the 4-letter window took that to 0 and 0.2%. Punto's
  rules with only the dictionary guard flip 11-16% of typo'd words mid-word, which is what Punto does.
  Word starts beyond 4 letters changed nothing.

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
