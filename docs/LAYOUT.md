# Layout conversion and autocorrect

Living plan: tick items as they land. Replaces the keyboard half of the old `docs/INPUT.md`
(steps 15-16) and `plan.md`. The input pipeline under it (`Core/Input`, hold, edits) stays in
`docs/INPUT.md`.

## Where it stands

Built: `kbd.convert_word` (convert the last word, again to convert back), autocorrect on Space
(learned words, Punto's rules, dictionary, context, ngram), the next word deciding a word of both
languages, learning from the user's corrections, en and ru packs, the `langpack` tool, opt-in
decision log. Step 1 stopped the two defects that made autocorrect get in the way, step 2 added
the rules and the run, step 3 the learning; step 4 measures it on real text.

## Diagnosis (2026-09-26 report)

Report: `ye vj;yj yfdthyjt` ("ну можно наверное" in the wrong layout) was not corrected, `щас`
became `ofc`. Two separate defects, both reproduced live.

1. **Silent outside the pair.** Installed: US, Russian, Ukrainian (Enhanced) in Preload, and a
   French layout the session has but Preload does not list. Alt+Shift cycles
   0419 -> 0422 -> 040C -> 0409. The pair fell back to the first two (en, ru); in any other layout
   `WordTracker::_side` returned -1 and autocorrect did nothing and logged nothing. Uk looks like
   ru and fr types the same letters, so the user could not see it. "Came back by itself" = the
   cycle reached en or ru again. Repro: `tests/AutocorrectDeathProbe.ps1`.
   - Not fully explained: the reported second line looks typed in US (`;`, `z`, `m`) and still
     had no log. Candidates: a French-language HKL with a US keyboard (low word 040C), or a stuck
     modifier bit in the tracker (every key clears the word until that modifier comes up again).
     The skip log added in step 1 names the cause next time.
2. **`щас` -> `ofc`.** Not in the ru list (danakt has literary forms only), a bloom miss on both
   sides, so the ngram branch decided on a single trigram: margin 1.69 over the threshold even at
   pref -1.00 (fully Russian context). The fix also switched the layout, so the next word came out
   Latin (`ltkfkb` -> `делали` in the 17:01 log): one false fix costs two edits.
3. **The benchmark hid it.** `tools/langpack/bench.ps1` samples random dictionary forms
   (`приверченною`, `генеральс`) and showed 0 errors. Real text is the measure now (below).

## Punto Switcher, reverse engineered

Personal build, never distributed: Punto's data may be used here. It is proprietary, so it never
goes into a public release.

### Getting the files without installing

- `PuntoSwitcherSetup.exe` (4.9 MB, July 2024) is a WiX Burn bundle (`wix\setupexe\...\setupexe.pdb`).
- `PuntoSwitcherSetup.exe /extract <dir>` gives `PuntoSwitcher.msi`;
  `msiexec /a PuntoSwitcher.msi /qn TARGETDIR=<dir>` lays the files out without installing
  anything. The bundled Yandex offer (`downloader.exe` and its inner MSI) never runs.
- Payload `Yandex\Punto Switcher\`: `punto.exe` (4 MB, x86 MSVC, embeds SQLite), `pshook.dll` and
  `pshook64.dll`, `ps64ldr.exe`, `layouts.exe` (maps other layouts onto RU/EN), `diary.*`,
  `Data\ps.dat`, `Data\triggers.dat`, `Data\translit-en.dat`, `Data\translit-ru.dat`,
  `Data\default-conf.json`, `Updater\*`.
- Local copy: `cmake-build-tools/punto-extract/` (not in git; rerun the two commands if it is gone).
- Tools: rizin (`rz-bin -I/-S/-z`, `axt`). Addresses below are from static analysis.

### `ps.dat`: the rules

- XOR 0xAA on every byte except CR and LF, then CP1251 lines (decode loop at `0x479e30`, CR/LF
  tests at `0x479e97`/`0x479eb1`; loader `FUN_0041ea80`, called from `fcn.0041ef90`,
  `CLayoutManager::InitializeLayouts`). First line `PSVersion=20170627`, then 37,747 rules.
- Not a word list. Each line is `[_FLAGS ]pattern`, the pattern written in the characters of the
  layout it was typed in (RU or US) - in effect key positions. A Cyrillic pattern was typed in RU
  and meant English; a Latin one was typed in US and meant Russian.
- Flags (inferred from the data and checked by the emulator; the matcher itself is not traced):

  | Flag | Meaning | Examples |
  |---|---|---|
  | none, `A` | anywhere in the word | `yfd` нав, `ukz` гля, `rfr` как, `ytn` нет |
  | `B` | at the word begin | `vj` мо, `ghb` при, `ofc` щас |
  | `P` | the whole word | `ye` ну, `yf` на, `z` я |
  | `E` | exception: never switch | `EP chkdsk`, `EP abbyy`, `EP мб`, `BE ;atvia` |
  | `C` | case-sensitive | `CP NE`, `EC CCTV` |
  | `D` | only as `_PD a..z` (26); unknown, treated as off | |

- Counts: `_B` 8556, untagged 14,498 (8127 Cyrillic, 6119 Latin, 252 led by punctuation), `_BE`
  3903, `_P` 2295, `_EPC` 1276, `_ECP` 956, `_C` 924, `_BC` 897, `_PEC` 809, the rest under 600.
  By class: en anywhere 6451 / begin 3672 / whole 1832, ru 8987 / 6059 / 1592; exceptions en
  334 / 2201 / 2142, ru 312 / 2407 / 1732.
- A pattern may carry a space at its edge, anchoring it to the word edge.
- **Mid-word switching** is anywhere and begin rules checked on every key: the switch happens the
  moment `yfd` is typed. **uk,ru -> en works** because the rules are key positions, and
  `layouts.exe` maps other layouts onto them (inference).

### Other data

- `triggers.dat`: plain CP1251, header `dc§`, 4513 lines, loaded by `FUN_0041ede0` into a vector.
  Force-switch fragments, mostly code and HTML typed in the wrong layout: `Бешеду` `<title`,
  `Эрее` `"htt`, `Б!В` `<!D`, `[jhbc` хорош, digit-led `1ем` `1tv`, `4ыр` `4sh`.
- `translit-*.dat`: name transliteration tables, plain CP1251 (`Abel=Эйбел`).
- `default-conf.json`, per app: `use_paste` (icq, lingvo, telegram, viber, whatsapp - the edit
  goes through the clipboard, not keystrokes), `unhook` (games), `deactivate_menu`,
  `use_special_copy_paste` (atom with a 100 ms delay), `use_hotkey_switching` per Windows version
  (devenv, skype, outlook, WINWORD with a 30 ms delay), `hook.patch_layout_funcs`.
- Learning and exclusions: `user.dic` (loaded near `0x41f06a`), `ProgramsExceptions`,
  `FolderExceptions`, `TitlesExceptions` (`prog_ex.dat`, `folders.dat`, `titles.dat`),
  `ShareHotKeyForUndoConvertAndSelectionConvert`, `DetectPasswords`, and a dialog that offers a
  rule "for %s" after %d undos in a row.
- Shape: `pshook64.dll` installs `WH_KEYBOARD_LL` (`SetHook` at `0x180003640`), keeps 104 bytes of
  shared memory `PS_SHARED_DATA_32_64`, posts keys to `punto.exe`, which decides and retypes
  (Backspace, replay, `WM_INPUTLANGCHANGEREQUEST`). Our design has the same shape, plus the hold,
  which keeps fast typing from racing the edit.

### Measured: Punto's rules against ours

`tools/punto/Sim.ps1 -PsDat <ps.dat> [-BenchDir <dir>]` emulates the flags on whole words.
Corpus `tools/punto/corpus.txt`: 325 tokens of the user's real messages and decision log, each
typed in its own layout; the wrong-layout set is the same tokens through the RU/US key map. Our
engine: `langpack --packs <dir> --pair en,ru < tokens`.

| | Punto rules | ours before | ours after step 1 |
|---|---|---|---|
| False fixes of correct text | 3 (`ofc.`, `uk,ru`, `en.`) | 5 (`щас` x4, `en.`) | 1 (`en.`) |
| Wrong-layout words fixed | 317 (97.5%) | 321 (98.8%) | 320 (98.5%) |
| Fixed before the word ends | 56%, at 70% of the word on average | 0 | 0 |
| Random forms, false en / ru | 0.8% / 0.6% | 0 / 0 | 0 / 0 |
| Random forms, fixed ru-in-en / en-in-ru | 99.7% / 97.1% | 100% / 100% | 100% / 100% |

- Punto on correct English: top 1000 words 0 false, top 25k 1.1%; ru forms (every 50th of 1.5M)
  0.5%. A rule firing mid-word on a word an exception covers only at its end: 44 of 25k en, 87 of
  30k ru.
- Punto gets `щас` right (no rule for it) and `ye`, `vj;yj`, `yfdthyjt`, `ukzyenm` all mid-word
  (`vj` on the 2nd key, `yfd` on the 3rd).
- Ours without ngram at all: 1 false, 304 fixed. Ngram adds 17 real fixes (`punto`, `rizin`,
  `отреверсить`, `перепройду`, margins 1.33-3.82); all but one have 5+ letters.

## Decisions (fixed)

- Rules are checked on Space, on the edit lane - no table lookups in the hook, so the
  `docs/INPUT.md` contract holds. Mid-word switching waits for data (step 5).
- A layout outside the pair types for the pair side of its script (uk -> ru side, fr -> en side).
  Detection reads the positions through the pair's tables, the languages the packs speak.
- The conversion goes to the layout last typed in on the other side (user's choice: the last
  Cyrillic one), else the pair's.
- Ngram decides only single words of 5+ letters (3+ trigrams).
- Learning: a word learned from the first correction. The user's words outrank everything.
- Priority: learned words > rules > dictionary > context > ngram, measured in step 2. An exception
  only cancels a rule. A rule never flips a known word (typed is a hit, the conversion a miss);
  a word with punctuation, or a mid-word pattern, also needs the dictionary or the ngram to agree.
- Rules match the word rendered through the pair's tables as text, which equals key positions for
  a US/RU pair; skipped when both sides share a script.
- The next word decides: a word of both languages the context left alone waits in the run, and a
  fix of the next word by dictionary or rule converts the run with it in one edit. Context and
  ngram fixes never carry it: they are guesses.
- Rule sources: Punto's as the base; ours generated from the packs wait for step 5.
- Learned words live in the config (`KeyboardSettings::Learned`), not `rules.user.txt`: the
  settings page edits them under the same Apply/Cancel, and glaze saves them.

Kept from step 15:

- The puntish engine lives in `Features/Keyboard/Convert/` and nowhere else; the pack builder and
  console are `tools/langpack/`, a separate target not linked into the exe.
- The pair is two installed layouts bound by KLID, so two layouts of one language work. Packs are
  external files found by the layout's language in `packs/` (ru is 2.9 MB), not resources.
- The buffer holds key positions (vk, Shift, Caps), never characters; converting renders the same
  positions in another table.
- Undo by converting again: `kbd.convert_word` pressed with nothing typed in between converts
  back, including an autocorrection. `kbd.undo_auto_convert` is separate: it only undoes the last
  successful automatic fix before the word changes, and returns to the original layout.
- Autocorrect is off by default and fires on Space only; Enter and Tab clear the word, since the
  line is usually sent by then.
- Case folding through `CharLowerBuffW`; switching through `InputLanguage`, which targets the
  focused window, not the foreground one.

## How it works now

- Stage `kbd.text`, order 300, on the input thread. A buffer of 64 `{vk, Shift, Caps}`, the spaces
  after the word (up to 8), the foreground window at word start and `Word.Layout`, the layout the
  word is on screen in: null until a Space or a conversion pins it, then read from the focused
  window. `OnKey` does no allocation and no table lookup, and always returns `Next`.
- Modifiers and Caps come from the stage's own stream (Caps seeded on enable). Backspace pops a
  space, then a key. Space ends the word but keeps it. The word clears on focus change, a mouse
  button, Enter, Tab, Esc, navigation keys, a Ctrl/Alt/Win chord and any key outside the typing
  set.
- Autocorrect: Space -> `Input::Edit` -> the lane runs `Autocorrect::Decide` -> `Convert::Detect`
  (rules, dictionary, then context for short words and ties, then ngram for 5+ letters) ->
  `Input::Post(apply)`. `OnHold` moves the word into the edit slot; keys typed during the hold
  are the next word. A token with no letters (`1.`, `...`) is never converted: `1.` became `1ю`,
  `ю` being in the Russian dictionary.
- Rules: every `packs/*.rules` loads on `Restore` with the packs into `Convert::Rules`, sorted
  64-bit hashes (Punto's 40,625 rules, 325 KB), read only on the lane. `langpack rules` writes
  `packs/punto.rules` from Punto's `ps.dat` and `triggers.dat`; the exe never reads their format.
- The run (`TypedWord::Join`): a word kept as `Undecided` moves into `_run` when the next word
  starts after 1-7 spaces; any other clear drops it (focus, navigation, a mouse button, Backspace
  past the word). A sure fix of the next word erases and retypes both, and the phrase becomes the
  current word, so `kbd.convert_word` and `kbd.undo_auto_convert` act on all of it.
- `apply` drops unless the hold id, focus and foreground are unchanged and every key renders in
  both layouts. The edit is Backspace x (keys + spaces), then the target rendering and the spaces
  as `KEYEVENTF_UNICODE`, wrapped in ups and downs of the held modifiers (a vkE8 tap first when
  Alt or Win is down). The layout switch is posted before `Commit`; a refused `Commit` switches
  back. After it the converted word is the current one, so converting again goes back.
- `WordTracker` keeps `_last[side]`, the layout each side was last typed in, and a table for
  every installed layout: `InputLanguage::Installed()` lists Preload, then the session layouts
  Preload lacks.
- The context (`LanguageContext`, 6 entries) clears on focus change and when a word is typed in
  another language's layout than the last (`Typing`): a switch outranks the words before it. A fix
  or a conversion restarts it holding the fix's language (`Switched`). Found on 2026-09-27: `from
  my perspective its та не` flipped `не` to `yt` by the English words, and `Clear` left the write
  slot mid-ring, so a cleared context read stale entries. Excluded, fullscreen
  (`SkipFullscreen`, on by default) or unresolved foreground apps are not tracked while auto, an
  exclusion or `SkipFullscreen` is on. `Foreground` judges fullscreen off the input thread when a
  window takes the foreground: its rect equals its monitor's and it is not maximized (with an
  auto-hidden taskbar a maximized editor fills the monitor too), the desktop aside.
- Config `KeyboardSettings`: `PairA`/`PairB` (KLIDs, a session-only layout's HKL in hex, empty =
  first and second installed),
  `PackA`/`PackB` (filenames, empty = the layout's `<locale>.pack`), `AutoCorrect`, `Exclude`,
  `Threshold` (hundredths, 0 = pack), `UseContext`, `LogDecisions`, `Learned`, `SkipPasswords`,
  `SkipFullscreen`, `FixSound`, `FixSoundVolume`, `FixNotification`. The Keyboard page puts each layout and its pack
  on one line (`SettingsRow::Beside`), the checks and sliders next, the exclusion and learned lists
  last; `Exclude` remains a comma-separated config field and suppresses manual conversion too.
  Packs load on `Lifecycle::Restore` from `packs/` next to the exe and unload on `Suspend`.
- Feedback: a hotkey edit plays its binding's sound and notification only when `Commit` took a
  non-empty edit (`Input::Edit`'s `landed`). An auto-fix plays `FixSound` and, with
  `FixNotification`, posts `typed → fixed`.
- Password fields (`SkipPasswords`, on by default): only a verdict that would fix asks
  `Accessibility::IsPassword` on the lane - MSAA `WM_GETOBJECT` with a 50 ms timeout, the focused
  element's `STATE_SYSTEM_PROTECTED`, or `ES_PASSWORD` on a plain edit. A password field keeps the
  word unjudged and the log omits it. `kbd.convert_word` is not guarded: the user asked for it.
- Learning (`Learning.*`): the config holds `Learned.Always` and `Learned.Never`, entries
  lowercase with ASCII punctuation trimmed (`Convert::WordKey`), as typed through the pair's table
  of the side typed on. The input thread holds a `shared_ptr<const LearnedWords>` snapshot, each
  Space's lane closure captures it, and `Autocorrect::Decide` lets it overrule `Detect` (reason
  `user`). A signal renders the word on the input thread and `Dispatcher::ToUi`s it; the UI thread
  files it, saves the config and posts a new snapshot. While settings holds the config
  (Suspend..Restore) a late one waits and is filed on Restore. The Keyboard page lists the words
  with Always, Never and Remove.
- `Word.Judged` (`TypedWord::Judgement`): what autocorrect made of the word on its Space - Kept,
  Undecided (joins the run), Fixed - cleared by an edit or a manual convert. Only a judged word
  teaches.
- `LogDecisions` logs every verdict (word, reason, margin, preference), what was learned and why a
  Space was skipped (layout outside the pair, a hold still up, modifiers held) - off the input
  thread.

## Design, steps 2-5

### Rules (step 2, built)

- Loaded on `Restore` (UI thread) into one sorted vector of 64-bit hashes of (kind, exception,
  case, script, pattern); the text is not kept.
- Sources: `packs/*.rules`. `langpack rules <out> <ps.dat> [triggers.dat]` decodes Punto's (XOR
  0xAA but CR and LF, CP1251); a trigger becomes a begin rule. The user's words are step 3's.
- On Space, on the lane: the whole word, every prefix of `word + " "`, every substring of
  `" " + word + " "` up to the longest pattern (18), then the exceptions over the same.
- Guards, measured on the random forms: Punto's anywhere rules alone flipped 110 of 3000 English
  forms (compounds: `subclasses`, `checkmark`), and exceptions that also blocked the dictionary
  lost 5 of 3000 fixes. Hence the rules in Decisions above.
- Every log line names the rule: `FIX ofc -> щас (rule B ofc, ...)`; a run adds
  `FIX ey too, the next word decided`.
- Not built: `langpack rules gen`, trigrams with zero count in language A that are frequent in B's
  words rendered in A. On Space the dictionary and ngram already cover it; it matters for step 5.

### Learning (step 3, built)

| Signal | Learns |
|---|---|
| `kbd.convert_word` or `kbd.undo_auto_convert` right after an auto-fix | "never" for the word as typed |
| `kbd.convert_word` on a word autocorrect judged and kept | "always" for the word |
| A judged word erased whole, the same keys typed again from the other side, Space | "never" after an auto-fix, "always" after a keep; that Space is left alone |

- A phrase (the run) teaches nothing: which of its words erred is unknown.
- The erase-retype signal survives a layout switch by keyboard (Alt+Shift and Ctrl+Shift are bare
  modifiers, Win+Space a Space chord); anything else in between drops it.
- The word is written in the script it was typed in (`ofc` never, `ye` always), so one entry
  serves one side. A word learned the other way moves between the lists.
- Not built: strikes on rules (two undos disable a Punto rule). A per-word never covers the
  complaint, and a strike that disables a broad `A` rule silently loses fixes; step 4's log decides.

### Real-text bench (step 4)

- The decision log plus what followed each decision (undo, manual convert, erase-retype) is the
  corpus; `langpack` replays it and prints false fixes and misses. `tools/punto/corpus.txt` seeds
  it. `bench.ps1`'s random forms stay only as a regression check.

### Mid-word (step 5, only if the data asks)

- Worth it only if step 2's logs show many words fixed late. Needs lookups in the hook (a contract
  change: architecture review first), an edit of k-1 keys with the held k-th key replaying in the
  new layout, and a wait while the prefix can still become an exception.

## Checklist

### Step 1 - stop the harm

- [x] Sides by script and the last layout per side: `LayoutTable::Script`, `WordTracker`
      `_sideOf`/`_target`, `Word.Layout` replaces `Word.Side`
- [x] `InputLanguage::Installed()` adds the session's layouts that Preload lacks
- [x] Ngram only for single words of 5+ letters (`Detector.cpp`, `kNgramMinLetters`)
- [x] `LogDecisions` names why a Space was skipped
- [x] `tests/AutocorrectDeathProbe.ps1`: the reported session typed live, with a timeline
- **Result:**
  - Corpus: false fixes 5 -> 1, fixed 321 -> 320 of 325; random-form bench unchanged.
  - Live: uk (0422) and fr (040C) words get verdicts; a word typed in fr converts into uk (the
    last Cyrillic); `щас` stays. `tests/HotkeyProbe.ps1 -Autocorrect` passes.
  - `.\build.ps1 -Test -Tools` and `-Config Debug` clean. Binary 1,185,280 -> 1,186,816 bytes.
  - Review (quality, bug-hunt): 1 finding, fixed - `_suspend` cleared the state by hand and
    missed `_heldFocus`; it now calls `_reset`. A re-check found nothing.

### User controls - before rules

- [x] List compatible packs, choose one per layout, and show missing or incompatible selections
- [x] Pick running apps or type an `.exe`, manage excluded apps as a list, keep the config field
- [x] Bind `kbd.undo_auto_convert` to undo only a successful recent auto-fix; no default hotkey
- **Checked:** `build.ps1 -Test -Tools`, `-Config Debug`, `KeyboardSettingsProbe.ps1`
  (pack change, exclusion Add/Remove, save, Cancel). `HotkeyProbe.ps1 -Autocorrect` passed
  before extracting the edit builder; the final rerun could not take focus from the desktop
  and stopped before sending keys.
- Per-result notification and sound: done under Feedback and password fields below.

### Step 2 - rules on Space

- [x] `Convert/Rules.*`: the table, exceptions, the veto and the guards
- [x] `langpack rules` (import); `ps.dat` becomes `packs/punto.rules`. `rules gen` moved to step 5
- [x] `Detect` order: rules > dictionary > context > ngram; the log names the rule. User rules: step 3
- [x] `triggers.dat` in the same table
- [x] The next word decides a word of both languages (the run), undo covers the phrase
- **Done when:** corpus false fixes <= 1 and fixed >= 98.5%, including `ye` and `ofc`; Punto's
  whole-word rules cover the short words the context branch misses.
- **Result:**
  - Corpus: fixed 320 -> 322 of 325 (99.1%), false 1 -> 1 (`en.`). `ye`, `yt`, `vs`, `jy` go by
    whole-word rules at a fresh context, `ofc` by `B ofc`; left: `"щас"` typed as `@ofc@` (the
    punctuation guard) and `щасю`. Per message with a fresh context: the run adds 2 fixes without
    rules (`ye`, `ey`), 1 with them (`ey`), no false ones.
  - Random forms, 3000 each on the current packs: ru-in-en 3000 -> 3000, en-in-ru 2945 -> 2991,
    false en 3 -> 6 (`kvetching`, `aasvogels`, `itylus` by begin rules), false ru 0 -> 1 (`туфе`,
    Punto's whole-word rule for `next`).
  - Without Punto's data (a public build) the run is what fixes `ye ukzye`.
  - Live `HotkeyProbe.ps1 -Autocorrect`: `ey ukzye ` -> `ун гляну `, undo -> `ey ukzye `,
    `ofc ` -> `щас `, earlier steps unchanged. `build.ps1 -Test -Tools` (new `AutocorrectTest`)
    and `-Config Debug` clean. Binary 1186 -> 1203 KB (the rules ~9.5 KB with sort and file read).
  - Review: architecture on the design found the run lost for fast typists (fixed with
    `_heldIntact`); quality and bug-hunt on the code found nothing, architecture one include
    without the `Core/` prefix (fixed).

### Step 3 - learning

- [x] The three signals in the tracker; strikes per rule deferred to step 4's log
- [x] Learned words in the config, an input-thread snapshot, filed on the UI thread; a settings list
- **Done when:** an undo of a word stops the next fix of it at once, and a restart keeps it.
- **Result:**
  - Live `HotkeyProbe.ps1 -Autocorrect`: undo of `привет` -> `ghbdtn` stays next time; a manual
    convert of the kept `hello` makes the next `hello ` `руддщ `; `текст` erased, layout switched,
    `ntrcn` retyped stays, and the next `ntrcn` too; the config holds
    `always=hello never=ghbdtn,vbh,ntrcn` after exit. `KeyboardSettingsProbe.ps1`: the list shows,
    adds (`OFC,` -> `ofc`, no duplicate), removes, Cancel reverts.
  - Fixed on the way: a word erased to zero keys kept its pinned layout, so the next word typed
    there was read in the old one.
  - `build.ps1 -Test -Tools` and `-Config Debug` clean. Binary 1203 -> 1221 KB.
  - Review: architecture on the design found learning lost when it lands while settings is open
    (deferred to Restore) and the judgement stale on the fast-typist path (fixed); quality,
    bug-hunt and architecture on the code found nothing.

### Feedback and password fields

- [x] Hotkey edits announce only a landed edit; an auto-fix gets an optional sound and notification
- [x] `SkipPasswords`: MSAA on a fix only, 50 ms timeout, oleacc loaded on first use
- [x] Keyboard page: layout and pack on one line, lists last, no "(available)"
- [x] `SkipFullscreen`: a window covering its monitor counts as excluded (`Foreground::Snapshot`)
- **Result:**
  - Overlay captures: a no-op convert or undo shows nothing, a real one `converted`/`undone`, an
    auto-fix `ghbdtn → привет`; the multi-press path the same (`multi-one`, `multi-two` only when
    the edit landed). Sound playback not heard (no audio device here).
  - Live `HotkeyProbe.ps1 -Autocorrect`: `vjht` stays in a password box and in a borderless
    window the size of the monitor, becomes `море` back in the probe window; with
    `SkipFullscreen` off the fullscreen one is fixed too. `KeyboardSettingsProbe.ps1` passes.
  - `build.ps1 -Test -Tools` and `-Config Debug` clean. Binary 1221 -> 1234 KB.
  - Review: architecture on the guard found a hung app could block `AccessibleObjectFromWindow`
    (now `SendMessageTimeout` + `ObjectFromLresult`); on the code, an unclaimed `WM_GETOBJECT`
    object when oleacc is missing and uncleared VARIANTs, and combo items added twice (fixed).
    Fullscreen: a maximized window filling the monitor under an auto-hidden taskbar, and an older
    resolve of the same window landing last (fixed: `IsZoomed`, a generation per focus); the
    re-check found nothing.

### Whole-feature review (2026-09-27)

- [x] Quality and bug-hunt on the whole slice (architecture failed on the gateway)
- **Result:**
  - Fixed: a failed `kbd.convert_word` lost the word, so typing on tracked only the tail; the
    context wiped the fix's language on the next word and noted a dictionary fix twice; `1.` and
    `...` converted (by the dictionary too, not only the context). Simpler: one `_onTarget` guard
    and one `_unhold` restore in `WordTracker`, `BloomBits` shared by lookup and builder (packs
    rebuild byte-identical), no `sstream` in `Detector` (1235 -> 1227 KB), `Available` gone.
  - `langpack` replay: `акщь my perspective its nf не я не хотел бы` gives `from ... та не я не
    хотел бы`, `1.` `...` `;` `2,` stay. `build.ps1 -Test -Tools` and `-Config Debug` clean. Live
    probe not rerun yet.
  - Re-check (quality, bug-hunt on the fixes; architecture on the whole slice): `FeedContext` noted
    a fix before it landed, so a declined one tilted the context (now only `Switched` counts a
    fix); a session-only layout (the report's French one) had an empty id and could not be picked
    for the pair (now its HKL in hex). Not taken: `LayoutLayer` keeping its WinEvent hooks while
    settings is open - the same cost as outside it, and `_watch` is idempotent.

### Step 4 - real-text bench

- [ ] Log -> corpus replay in `langpack`; `corpus.txt` moves under it
- **Done when:** a day of real ru/en/uk typing gives no false switch the log cannot explain.

### Step 5 - mid-word (decide by data)

- [ ] Measure how many fixes land late; decide; architecture review before any hook lookup
- [ ] `langpack rules gen` if mid-word goes ahead

## Carried over, still open

From the old INPUT.md checklists, never verified live:

- Capture in the action dialog by hand; push-to-talk; Win+L and Ctrl+Alt+Del leave no modifier
  stuck (the desktop-switch reset is in, unverified).
- Conversion in Chromium, an Electron app and Windows Terminal; an elevated window without
  SkipUac is left alone.
- `GetKeyState(VK_CAPITAL)` accuracy on the input thread; the hold timeout and the input thread
  priority - measure.
- The tracker was planned to stay off while a CJK IME is active; not built.
- Packs: en is the top 25k of hackerb9/gwordlist (`tools/langpack/README.md`); ru is danakt, with
  no colloquial forms (`щас`, `чё`, `ща`) - add a frequency or colloquial list. Distribution and
  licences are moot for a personal build.

## Not now

- Punto's per-app edit modes (clipboard for Telegram and WhatsApp, games unhooked): take them
  when an app misbehaves.
- Converting a selection; transliteration (`translit-*.dat`); downloading packs from settings.

## Known limits (accepted)

- A Backspace replacement cannot prove the caret is still where the word ended (autocomplete
  popups, editors that reformat). The hold removes the typing race, not this.
- Non-elevated hooks see nothing in elevated windows, and `SendInput` into them fails silently
  (UIPI). SkipUac covers it.
- An app lagging behind its input queue reads the layout switch before keys typed ahead of the
  conversion, so those come out in the new layout.
- Same-script pairs (EN/DE, EN/PL) are out of reach for this detector, and AltGr characters are not
  in the tables.
- A layout switched with the mouse between erasing a word and typing it again is not seen as a
  retype (the click drops the word), so it teaches nothing.
- A password field that exposes no MSAA state (some custom-drawn or game UIs) is not detected;
  the exclusion list is the answer. Asking a Chromium window may switch on its accessibility tree.
- Fullscreen is judged when a window takes the foreground: one that goes fullscreen later (F11, a
  game's mode switch) counts on the next switch to it. F11 browsers and terminals count as
  fullscreen too; a borderless window that fills the monitor through the maximized state does not
  (the exclusion list covers it).
- Punto's patterns were typed on US and RU: with another Latin layout in the pair (AZERTY,
  Dvorak) its Latin rules match the wrong keys.
- The run holds only text typed straight through, and 8 spaces or more between words drop it:
  the count saturates, so the edit could erase the wrong text.
