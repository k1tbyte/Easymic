# langpack

Pack builder and detection console for the EasyLauncher layout-conversion engine.
The engine itself lives in `src/Features/Keyboard/Convert/`; this tool shares the
same source files (one build, no copy).

## Build

```powershell
.\build.ps1 -Tools
```

The tool is built as `langpack.exe` next to the main executable.

## Commands

### pack

```
langpack pack <wordlist.txt> <locale> <out.pack> [--threshold 0.35] [--fp 0.001]
```

Builds a language pack from a UTF-8 word list (one word per line). Hunspell flags
after `/` are stripped, so `.dic` files from LibreOffice work directly.

### lookup

```
langpack lookup <pack> <word> [word ...]
```

Checks whether each word is in the pack's dictionary (bloom filter) and prints
its trigram score.

### layouts

```
langpack layouts
```

Lists every installed keyboard layout: KLID, ISO 639-1 language code, key count.

### convert

```
langpack convert <from-lang> <to-lang>
```

Reads lines from stdin and prints them converted through the position map of the
two layouts. Unmapped characters pass through unchanged.

### Line mode (default)

```
langpack [--packs dir] [--pair en,ru] [--threshold X]
```

Loads packs from `dir`, binds each language of the pair to its first installed
layout, and reads lines until an empty one. For each line the typed side is
picked by alphabet coverage, the text is mapped to key positions through that
side's reverse map, and `Detect` runs with the context. Prints:

```
  FIX -> fixed text   [source to target, reason margin=X.XX pref=X.XX]
  ok   [source, reason margin=X.XX pref=X.XX, alt: other reading]
```

## Benchmark

```powershell
.\tools\langpack\bench.ps1 [-LangpackExe path] [-DictDir path] [-PackDir path]
```

Reproduces the puntish README table on the en/ru pair. Generated files go to
`$env:TEMP`, nothing is written into the repo.

## Word lists

The dictionaries (`data/dict_*.txt`, ~66 MB total) are not in the repo. Sources:

| File | Source | License |
|---|---|---|
| `dict_en.txt` | [words_alpha](https://github.com/dwyl/english-words) (~370k words) | unverified - check before any pack ships |
| `dict_ru.txt` | [russian-words](https://github.com/danakt/russian-words) (~1.5M forms) | unverified - check before any pack ships |
| `dict_el.txt` | LibreOffice `el_GR.dic` (~828k forms, pre-expanded with unmunch) | unverified - check before any pack ships |
| `dict_he.txt` | unknown | unverified - check before any pack ships |
