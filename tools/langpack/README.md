# langpack

Pack builder and detection console for the EasyLauncher layout-conversion engine.
The engine itself lives in `src/Features/Keyboard/Convert/`; this tool shares the
same source files (one build, no copy).

## Build

```powershell
.\build.ps1 -Tools
```

Or build just the standalone tool from an MSVC Developer PowerShell session:

```powershell
cmake -S tools/langpack -B cmake-build-langpack -G Ninja
cmake --build cmake-build-langpack --target langpack
```

`build.ps1 -Tools` keeps `langpack.exe` next to `EasyLauncher.exe`.

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

### rules

```
langpack rules <out.rules> <ps.dat> [triggers.dat]
```

Converts Punto Switcher's rule files (`Data\` of its install) into UTF-8 lines
of the same `[_FLAGS ]pattern` syntax; a trigger becomes a word-begin rule. The
app loads every `packs/*.rules`. Punto's data is proprietary: personal builds
only, never committed (see `docs/LAYOUT.md`).

### Line mode (default)

```
langpack [--packs dir] [--pair en,ru] [--threshold X] [--frequency off] [--rules file ...]
```

Loads packs from `dir`, binds each language of the pair to its first installed
layout, and reads lines until an empty one. For each line the typed side is
picked by alphabet coverage, the text is mapped to key positions through that
side's reverse map, and `Detect` runs with the rules (`--frequency off`: as with
`FrequencyAnalysis` off, no ngram and no rule guards). Prints:

```
  FIX -> fixed text   [source to target, reason margin=X.XX]
  ok   [source, reason margin=X.XX, alt: other reading]
```

A rule decision names its pattern: `rule B ofc margin=...`.

## Benchmark

```powershell
.\tools\langpack\bench.ps1 [-LangpackExe path] [-DictDir path] [-PackDir path]
```

Reproduces the puntish README table on the en/ru pair. For a new frequency
pack, also pass `-EnglishList cmake-build-minsizerel/tools/langpack/en_filtered.txt`
and `-PackDir cmake-build-minsizerel/packs`; the default English list belongs
to the old Puntish pack. Generated samples go to `$env:TEMP`.

---

## Building language packs from external word lists

Language packs (`.pack` files) are **not** stored in the repository and are
**not** downloaded automatically. You build them locally from word-list files
that you obtain separately. The opt-in CMake targets write them to `packs/`
next to the application executable.

### Prerequisites

| Pack | Source file | Where to get it | License |
|------|-----------|-----------------|---------|
| **en** | `frequency-alpha-alldicts.txt` | [hackerb9/gwordlist](https://github.com/hackerb9/gwordlist) — ranked English word-frequency list compiled from multiple dictionaries | [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/) — derived from [Google Books Ngrams](https://storage.googleapis.com/books/ngrams/books/datasetsv2.html); see the gwordlist repo for attribution |
| **ru** | `russian_words.txt` (UTF-8) | [danakt/russian-words](https://github.com/danakt/russian-words) — ~1.5 M Russian word forms. The original file is CP-1251; convert to UTF-8 first (e.g. `iconv -f CP1251 -t UTF-8 russian.txt > russian_words.txt`) | [MIT](https://github.com/danakt/russian-words/blob/master/LICENSE) |

### CMake targets

The sub-project in `tools/langpack/CMakeLists.txt` defines optional
`langpack_en` and `langpack_ru` targets. They only appear when you pass the corresponding input paths at configure
time. Run CMake commands below from an MSVC Developer PowerShell session;
`build.ps1` imports the compiler environment only for its own process.

#### English pack

The English frequency file is pre-filtered to keep only clean lowercase ASCII
words (up to 28 letters, only `a` and `i` permitted as one-letter words).
The default is the top **25 000** words; override with `-DLANGPACK_EN_TOP=N`.
This cutoff excludes `yee` (ranked around 27 000 by the source); a larger
vocabulary reduces misses but admits more obsolete dictionary entries.

```powershell
# Configure (once)
cmake -S . -B cmake-build-minsizerel -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel `
    -DLANGPACK_EN_INPUT="C:/data/frequency-alpha-alldicts.txt"

# Build the pack
cmake --build cmake-build-minsizerel --target langpack_en
# -> cmake-build-minsizerel/packs/en.pack
```

#### Russian pack

The input must already be UTF-8. If the source file is CP-1251, convert first:

```bash
iconv -f CP1251 -t UTF-8 russian.txt > russian_words.txt
```

```powershell
cmake -S . -B cmake-build-minsizerel -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel `
    -DLANGPACK_RU_INPUT="C:/data/russian_words.txt"

cmake --build cmake-build-minsizerel --target langpack_ru
# -> cmake-build-minsizerel/packs/ru.pack
```

#### Both at once

```powershell
cmake -S . -B cmake-build-minsizerel -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel `
    -DLANGPACK_EN_INPUT="C:/data/frequency-alpha-alldicts.txt" `
    -DLANGPACK_RU_INPUT="C:/data/russian_words.txt"

cmake --build cmake-build-minsizerel --target langpack_en langpack_ru
```

### Deploying the packs

The app only reads `.pack` files inside `packs/` beside `EasyLauncher.exe`. The
CMake targets above put them there; when building elsewhere, copy them there:

```
EasyLauncher.exe
packs/
  en.pack
  ru.pack
```

### Licensing note

The `.pack` files you build contain bloom-filter hashes and n-gram statistics
derived from the source word lists. Respect the upstream licenses:

- **English** — CC BY 3.0 (attribution required; see the gwordlist README for
  the full Google Books Ngrams attribution).
- **Russian** — MIT (include the LICENSE file from danakt/russian-words if you
  redistribute the pack).

No `.pack` binaries or source word lists are committed to this repository.

---

## Legacy word lists

Previous development used different dictionaries (`data/dict_*.txt`, ~66 MB).
Those are not in the repo. Sources:

| File | Source | License |
|---|---|---|
| `dict_en.txt` | [words_alpha](https://github.com/dwyl/english-words) (~370k words) | unverified |
| `dict_ru.txt` | [russian-words](https://github.com/danakt/russian-words) (~1.5M forms) | MIT |
| `dict_el.txt` | LibreOffice `el_GR.dic` (~828k forms, pre-expanded with unmunch) | unverified |
| `dict_he.txt` | unknown | unverified |
