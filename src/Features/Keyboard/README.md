# Keyboard

Layout indicator, `kbd.switch_layout`, `kbd.convert_word` (convert the last word, again to go back),
`kbd.undo_auto_convert`, and autocorrect on Space. The plan, the Punto findings and the measurements
are in `docs/LAYOUT.md`; the hook and hold contract is in `docs/INPUT.md`.

## Files

| File | Does |
|---|---|
| `Keyboard.cpp` | Module entry: the three actions, registers everything below |
| `KeyboardPage.*` | Settings rows, the exclusions and learned words panel |
| `LayoutLayer.*` | The current layout on the overlay |
| `InputLanguage.hpp` | Installed layouts, the focused window's layout, switching |
| `WordTracker.*` | Stage `kbd.text` on the input thread: the word buffer, Space, convert, undo, learning signals |
| `TypedWord.hpp` | The word: key positions, spaces after it, what autocorrect judged; joining the run |
| `WordEdit.*` | The edit itself: Backspaces, the new text as Unicode, held modifiers around it |
| `Judge.*` | Judges the word on the threadpool while it is typed, so a kept word's Space is not held |
| `Autocorrect.*` | `Runtime` (tables, packs, rules, settings) built on Restore; `Decide`; fix feedback and log lines |
| `Learning.*` | Learned words: an atomic snapshot for any thread, filed and saved on the UI thread |
| `KeyboardPacks.hpp` | Finds `.pack` and `.rules` files in `packs/` |
| `KeyboardExclusions.hpp` | The excluded `.exe` list, parse and join; the per-window app filter |
| `Convert/` | The engine, shared with `tools/langpack`: `LayoutTable` (key <-> char per layout), `Pack`, `Bloom`, `Ngram`, `Alphabet`, `Rules`, `Detector` |

## Threads

- Input thread: `OnKey` stays O(1) and never allocates; it records key positions and wakes the judge.
- Judge (threadpool): detection and rules on the word so far.
- Edit lane (threadpool): the password check, and detection when the judge is behind.
- UI thread: `Restore` builds the `Runtime` from the config, learned words are filed and saved.
- Action worker: hotkey actions and their sound/notification.

## A word on Space

1. Tracked only in an allowed app: not excluded, not fullscreen (`SkipFullscreen`: borderless or
   exclusive, never maximized, judged when the window takes the foreground), and only once
   `Foreground` resolved it.
2. Each key -> the judge runs `Autocorrect::Decide`; Space with its keep goes through unheld. A fix, or
   no verdict yet -> `Input::Edit` holds key delivery and the lane finishes:
   1. `Convert::Detect`: rules > dictionary > ngram (5+ letters only); no letters
      (`1.`, `...`) - never converted;
   2. a learned word (Always / Never) overrules the detector's answer;
   3. a fix in a password field is dropped, learned or not (`SkipPasswords`, asked only for a fix).
3. A fix -> the input thread switches the layout, then `Input::Commit` sends the edit (Backspaces,
   the other rendering as Unicode) and releases the held keys.
4. A short word valid in both languages waits in the run; a sure fix of the next word converts both.

Rule guards: an `E` rule only cancels a rule; a rule never flips a word the dictionary knows; a
mid-word rule also needs the ngram to agree, a word with a sign the dictionary unless the sign is a
letter of the other side (`bv,f` = имба). `FrequencyAnalysis` off drops the guards and the ngram.

## Data: `packs/` next to the exe

```
EasyLauncher.exe
packs/
  en.pack       one per language
  ru.pack
  punto.rules   every *.rules loads, into one table
```

- **Pack** (per language): locale (ISO 639-1, `en`), alphabet (up to 64 frequent symbols), a bloom
  dictionary (is this a word?), bigram and trigram counts (does it look like the language?), a
  default threshold. "Auto" on the page means `<layout's ISO 639-1>.pack`; a picked pack must carry
  the layout's locale.
- **Rules** (per pair, not per pack): UTF-8 lines `[_FLAGS ]pattern`. The pattern is written in the
  characters of the layout it was typed in: `_B ghb` is typed on US and means Russian (`при`),
  a Cyrillic pattern the reverse. Flags: `P` whole word, `B` word begin, none or `A` anywhere,
  `E` never switch, `C` case-sensitive, `D` ignored. A space at the edge anchors to the word edge.
  Rules assume US/RU key positions and are skipped when both sides share a script. Stored as
  64-bit hashes, the text is dropped.
- **Learned words**: `config.json` -> `Keyboard.Learned`, lowercase, typed form (`ofc`, not `щас`).

## Build the tool and the packs

```powershell
.\build.ps1 -Tools     # langpack.exe next to EasyLauncher.exe
```

en and ru have CMake targets (sources and licences in `tools/langpack/README.md`):

```powershell
cmake -S . -B cmake-build-minsizerel -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel `
    -DLANGPACK_EN_INPUT="C:/data/frequency-alpha-alldicts.txt" -DLANGPACK_RU_INPUT="C:/data/russian_words.txt"
cmake --build cmake-build-minsizerel --target langpack_en langpack_ru   # -> cmake-build-minsizerel/packs/
```

Run CMake from an MSVC developer shell; `build.ps1` imports that environment only for itself.

## Punto's rules (personal builds only)

Punto's data is proprietary: never commit it or the `.rules` made from it.

```powershell
PuntoSwitcherSetup.exe /extract C:\punto                 # gives PuntoSwitcher.msi
msiexec /a C:\punto\PuntoSwitcher.msi /qn TARGETDIR=C:\punto\x
langpack rules cmake-build-minsizerel\packs\punto.rules `
    "C:\punto\x\Yandex\Punto Switcher\Data\ps.dat" "C:\punto\x\Yandex\Punto Switcher\Data\triggers.dat"
```

Without it everything works; the run and the dictionary cover most of what the rules add.

## Add a language

1. Install its Windows keyboard layout. `langpack layouts` prints each layout's KLID and ISO 639-1
   code: that code names the pack.
2. Get a UTF-8 word list, one word per line (a Hunspell `.dic` works, flags are stripped). Common
   words matter more than a huge list: rare forms make typos look valid.
3. `langpack pack words.txt <iso> cmake-build-minsizerel\packs\<iso>.pack` (`--threshold`, `--fp`
   are optional; it fails if the alphabet exceeds 64 symbols).
4. Check: `langpack lookup <pack> word ...`, then `langpack --packs <dir> --pair en,<iso>` and
   type lines in the wrong layout.
5. Settings -> Keyboard: pick the layout for one side; Auto finds `<iso>.pack`.
6. Optional: a `langpack_<iso>` target in `tools/langpack/CMakeLists.txt`, like `langpack_ru`.

Limits: the two sides need different scripts (EN/DE cannot work); rules only help a Latin/Cyrillic
pair; a third layout of a side's script (uk next to ru) uses that side's pack.

## Debug and test

- Settings -> Keyboard -> "Log typed words and decisions": `easylauncher.log` next to the exe gets
  `FIX ghbdtn -> привет (rule B ghb, margin=..., pref=...)`, `ok ...`, skips and learned words.
- `.\build.ps1 -Test`: `AutocorrectTest`, `KeyboardConfigTest`.
- Live, on an idle desktop: `tests\HotkeyProbe.ps1 -Autocorrect`, `tests\KeyboardSettingsProbe.ps1`.
- Accuracy: `tools\langpack\bench.ps1` (random forms), `tools\punto\Sim.ps1` on `corpus.txt` (real text).
