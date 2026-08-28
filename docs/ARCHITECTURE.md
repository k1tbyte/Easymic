# Architecture

Target architecture and the migration checklist that gets us there. The top half is the
reference and stays; the checklist at the bottom is deleted once it is done.

The app is growing out of "microphone tray tool" into a general Windows utility host:
hotkeys, microphone, keyboard layout and launcher, with more of the same shape to come.
Planned rename: `Easymic` -> `EasyLauncher`.

Priorities are unchanged and rank above everything below: **input latency, idle footprint,
binary size, simplicity** - in that order.

Baseline for this work: tag `1.3.0.0`.

---

## 1. The problem this solves

| Place | Problem |
|---|---|
| `src/ViewModel/MainWindowViewModel.hpp` (511 lines) | Six jobs in one file: indicator, tray, settings launch, audio state mirror, **the dispatch table for every built-in action** (`MakeBuiltInHandler`), and the hotkey registration loop |
| `src/Lib/HotkeyManager.cpp` (776 lines) | ~270 of them are `KeysNameTable`. The action queue and worker thread are buried here as a private detail, although every feature needs them |
| `src/Lib` (25 files) | Three unrelated kinds mixed: infrastructure (`Str`, `Event`, `Logger`, `Win32Hook`), OS services (`UACService`, `UIAccess`, `CrashHandler`, `UpdateManager`), and actual features (`HotkeyManager`, `InputLanguage`, `CommandRunner`) |
| `src/AppConfig.hpp` | One flat struct holding every feature's settings; a magnet node in the include graph |
| Settings UI | `SettingsWindow::Categories[]` + `IDD_SETTINGS_*` + `HandleSectionChange` switch + one `Initialize*Section` per page + a single giant `HandleButtonClick` switch |

Cost of adding something today:

- **A built-in action**: 3 edits across 2 layers - `BuiltInId` enum, `BuiltInActions::All`, and a
  `case` in `MainWindowViewModel::MakeBuiltInHandler`.
- **A settings checkbox**: 5 edits - `Resource.h`, `Resource.rc`, the page's `Initialize*Section`,
  a `case` in `HandleButtonClick`, and a field in `AppConfig`.

Target: **a feature is one folder plus one line in `main`.**

---

## 2. Core idea

The app is a runner of triggers and actions. Microphone, keyboard layout and launcher are all
*actions*. Hotkeys are *one* trigger source. Indicator, tray and settings are presentation.

So the kernel knows nothing about microphones or layouts. The kernel is a registry plus a
dispatcher; features register into it.

---

## 3. Target layout

```
src/
  Core/                    the kernel - no knowledge of any feature
    ActionRegistry.hpp       id -> ActionDesc {title, args, factory}
    HotkeyService.*          LL hooks, key masks, multi-press (today's HotkeyManager, minus queue)
    KeyNames.hpp             the 256-entry VK name table, on its own
    Dispatcher.*             worker thread + PostToUi
    ConfigStore.hpp          per-module config sections
    Feedback.hpp             sound + overlay text + token expansion (today's ActionFeedback)
    SettingsHost.hpp         settings page registry
    Host.hpp                 the facade a feature module sees

  Platform/                thin wrappers, no domain knowledge
    Str.hpp Event.hpp Logger.* Registry.hpp ComObject.hpp Win32Hook.hpp RateLimiter.hpp
    Uac/ UIAccess/ Shell/ Crash/ Update/ Version/

  Features/                vertical slices
    Microphone/            Wasapi/, actions, indicator, settings page
    Keyboard/              InputLanguage, actions, settings page
    Launcher/              CommandRunner, ShellLaunch, ShellContext, {dir}/{stdout}

  UI/                      Win32 plumbing, feature-agnostic
    BaseWindow.hpp TrayIcon.hpp LayeredWindow.hpp GdiRenderer.hpp DialogControls.hpp
    Settings/              page host, declarative row builder

  Resources/
  main.cpp
```

### Slice rule

**No file under `Features/X/` may include `Features/Y/`.** Only `Core/` and `Platform/` are
shared. If two features need to talk, that is a `Core/` concern.

Enforce it with a grep step in `build.ps1` - cheap, and it is the one rule that keeps the
whole design from rotting back into `Lib/`.

---

## 4. Core contracts

### ActionRegistry

```cpp
// Core/ActionRegistry.hpp
using ActionFn = std::function<void()>;

struct ActionContext {
    std::string_view Args;   // whatever the user typed in this binding's argument row
    Feedback&        Fb;
};

enum ActionFlags : uint32_t {
    None     = 0,
    HoldOnly = 1u << 0,   // both edges, "trigger on release" is meaningless (push to talk)
    NoSound  = 1u << 1,   // feedback is already covered elsewhere (toggle mute)
};

struct ActionDesc {
    std::string_view Id;        // "mic.toggle_mute" - the config key, stable forever
    std::string_view Title;
    std::string_view Group;     // "Microphone" - grouping in the "Add action" menu
    ActionFlags      Flags;
    std::string_view DefaultSound;
    std::string_view DefaultNotification;
    std::string_view ArgsLabel; // empty means the action takes no argument
    std::string_view ArgsHint;
    ActionFn (*Make)(const ActionContext&);
};
```

`Make` is a plain function pointer, not `std::function`: `ActionDesc` stays POD, can be
`constexpr`, and costs nothing in binary size.

### Host

The only thing a feature module ever sees:

```cpp
// Core/Host.hpp
struct Host {
    ActionRegistry&   Actions;
    SettingsHost&     Settings;
    ConfigStore&      Config;
    Dispatcher&       Dispatch;
    Feedback&         Fb;
    HINSTANCE         Instance;
};
```

### Module registration

```cpp
// Features/Microphone/Module.cpp
void Microphone::Register(Host& host) {
    host.Actions.Add({
        .Id = "mic.toggle_mute", .Title = "Toggle mute", .Group = "Microphone",
        .Flags = NoSound, .DefaultNotification = "Mic {mic}",
        .Make = [](const ActionContext& c) -> ActionFn { /* ... */ }});

    host.Settings.AddPage({.Title = L"Microphone", .Build = &BuildPage});
}
```

```cpp
// main.cpp
constexpr void (*Modules[])(Host&) = {
    &Microphone::Register, &Keyboard::Register, &Launcher::Register,
};
```

One line per feature. `BuiltInId`, `BuiltInActions::All` and the `MakeBuiltInHandler` switch
all disappear.

### Binding

```cpp
struct HotkeyTrigger {
    uint64_t Mask = 0;
    uint8_t  Presses = 1;
    bool OnRelease = false, Block = false, TapOnly = false;
};

struct Binding {
    std::string   ActionId;      // "mic.toggle_mute"
    std::string   Args, Sound, Notification;
    uint8_t       SoundVolume = 100;
    bool          ShowNotification = false;
    HotkeyTrigger Trigger;       // later: std::variant<HotkeyTrigger, WindowTrigger, ...>
};
```

The nested `Trigger` is the one deliberately forward-looking piece here. Nothing needs it yet;
it costs one level of nesting now and keeps the binding shape intact if a non-hotkey trigger
source ever appears (window opened, app launched, timer).

### Config

Split the flat `AppConfig` into nested per-module structs. glaze BEVE handles nesting fine.

```cpp
struct Config {
    CoreSettings          Core;        // updates, skipUac, autostart
    MicSettings           Mic;         // volume, keepVolume, bell, mute/unmute sounds
    IndicatorSettings     Indicator;
    std::vector<Binding>  Bindings;
    std::set<std::string> RecentSounds;
    int32_t               Version;
};
```

A module takes `MicSettings&`, not `AppConfig&`. That is what collapses the include graph.

### Serialization format: JSON, not BEVE

The config moves from BEVE (`conf.b`) to prettified JSON (`config.json`). Read with
`glz::read_file_jsonc`, write with `glz::write_file_json` under `glz::opts{.prettify = true}`.

This does not grow the binary. glaze's JSON codec is **already** linked in: `UpdateManager`
parses the GitHub releases API with `glz::read`, and `glz::opts::format` defaults to `JSON`.
Today the binary carries both codecs; after the switch it carries one. Anything else we ever
fetch or parse will be JSON too, so BEVE was always going to be the odd one out.

The config is a cold path - read once at startup, written on Apply - so parse speed and file
size were never arguments for either side.

What this buys: a config that can be read, hand-edited, diffed, put in git, shared, and pasted
into an issue. That matters more than usual here, because nothing migrates (see step 3): every
refactor step that changes the shape discards the file, and with JSON the user can at least see
what they had.

**The hotkey mask has to become a string for any of that to be true.** `Trigger::Mask` is a
packed `uint64_t`; as `1108152124160` it is the one field nobody can read, and it is the
interesting one. It is stored as its display name instead:

```json
{
  "ActionId": "mic.toggle_mute",
  "Trigger": { "Keys": "CTRL + SHIFT + M", "Presses": 2 }
}
```

`GetHotkeyName` already produces that string; step 1 adds the inverse in `Core/KeyNames.hpp`.
A name that does not parse zeroes the trigger - the binding stays in the list, unbound and
visible in settings, rather than vanishing.

Caveat to know about: settings Apply rewrites the whole file, so comments a user added by hand
are lost on the next save. Reading them is supported, round-tripping them is not.

---

## 5. Invariants that must not break

1. **Hook procs stay O(1).** Registry lookups happen at bind time (`RestoreConfig`), never on a
   keypress. The hot path stays: hook proc -> `unordered_map` lookup by mask -> push a
   `std::function` onto the queue. No design below is allowed to add work to it.
2. **Actions never run inside the hook proc.** A proc that overruns `LowLevelHooksTimeout` is
   silently dropped by Windows and input stalls desktop-wide.
3. **Threading contract, stated once and for all** (today it lives in scattered comments):
   - LL hook proc: mask lookup and enqueue only.
   - Action worker: where every action body runs. Feature callbacks land here.
   - UI thread: everything touching a window or the config. Reached via `Dispatcher::ToUi`.
   - WASAPI notification thread: writes state through atomics only.
4. **Config keys are permanent.** `ActionDesc::Id` is written to disk. Renaming one is a
   migration, not an edit.

---

## 6. Settings UI

The highest-leverage part of the refactor and the one that makes Win32 bearable.

Plan: **declarative rows for settings grids, DIALOGEX only where the layout is genuinely custom.**

A module describes its page as data; a generic builder creates the controls with
`CreateWindowExW`, handles DPI layout, and reads/writes the bound config fields.

```cpp
inline const Row GeneralRows[] = {
    {Check,  L"Start with Windows", Bind(&CoreSettings::AutoStart)},
    {Check,  L"Skip UAC",           Bind(&CoreSettings::SkipUac), {}, RequireElevation},
    {Check,  L"Check for updates",  Bind(&CoreSettings::Updates)},
    {Slider, L"Indicator size",     Bind(&IndicatorSettings::Size), {10, 32}},
};
```

Row kinds needed to cover everything that exists today: `Check`, `Combo`, `Slider`, `Text`,
`Button`, `SoundPicker`. Roughly 250-300 lines of builder, once.

Effects:

- a checkbox goes from 5 edit sites to one row plus one config field;
- `Initialize*Section`, `HandleSectionChange` and most of `HandleButtonClick` are deleted;
- Apply/Cancel becomes generic over the binding table instead of copying and diffing `_cfgPrev`.

Keep as DIALOGEX: `IDD_ACTION_EDIT` and `IDD_UPDATE_DIALOG`. Their layout is not a settings grid.

---

## 7. Rename to EasyLauncher

163 textual occurrences; 46 files carrying `EASYMIC_*` include guards.

Touches `CMakeLists.txt` (project name), `src/definitions.h` (`APP_NAME`, `MUTEX_NAME`,
`CONFIG_NAME`, `REPO_NAME`, `DEV_NAME`), `build.ps1` (paths and exe name), the
`EASYMIC_NO_GLOBAL_HOOKS` flag, the autostart registry value name, and the GitHub repo.

Three things are keyed by the app name, so each one changes identity with the rename:

- **The single-instance mutex.** It was `APP_NAME` + a GUID; it is now the bare GUID. The name
  never bought uniqueness - the GUID does that - nor a namespace, which is what a `Local\` or
  `Global\` prefix is for. All it did was let a rename break the check silently. A pre-rename
  build is therefore no longer detected, which is accepted: the updater copies the new binary
  over the running exe's own path and the old process exits before the new one starts, so the
  auto-update path never has two instances live. Only a manual side-by-side run does.
- **The autostart registry value.** The old entry is orphaned, pointing at the previous exe.
- **The `Easymic_SkipUAC` scheduled task.** Orphaned the same way. It carries no trigger and only
  ever ran on demand, so it does nothing by itself.

The last two are left for a one-time manual cleanup rather than code. **No compatibility shims** -
this is a single-user tool, and every line of bridge code written here would outlive its reason.

The config lives next to the exe (`exe_dir/conf.b`), so a renamed exe in the same folder still
finds it. Nothing to do - and step 3 discards it anyway.

Do the rename together with an `#ifndef` -> `#pragma once` sweep: 46 files, about 140 lines gone,
and it makes the rename mechanical.

---

## 8. Explicitly not doing

- **A DI container or service locator.** `Host&` passed to `Register()` is enough.
- **An event bus between features.** Features do not talk to each other.
- **A cross-platform abstraction over Win32.** Windows-only by design.
- **`std::function` anywhere in the hot path.**
- **Rename and refactor in one commit.**

---

## 9. Migration checklist

Each step compiles and ships on its own. Steps 0-2 carry most of the win: after step 2 a new
feature can be written without touching anything that already exists.

### Step 0 - Mechanical prep

- [x] Replace all `#ifndef EASYMIC_*` include guards with `#pragma once` (46 files)
- [x] Rename `Easymic` -> `EasyLauncher` across `CMakeLists.txt`, `build.ps1`, `src/definitions.h`,
      the log file name, the UAC task, the update user-agent, `app.manifest`, and the captions and
      VERSIONINFO in `Resource.rc`. User-visible captions and tooltips now build on `APP_NAME`
      instead of repeating the literal
- [x] `MUTEX_NAME` is the bare GUID, with no app name in it (see section 7)
- [x] Rename `EASYMIC_NO_GLOBAL_HOOKS` -> `APP_NO_GLOBAL_HOOKS`
- [x] No shims for the orphaned autostart value or scheduled task - both cleaned up by hand once
- **Done when:** MinSizeRel builds, binary size unchanged within noise, app starts, and a second
  launch is still rejected.
- **Result:** MinSizeRel 853 KB -> 854 KB (longer strings), Debug builds too. Verified: starts
  (10 threads, 23 MB working set), second launch exits immediately on the held mutex.

### Step 1 - Extract the kernel plumbing

- [x] Move the name tables and `GetHotkeyName` out to `Core/KeyNames.*` as `KeyNames::Format`
- [x] Add the inverse, `KeyNames::Parse` (0 when it does not parse). Round-tripped against every
      named key, every modifier combination and multi-key sequences. It found one real collision:
      VK_OEM_PLUS was named `"+"`, which is the separator - it is `"Plus"` now
- [x] Extract the action queue and worker thread into `Core/Dispatcher.*`, with a deferred slot
      in place of the worker re-reading the hotkey table. `_pendingMask` and the multi-press
      timing stay in `HotkeyService`, where the counting belongs
- [x] Add `Dispatcher::ToUi(fn)`; port `ToggleBellSound`'s hand-off to it. `ID_APP_TOGGLE_BELL`
      stays - it turned out to be a real tray menu item, not only a self-post
- [x] `HotkeyManager` -> `Core/HotkeyService`, now hooks + masks only
- **Done when:** multi-press and push-to-talk still behave, and clean exit still joins the worker
  without a crash report.
- **Result:** 776 lines became 334 (`HotkeyService.cpp`) + 136 (`Dispatcher.cpp`) + 361
  (`KeyNames.cpp`, almost all of it the table). The "~230 lines" estimate was wrong: about 90
  lines of `HotkeyService.cpp` are the two hook procs, which are irreducible Win32 switches.
  Dispatcher's ordering was checked directly - post order, deferred replacement, flush-before-post,
  cancel and stop - since that is where multi-press semantics actually changed. Binary unchanged
  at 854 KB.

### Step 2 - ActionRegistry, and the god object dies

- [ ] Add `Core/ActionRegistry.hpp` with `ActionDesc` / `ActionContext` / `ActionFlags`
- [ ] Add `Core/Host.hpp`
- [ ] Move `ActionFeedback` from `ViewModel/` to `Core/Feedback.hpp`
- [ ] Create `Features/Microphone/Module.cpp` - registers `mic.toggle_mute`, `mic.push_to_talk`,
      `mic.volume_up`, `mic.volume_down`, `mic.toggle_bell`
- [ ] Create `Features/Keyboard/Module.cpp` - registers `kbd.switch_layout`
- [ ] Custom commands become an action too: `launcher.run` in `Features/Launcher/Module.cpp`,
      with the command line as its argument. Removes the built-in/custom fork in the binding loop
- [ ] Rewrite `RegisterConfiguredActions` against the registry: `Find(binding.ActionId)` ->
      `desc->Make({args, feedback})`
- [ ] Delete `src/Actions.hpp`, `BuiltInId`, `BuiltInActions::All`, `MakeBuiltInHandler`
- [ ] `MainWindowViewModel` keeps only indicator, tray and settings launch
- **Done when:** `MainWindowViewModel.hpp` is under 250 lines, `src/Actions.hpp` is gone, and
  adding a new action means editing exactly one file inside one `Features/` folder.

### Step 3 - Config split

**No migration.** Pre-refactor config files are discarded, not converted. The user reconfigures
their bindings once.

- [ ] Split `AppConfig` into `CoreSettings`, `MicSettings`, `IndicatorSettings` + `Bindings`
- [ ] Replace `Action` with `Binding`, hotkey fields nested under `Trigger`
- [ ] Swap BEVE for JSON: `read_file_jsonc` / `write_file_json` with `.prettify = true`,
      `CONFIG_NAME` `conf.b` -> `config.json`. Confirm no `*_beve` call sites remain, so the
      BEVE codec drops out of the binary entirely
- [ ] `Trigger` stores `Keys` as a display-name string, not a packed mask; convert through
      `GetHotkeyName` / `ParseHotkeyName` on save and load. An unparseable name means an unbound
      trigger, not a dropped binding
- [ ] Bump `CurrentVersion` 2 -> 3
- [ ] On load, a `Version` that is not the current one means the whole file is ignored and
      defaults are used. One `if`, no mapping tables - this is what `Version` is for now that
      nothing migrates. It has to be checked *before* the fields are used, or an old file
      half-loads: unknown keys are already dropped silently, so stale `BuiltIn` strings would
      leave bindings pointing at actions the registry has never heard of
- [ ] Each module takes its own settings struct by reference, never the whole `Config`
- **Done when:** a config written by 1.3.0.0 is ignored cleanly - app starts with defaults, no
      error dialog, no half-populated action list - and `config.json` is legible enough to
      hand-edit a binding and have the app pick it up on restart.

### Step 4 - Folder layout

- [ ] `git mv` into `Core/ Platform/ Features/ UI/` per section 3
- [ ] Update `target_include_directories` in `CMakeLists.txt`
- [ ] Add the slice-rule grep to `build.ps1`: fail the build if `Features/X` includes `Features/Y`
- **Done when:** the tree matches section 3 and the slice check passes.

### Step 5 - Settings page registry

- [ ] Add `Core/SettingsHost.hpp` - modules contribute `{title, build}` pages
- [ ] `SettingsWindow` walks the registry instead of the static `Categories[]`
- [ ] `HandleSectionChange` switch replaced by the page's own `Build`
- **Done when:** `SettingsWindow.cpp` has no `IDD_SETTINGS_*` knowledge beyond the frame itself.

### Step 6 - Declarative settings rows

- [ ] Build the row builder in `UI/Settings/` - `Check`, `Combo`, `Slider`, `Text`, `Button`,
      `SoundPicker`, with DPI layout and config binding
- [ ] Port General, Indicator, Sounds and Hotkeys pages to row tables
- [ ] Delete the ported `IDD_SETTINGS_*` templates from `Resource.rc` and their ids from `Resource.h`
- [ ] Delete `Initialize*Section` and the settings half of `HandleButtonClick`
- [ ] Generic Apply/Cancel over the binding table; drop the `_cfgPrev` copy-and-diff
- [ ] Keep `IDD_ACTION_EDIT` and `IDD_UPDATE_DIALOG` as DIALOGEX
- **Done when:** adding a settings checkbox is one row plus one config field.

---

## 10. Open questions

- **`SettingsHost` page ordering.** Registration order is the module order in `main`, which puts
  About in the middle. Needs either an explicit sort key on the page or a fixed head and tail for
  General and About.
- **Where the indicator lives.** It is microphone-shaped today (mute state, peak meter) but the
  notification overlay is used by every action. Likely split: `Core/Feedback` owns the overlay,
  `Features/Microphone` owns the mic pill and the peak meter.
