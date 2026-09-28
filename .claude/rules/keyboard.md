---
paths:
  - "src/Features/Keyboard/**"
  - "tests/Keyboard/**"
  - "tools/Keyboard/**"
---

# Keyboard: conversion and autocorrect

## Files

- `WordTracker` - stage `kbd.text`: the word as key positions (never characters), Space, mid-word
  fixes, convert, undo, learning signals.
- `Judge` - `Autocorrect::Decide`/`Early` on the threadpool while the word is typed. Never on the input
  thread: a page fault in the mapped pack would stall desktop input.
- `Autocorrect` - `Runtime` (tables, packs, rules, settings) built on Restore; verdicts, feedback, log.
- `TypedWord` (the word, the run), `WordEdit` (Backspaces, Unicode text, held modifiers), `Learning`.
- `Convert/` - the engine, shared with `tools/Keyboard/langpack`: `LayoutTable`, `Pack`, `Rules`, `Detector`.

## Flow

- A key that changes the word wakes the judge. Space with a ready keep is not held. A fix, or no
  verdict yet: `Input::Edit` holds, the lane checks for a password field (`SkipPasswords`) and decides,
  the input thread switches the layout and commits Backspaces plus the other rendering as Unicode.
  `apply` drops unless the hold, focus and foreground are unchanged.
- Mid-word (`MidWord`), once per word: the first 4 letters start no word of the typed language
  (`Pack::Begins`), and with frequency analysis the converted start begins a word of the other side, its
  ngram wins by 1+, and 4+ keys or a Punto `B`/`A` rule. The word is then pinned.
- `Detect`: learned > rules > dictionary > ngram. No letters: never. Up to 2 letters, or a word both
  dictionaries know: kept; a 2+ letter word of both waits for the next (the run), whose sure fix converts
  both. The ngram decides only single words of 5+ letters.
- Rule guards: never flip a word the dictionary knows; an anywhere rule needs the ngram too; a sign needs
  the dictionary unless it is a letter of the other side (`bv,f` = имба; `e.g`, `:p`, `ofc.` stay). Rules
  are skipped when both sides share a script.
- The pair is two layouts by KLID. A third layout of a side's script (uk next to ru) is never judged,
  only converted by hand; a conversion always goes to the pair's other layout.
- Learning (`Keyboard.Learned`, typed form, lowercase): convert or undo right after an auto-fix teaches
  Never, converting a kept word teaches Always, erasing and retyping from the other side teaches too.
  A trailing `*` in Never refuses a start, mid-word only.
- Undo returns the fix and what was typed after it; a caret move, chord, layout switch or next fix ends
  it. Not tracked: excluded apps, fullscreen windows, an unresolved foreground app.

## Data: `packs/` next to the exe, never in git

- `<iso 639-1>.pack`: alphabet (up to 64 symbols), a bloom of words and of every 2-4 letter start,
  bigrams, trigrams, threshold. "Auto" is the layout's language.
- `*.rules`: UTF-8 `[_FLAGS ]pattern`, written as typed (`_B ghb` = при). `P` whole word, `B` begin,
  none or `A` anywhere, `E` never switch, `C` case-sensitive, `D` skipped; a space at an edge anchors.
- `langpack pack <words.txt> <iso> <out.pack>`: one word per line; common words beat a huge list.
  `langpack rules <out> <ps.dat> [triggers.dat]` converts Punto's (XOR 0xAA, CP1251).
- CMake `langpack_en` (`-DLANGPACK_EN_INPUT`: hackerb9/gwordlist, CC BY 3.0, top 25k) and `langpack_ru`
  (`-DLANGPACK_RU_INPUT`: danakt/russian-words, MIT, converted to UTF-8).
- `langpack --packs <dir> --pair en,ru` is the detection console; `tools/Keyboard/langpack/bench.ps1`
  and `tools/Keyboard/punto/Sim.ps1` measure accuracy. `LogDecisions` writes every verdict to `easylauncher.log`.

## Decided

- No language context: it could only brake fixes and prevented no false fix.
- Punto's rules keep our guards: without them rare English words flip (`ashtray` -> `фырекфн`).
- Punto fixes on the key that completes a rule: eats it, erases, switches, retypes as VKs. Its per-app
  modes (clipboard for Telegram, games unhooked) are not copied.

## Accepted limits

- Backspace cannot prove the caret is still at the word's end; a browser's inline autocomplete takes
  the first one.
- Elevated windows: the hooks see nothing and `SendInput` fails silently.
- Same-script pairs (EN/DE) and AltGr characters are out of reach; Punto's Latin rules assume US keys.
- A password field without MSAA state is not detected: exclude the app.

## Open

- Real-text bench: `langpack` replays the decision log and what followed each decision (undo, convert,
  erase-retype) as the corpus.
- Live check in Chromium, an Electron app and Windows Terminal. Are fixes dropped there (the password
  check against the 150 ms hold)? The log says `fix landed|dropped after N ms`.
- The ru pack lacks colloquial forms (`щас`, `чё`); a CJK IME is not detected.
