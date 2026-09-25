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

Cost of adding something, before this refactor:

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
    Dispatcher.*             worker thread + ToUi
    Feedback.hpp             sound + overlay text + token expansion
    Overlay.hpp              the layer registry: who is allowed to draw on the overlay
    Tray.hpp                 tray providers: who paints the icon, who adds a menu item
    Lifecycle.hpp            Suspend / Restore - the settings window taking the config and back
    SettingsHost.hpp         settings page registry
    SoundCatalog.hpp         bundled sounds, and the lookup every picker goes through
    AppConfig.hpp            the on-disk shape, one section per module
    Host.hpp                 the facade a feature module sees
    Input/                   the input thread, LL hooks on demand, the stage pipeline, the hold
    Hotkeys/                 one trigger source, not the concept - a stage of Input
      HotkeyService.*          key masks, multi-press
      KeyChord.hpp             downs and ups -> a mask
      KeyNames.*               the 256-entry VK name table and its inverse
      HotkeyCapture.hpp        the capture field the action dialog uses
      Bindings.hpp             configured bindings -> registered hotkeys

  Platform/                thin wrappers, no domain knowledge
    Str.hpp Event.hpp Logger.* Registry.hpp ComObject.hpp Win32Hook.hpp RateLimiter.hpp
    Gdi.hpp                  rounded pill, text measure, centred draw - a layer's whole toolkit
    TrayIconTheme.hpp        light taskbar detection, and the darkened copy of an icon
    LayeredWindow.hpp        per-pixel alpha surface + UpdateLayeredWindow
    Controls.hpp             a child control placed in dialog units
    UIAccess/ UACService.* CrashHandler.* UpdateManager.* Version.*

  Features/                vertical slices
    Microphone/            Wasapi/, actions, its overlay layer, its icons, its settings page
    Keyboard/              InputLanguage, actions, its overlay layer, its settings page
    Launcher/              CommandRunner, ShellLaunch, ShellContext, {dir}/{stdout}
    Desktops/              virtual desktop COM facade, presets, placer, page, tile editor, tracker

  UI/                      Win32 plumbing, feature-agnostic
    BaseWindow.hpp TrayIcon.hpp
    Overlay/               the surface, the slot layout, the text pill
    Settings/              page host, declarative row builder

  Resources/
  main.cpp
```

### Layering rules

**A file under `Features/` may include `Core/` and `Platform/`, and nothing else.** Two rules
fall out of that, and both are grepped in `build.ps1` rather than trusted - cheap, and they are
what keeps the whole design from rotting back into `Lib/`:

- **No file under `Features/X/` may include `Features/Y/`.** If two features need to talk, that
  is a `Core/` concern.
- **No file under `Features/` may include a header under `src/UI`.** A feature describes itself
  to the frame - an action, an overlay layer, a settings page, a tray provider - and never
  reaches up into a window.

**A feature may own a private window.** Its own class, its own message loop if it is modal, built
from `Platform/` helpers only: the Desktops page panel and the fullscreen tile editor are both
this. A helper both the frame and a feature need moves down to `Platform/` (`LayeredWindow.hpp`,
`Controls.hpp`), never up into a feature.

None of the ways of breaking either rule is a compile error: `src/UI` is on the include path, so
`#include "MainWindow.hpp"` builds, and a quoted include searches the includer's own directory
first, so `#include "../Launcher/Launcher.hpp"` builds too. The check therefore judges an
include by **where it lands** - resolved against the includer's directory - and only then by how
it is written, and it reads `<angle>` includes as well as quoted ones, because the angle form
searches the same `-I` directories.

---

## 4. Core contracts

### ActionRegistry

```cpp
// Core/ActionRegistry.hpp
using ActionFn = std::function<void()>;

struct ActionContext {
    const std::string& Args;          // whatever the user typed in this binding's argument row
    const std::string& Notification;  // already composed; only an action that delivers its own reads it
    Feedback&          Fb;
};

enum class ActionFlags : uint32_t {
    None        = 0,
    NoSound     = 1u << 0,  // feedback is already covered elsewhere (toggle mute)
    RunsCommand = 1u << 1,  // the argument is a command line: command tokens, command row
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
    ActionFn (*Make)(const ActionContext&);         // empty return = the action declines
    ActionFn (*MakeRelease)(const ActionContext&);  // the release edge, when it needs both
};
```

`Make` is a plain function pointer, not `std::function`: `ActionDesc` stays POD, can be
`constexpr`, and costs nothing in binary size. Returning an empty `ActionFn` is how an action
refuses an argument it cannot use, and the binding is then not registered at all - that is what
replaced the "custom action with no command line" check in the binding loop.

`HoldOnly` is not a flag. Carrying a `MakeRelease` is what makes "trigger on release" meaningless
for an action, so one fact says it rather than two that can disagree.

`ActionFlags` is scoped and `ActionContext::Args` is a reference rather than a view: an unscoped
`None` in the global namespace is a collision waiting to happen, and `[args = c.Args]` on a view
compiles and dangles.

The registry has to be **enumerable**, not only `Find(id)`. The "Add action" menu is built by
walking every registered action and grouping by `Group`, which is the whole reason `Group` is on
`ActionDesc`. Enumeration order must be stable across runs or the menu reshuffles itself.

### Host

The only thing a feature module ever sees:

```cpp
// Core/Host.hpp
struct Host {
    AppConfig& Config;    // ConfigStore in step 3
    Feedback&  Fb;
    HINSTANCE  Instance;
};
```

`ActionRegistry`, `Dispatcher` and `HotkeyService` are free namespaces a module reaches by
including them, so `Host` does not carry them - it holds only what a module cannot reach on its
own. `SettingsHost` joins it in step 5.

### Module registration

```cpp
// Features/Microphone/Microphone.cpp - namespace Mic, because the Windows SDK already has a
// Microphone (an EndpointFormFactor enumerator in mmdeviceapi.h)
void Mic::Register(Host& host) {
    for (const auto& desc : Actions) {
        ActionRegistry::Add(desc);
    }
}
```

```cpp
// main.cpp
constexpr void (*Modules[])(Host&) = {
    &Mic::Register, &Keyboard::Register, &Launcher::Register,
};
```

One line per feature. `BuiltInId`, `BuiltInActions::All` and the `MakeBuiltInHandler` switch
all disappear.

### Bindings

`Core/Hotkeys/Bindings.hpp` is the one place that knows both the config shape and the hotkey
service: `Bindings::Apply(actions, feedback)` resolves each `ActionId` through the registry,
composes the notification, wraps the handler and registers the combination. It was the view
model's job until step 2 and belongs to neither the view nor a feature.

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

### Overlay layers

The overlay is a *surface*, not a microphone indicator. It owns everything that is true of the
surface - where it sits, how big a pill is, whether it stays on top, whether screen capture sees
it, what a pill looks like. It owns nothing that is drawn *in* it.

It is a surface and not a window on purpose. `MainWindow` stays the app's one frame: it owns the
HWND, the tray icon, the message routing and `Dispatcher::BindUi`, because whoever owns the
message loop is the one the worker gets back to the UI thread through. A second window for the
overlay would be a second answer to that question, plus another window class and another HWND
for nothing - the overlay hides and shows constantly today and the tray icon on the same handle
never notices. So the overlay is `UI/Overlay/OverlaySurface.hpp`, driving the frame's window,
and what it takes from the config is `OverlaySettings` and nothing else.

What is drawn in it is a list of layers, and a layer is contributed by whoever the content belongs
to: the mic pill and the peak meter by `Features/Microphone`, the notification text by the overlay
itself (it is the overlay's own pill, fed through `Feedback`), the keyboard layout by
`Features/Keyboard` on the day someone wants it.

```cpp
// Core/Overlay.hpp
/// What a layer is told before it has a slot: the geometry it scales everything from.
struct OverlayCell {
    int Height;                 // the pill height, the one size a layer scales everything from
    int IconSize;               // Overlay::Size from the config
    float FontSize;
    bool Preview;               // the settings window is open - show a state, not nothing
};

struct OverlaySlot {
    int Width = 0;              // 0 = nothing on screen right now
    bool WantsTick = false;     // poll me anyway: at width 0 is how a layer watches for its cue
};

struct OverlayLayer {
    std::string_view Id;        // "mic.pill", "kbd.layout"
    int Order = 100;            // left to right; Overlay::Last pins the text pill
    OverlaySlot (*Measure)(const OverlayCell&);
    /// Origin is the slot, so a layer draws at 0,0 across the width Measure asked for
    void (*Render)(const OverlayCell&, Gdiplus::Graphics& canvas, int width);
    uint16_t TickMs = 0;
    void (*Tick)() = nullptr;   // invalidates by itself when it changed something
};

namespace Overlay {
    inline std::vector<OverlayLayer> Layers;
    void Add(const OverlayLayer&);
    /// Set by the window at bind time, the way Feedback takes its PostFn. Callable from any
    /// thread - the UI side posts.
    inline void (*Invalidate)() = nullptr;
}
```

Same shape as `ActionRegistry` and `SettingsHost`, deliberately: a POD descriptor with plain
function pointers, an `Order` sort key, enumerated in a stable order. A layer holds its state in
its own slice and reads it on the UI thread, exactly as `SettingsPage::Build` does.

What a layer is told differs between the two phases, and the first draft of this design made
that a second struct - `OverlayCanvas : OverlayCell`, adding the canvas and the granted width.
It bought nothing: the two callbacks already have two signatures, so a `Measure` cannot see a
width it has not asked for and a `Render` cannot get a null canvas whether the canvas arrives as
a reference member or as a reference parameter. The parameters say it in no lines at all.

Two invariants the layout pass keeps, both of them today's behaviour generalised from two fixed
slots to N:

- **The overlay draws the pill, a layer draws its content.** One corner radius, one background,
  one gap, so a pill a feature contributes cannot look like a different app.
- **The anchor is the user's, the width is the content's.** Slots are measured, laid out left to
  right from the anchor, and the whole strip flips to the left of it when the work area's right
  edge is in the way. Widening for a notification must never walk the indicator across the screen.

`WantsTick` is what pays for the peak meter honestly. Today "muted or talking" keeps an empty
window on screen because the peak timer only ticks while the indicator is visible, and the timer
is what discovers that the mic went live. A layer that asks for ticks at width 0 needs no phantom
window: the timer runs, the window stays hidden until there is something in it.

### Tray providers

The tray is the other surface, and it is not shaped like the overlay: **the icon is one slot and
the menu is a list.** Two features cannot both paint the icon, and there is no reason they cannot
both put an item in the menu. One registry, two kinds of contribution, and the difference is
settled by the user rather than by registration order.

```cpp
// Core/Tray.hpp
struct TrayProvider {
    std::string_view Id;      // "mic.state" - stored in the config, so permanent
    const wchar_t* Title;     // the radio label on the Tray page
    int Order = 100;
    /// UI thread. What the icon should be right now; null keeps whatever is up.
    HICON (*Icon)() = nullptr;
    /// UI thread. Null falls back to APP_NAME.
    std::wstring (*Tooltip)() = nullptr;
    /// Optional. The darkened copies for a light taskbar are built once, on the first switch to
    /// one, so a provider that themes its icons wants to be told rather than to poll.
    void (*ThemeChanged)() = nullptr;
    /// Optional, and independent of the radio: a provider's menu item is offered whether or not
    /// it owns the icon. The label is a function because it reports state - "Enable bell sound"
    /// and "Disable bell sound" are one item.
    const wchar_t* (*MenuLabel)() = nullptr;
    void (*MenuInvoke)() = nullptr;
};

namespace Tray {
    inline std::vector<TrayProvider> Providers;
    void Add(const TrayProvider&);
    /// Set by the window at bind time. Any thread - the UI side posts.
    inline void (*Refresh)() = nullptr;
    /// Who paints the icon for a stored choice: empty takes the first by Order, unknown the last.
    const TrayProvider* Owner(std::string_view id);
}
```

The frame registers one provider itself: `"tray.app"`, `IDI_APP` and `APP_NAME` - the same icon
the settings window puts in its own title bar. It pins itself `Last`, which makes it both a real
choice on the radio and the thing that paints the tray when nothing else can. There is no "None"
option: the tray icon carries the only route to Settings and Exit, so it always exists and the
choice is only what paints it.

Which one is chosen lives in `TraySettings::Provider` as an `Id`, not an index: the order of
`Modules[]` in `main` must not be able to silently repoint a setting the user made. An `Id` no
module registered falls back to the app icon, the same way an unparseable hotkey name leaves a
binding visible and unbound rather than dropping it.

**Empty is not the same as unknown.** An empty `Provider` is a config nobody has chosen in yet -
first run - and it resolves to the first provider by `Order`, which is the microphone as long as
it registers one. Sort order deciding the *default* is fine; sort order deciding a *stored choice*
is what the `Id` exists to prevent. That way a fresh install keeps today's behaviour without
`Core/AppConfig.hpp` naming a feature in its defaults.

A provider that has no `Icon` is not on the radio at all - it is a menu contribution and nothing
else. Wanting a menu item and wanting the icon are separate claims, and one is exclusive.

**Menu command ids stop being resource ids.** The tray assigns them at popup time by position and
calls the contribution back, so `ID_APP_TOGGLE_BELL`, its `case` in the view model's switch and
the `_config.Mic.BellVolume` read inside `MainWindow::ShowTrayContextMenu` all disappear together
- that read is the last thing in the window that knows what a microphone is.

The menu keeps Settings first and Exit last whatever registers in between, so the two items that
were always there do not move as features come and go. `MenuInvoke` runs on the UI thread, where
the popup is pumped: anything that belongs on the worker hops there itself, the same rule every
other UI-thread callback in this app follows. One item per provider, because the bell toggle is
the only contribution that exists and a vector for a hypothetical second one is a line of code
that would wait years to matter.

### Config

Split the flat `AppConfig` into nested per-module structs. glaze BEVE handles nesting fine.

```cpp
struct Config {
    CoreSettings          Core;        // updates, skipUac, autostart, multi-press window
    OverlaySettings       Overlay;     // position, size, on top, exclude from capture, text pill
    TraySettings          Tray;        // which provider paints the icon, by Id
    MicSettings           Mic;         // volume, keepVolume, bell, sounds, pill mode, threshold
    std::vector<Binding>  Bindings;
    std::set<std::string> RecentSounds;
    int32_t               Version;
};
```

A module takes `MicSettings&`, not `AppConfig&`. That is what collapses the include graph.

**Which section a field goes in is decided by who would still want it if the feature were
deleted.** `Size`, `OnTop` and `ExcludeFromCapture` are true of the overlay window whatever is
drawn in it, so they are `OverlaySettings`. `HideWhenInactive`, `VolumeThreshold` and the pill's
`Hidden / Muted / MutedOrTalk` mode only mean anything to a microphone, so they are `MicSettings`
even though the thing they affect is on screen. The same question settles the master notification
switch: it gates the overlay's own text pill, so it is `OverlaySettings::Notifications` rather
than a `Core` flag.

**The section structs stay in `Core/AppConfig.hpp`, declared by the kernel.** A feature declaring
its own struct and the kernel aggregating them would point the include graph the wrong way, and
the alternative - an untyped blob per module - trades the defaults and the compiler for nothing a
single-user config file needs. One file is the on-disk shape; a module still only ever *sees* its
own section, and that is the coupling that was worth removing.

### Serialization format: JSON, not BEVE

The config moves from BEVE (`conf.b`) to prettified JSON (`config.json`). Read with
`glz::read_file_json`, write with `glz::write_file_json` under `glz::opts{.prettify = true}`.

**It costs 50 KB, and the prediction that it would not was wrong.** The reasoning was that
glaze's JSON codec is already linked in - `UpdateManager` parses the GitHub releases API with
`glz::read` - so dropping BEVE would leave one codec instead of two. That is true of the shared
machinery and false of what actually dominates: glaze instantiates a reader and a writer per
type, and the config tree is far larger than the two structs `UpdateManager` parses. Measured on
MinSizeRel: 854 KB before, 904 KB after.

`read_file_jsonc` would allow comments in a hand-edited file and costs another 31 KB on top of
that. It was measured and dropped: the settings Apply rewrites the whole file, so a comment does
not survive the next save anyway, and 31 KB for a note that disappears is the wrong trade against
priority three.

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

`GetHotkeyName` already produces that string; step 1 adds the inverse in
`Core/Hotkeys/KeyNames.hpp`.
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
   - Input thread: owns the LL hooks and walks each event through the stage pipeline
     (`Core/Input`, `docs/INPUT.md`). A stage's `OnKey` is a lookup and an enqueue, nothing more;
     the hotkey table reaches it whole, through `Input::Post`. While an edit holds delivery,
     this thread also owns the hold ring and its timer.
   - Edit lane: the threadpool, reached through `Input::Edit`. A text edit runs here under a
     hold, with `Input::CurrentHold()` naming it, and answers with `Input::Post` + `Commit`.
   - Action worker: where every action body runs. Feature callbacks land here. Joined while the
     settings window is open, so an action may read and save the config.
   - UI thread: everything touching a window or the config. Reached via `Dispatcher::ToUi`.
     `Measure`, `Render` and `Tick` on an overlay layer are this thread and only this thread;
     `Overlay::Invalidate` is the one entry point that may be called from any other.
   - WASAPI notification thread: writes state through atomics only.
   - Threadpool: feature background work that must not queue behind actions on the serial worker
     (window placement, the desktop registry watch). Publishes through atomics
     or a mutex and may call `Tray::Changed`; anything that reads the config, `Feedback::Post`
     included, hops through `Dispatcher::ToUi`.
4. **Config keys are permanent.** `ActionDesc::Id` is written to disk. Renaming one is a
   migration, not an edit.
5. **A layer may not be polled for nothing.** `TickMs` runs a timer on the UI thread, and this
   app's second priority is idle footprint: a layer asks for ticks only while it has something to
   watch for, and the overlay stops the timer the moment it stops asking.

---

## 6. Settings UI

The highest-leverage part of the refactor and the one that makes Win32 bearable.

Plan: **declarative rows for settings grids, DIALOGEX only where the layout is genuinely custom.**

A module describes its page as data; a generic builder (`UI/Settings/SettingsRows`) creates the
controls with `CreateWindowExW`, lays them out in dialog units against the page's own font, and
reads/writes the bound config fields.

```cpp
static constexpr SettingsRow General[] = {
    {.Kind = RowKind::Check, .Label = L"Check for updates",
     .Field = Bind<&AppConfig::Core, &CoreSettings::Updates>(),
     .Changed = [](HWND, AppConfig& cfg) { cfg.Core.AutoUpdate = cfg.Core.AutoUpdate && cfg.Core.Updates; }},
    {.Kind = RowKind::Check, .Label = L"Enable auto-updates",
     .Field = Bind<&AppConfig::Core, &CoreSettings::AutoUpdate>(),
     .Enabled = [](const AppConfig& cfg) { return cfg.Core.Updates; }},
    {.Kind = RowKind::Slider, .Label = L"Size (px)",
     .Field = Bind<&AppConfig::Overlay, &OverlaySettings::Size>(), .Min = 10, .Max = 32,
     .Changed = [](HWND, AppConfig&) { Overlay::Changed(); }},
};
```

Row kinds: `Check`, `Combo`, `Slider`, `SoundPicker`, `Radio`, `Text`, `Group`, and `Custom` for
what is not a grid row - the action list, the log, and the Desktops panel. `Radio` arrived with the
tray page in step 11, where it was first needed. A page too rich for rows is one `Custom` row that
creates a feature-owned panel window; the panel handles its own notifications and edits the live
config, so Cancel and `Commit` still work unchanged. No row kind was added for it. A page whose
rows run past its window gets a vertical scroll bar before the rows are laid out, so they take
the narrower width; the page scrolls its children itself, wheel included.

The builder carries a **string-valued field** (`SoundPicker` binds `MicSettings::MuteSound` through
`RowField::Text`) and a **runtime item list**: `Items` is a function read when the page is built,
so a `Combo` over a fixed table and a `Radio` over a registry filled at startup are the same row.

**Hooks are what let the table hold the whole page rather than the easy half of it.** Every
control is re-read from its field after any change and written only where it differs, so a hook
edits *fields*, never controls: the updates box clears `AutoUpdate` and the auto-update row
repaints itself, a refused elevation reverts the field and its own box follows. Two hooks, because
there are two moments:

- `Changed(HWND owner, AppConfig&)` after the field is written from a click - the elevation prompt,
  the relayout, the dependent field;
- `Commit(HWND owner, AppConfig&, const AppConfig& before)` on OK - autostart into the registry,
  the UAC task into the scheduler. These must never run on a click, or Cancel stops meaning
  Cancel, which is why one hook was not enough.

Plus `Enabled(const AppConfig&)` for a row whose availability follows another field.

Effects:

- a checkbox goes from 5 edit sites to one row plus one config field;
- `Initialize*Section`, `HandleButtonClick`, `HandleComboBoxChange`, `HandleTrackbarChange` and
  `CommitPrivilegedSettings` are deleted;
- Cancel stays the whole-config snapshot it already was - there is nothing more generic to replace
  it with - and OK runs every row's `Commit` over it.

Keep as DIALOGEX: `IDD_ACTION_EDIT` and `IDD_UPDATE_DIALOG`. Their layout is not a settings grid.

### Who owns a page

**One feature is one page, registered by the feature.** A page describing itself as data is what
makes that possible at all: a DIALOGEX page means a template in the shared `Resource.rc` and ids
in the shared `Resource.h`, so a feature could not own its page without editing two files that
belong to everyone.

```cpp
struct SettingsPage {
    std::string_view Id;      // "mic" - stable, and what a child names as its parent
    std::string_view Parent;  // empty is a top-level page
    const wchar_t* Title;
    std::span<const Row> Rows;
    int Order = 100;
};
```

The sidebar is already a tree view, so a page with children costs `TVS_HASBUTTONS` and a second
insert pass - a feature that outgrows one screen nests instead of spilling into someone else's
page. Parenting is by `Id` rather than by title: a display string is not an identity, and
comparing wide text to find a parent would make renaming a page break the tree. Neither field
exists yet: they land with the first page that has a child.

What this settles, in the shape it exists today:

| Page | Owner | Was |
|---|---|---|
| General | the frame | unchanged |
| Overlay | the frame (it owns the window) | "Indicator", mixed with mic fields |
| Tray | the frame (it owns the icon) | did not exist - the icon was the microphone's by default |
| Microphone | `Features/Microphone` | split across "Sounds" and "Indicator" |
| Keyboard | `Features/Keyboard` | did not exist |
| Desktops | `Features/Desktops` | did not exist |
| Hotkeys | the frame | unchanged |
| About | the frame | unchanged |

**There is no Sounds page after this.** It was never a category - it was the microphone's chime
and level settings under a name that invited every other feature to pile in. What is actually
shared about sound is already shared: `SoundCatalog` for the bundled keys, `AppConfig::RecentSounds`
for the picker's list, the `SoundPicker` row kind for the control, and a per-binding sound on every
action through the action dialog. A feature that wants a sound of its own puts a `SoundPicker` row
on its own page and hardcodes nothing.

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

- [x] Add `Core/ActionRegistry.hpp` with `ActionDesc` / `ActionContext` / `ActionFlags`
- [x] Add `Core/Host.hpp`
- [x] Move `ActionFeedback` from `ViewModel/` to `Core/Feedback.hpp`, as `Feedback`
- [x] Create `Features/Microphone/Microphone.cpp` - registers `mic.toggle_mute`,
      `mic.push_to_talk`, `mic.volume_up`, `mic.volume_down`, `mic.toggle_bell`
- [x] Create `Features/Keyboard/Keyboard.cpp` - registers `kbd.switch_layout`
- [x] Custom commands become an action too: `launcher.run` in `Features/Launcher/Launcher.cpp`,
      with the command line as its argument. Removes the built-in/custom fork in the binding loop
- [x] Rewrite the binding loop against the registry, as `Bindings::Apply`
- [x] Delete `src/Actions.hpp`, `BuiltInId`, `BuiltInActions::All`, `MakeBuiltInHandler`
- [x] `MainWindowViewModel` keeps only indicator, tray and settings launch

The call sites that held the old table in place, found while extracting the kernel:

- [x] `SettingsWindowViewModel::AddAction` walked `BuiltInActions::All` and indexed back into it
      with `All[chosen - 2]`. It walks the registry now; the id is a position in that vector, read
      back in the same call, and a separator goes in wherever `Group` changes
- [x] `SettingsWindowViewModel::EditAction` read `ArgsLabel`, `ArgsHint`, `DefaultNotification`,
      `HasSound` and `HoldOnly` off `BuiltInActions::Find`. `HasSound` inverted into
      `ActionFlags::NoSound`; `HoldOnly` became `MakeRelease != nullptr`
- [x] `AppConfig.hpp` no longer includes anything that knows the action table
- [x] `_prevBellVolume` moved into `Features/Microphone` alongside `mic.toggle_bell`
- [x] Modules register in `main` before the window exists, so the registry is full before
      `RestoreConfig` resolves anything - an empty one would drop every binding silently
- [x] The fork reached the settings list too, through `action.BuiltIn.empty()`. The last column
      is the action's argument always. The row flag survives as `RunsCommand`, read off the
      action's own flags: it drives a tint that says "this row launches a command line", which is
      still true and still worth showing
- [x] `AddAction` seeds `Sound` from `ActionDesc::DefaultSound`
- [x] `MakeBuiltInHandler` was a `switch` with no default, so a missing case was a warning.
      `ActionDesc::Make` is stronger - an action cannot be registered without its factory - and
      the `"Built-in action '%s' has no handler"` path is gone rather than ported
- **Done when:** `MainWindowViewModel.hpp` is under 250 lines, `src/Actions.hpp` is gone, and
  adding a new action means editing exactly one file inside one `Features/` folder.
- **Result:** two of the three met. `Actions.hpp` is gone and an action is one entry in one
  module file. `MainWindowViewModel.hpp` went 511 -> 330 lines, not under 250: what is left is
  the indicator itself (layout, anchor, notification pill, peak meter), the tray, the settings
  window and `RestoreConfig`. None of it is microphone state any more. Getting under 250 means
  moving the indicator out, which is open question 10, not this step. Binary unchanged at 854 KB.

  Three things the plan did not have. The namespace is `Mic`, not `Microphone` - the Windows SDK
  claims that name in `mmdeviceapi.h`. `Core/Bindings.hpp` exists because the binding loop is
  kernel work and belonged in neither the view model nor a feature. And the module owns the
  `AudioManager` outright, so `main` no longer creates one and `Host` never had to carry it.

**Steps 2 and 3 have to ship together.** `Action::BuiltIn` stored a title (`"Toggle mute"`) and
`Action::ActionId` stores an id (`"mic.toggle_mute"`); `Action::Command` is gone and a command
line lives in `Args`. `AppConfig::Version` cannot police either change - `Load` stamps the field
on the way in and never compares it, so bumping `CurrentVersion` rejects nothing. The only thing
that actually breaks the old file is the step 3 filename change, `conf.b` -> `config.json`: the
new binary does not find it and starts clean. Until that lands, this branch reads a pre-refactor
config, fails every `Find`, and comes up with every binding quietly unbound - fine on a branch,
not fine in a release.

### Step 3 - Config split

**No migration.** Pre-refactor config files are discarded, not converted. The user reconfigures
their bindings once.

- [x] Split `AppConfig` into `CoreSettings`, `MicSettings`, `IndicatorSettings` + `Bindings`
- [x] Replace `Action` with `Binding`, hotkey fields nested under `Trigger`
- [x] Swap BEVE for JSON: `read_file_json` / `write_file_json` with `.prettify = true`,
      `CONFIG_NAME` `conf.b` -> `config.json`. No `*_beve` call site remains
- [x] `Trigger` stores `Keys` as a display-name string, not a packed mask; converted through
      `KeyNames::Format` / `KeyNames::Parse` at the two boundaries that need a mask - the action
      dialog and `Bindings::Apply`. An unparseable name means an unbound trigger, not a dropped
      binding. `ClearHotkey` compares the names directly, which is sound because every stored
      name came out of `Format` and is therefore canonical
- [x] Bump `CurrentVersion` 2 -> 3
- [x] On load, a `Version` that is not the current one means the whole file is ignored and
      defaults are used. One `if`, no mapping tables. It is checked *before* the fields are used:
      the file is read into a scratch config and only moved across once the revision matches
- [x] `IndicatorState` gets a `glz::meta` so it is written by name - "2" in a file whose point is
      being hand-editable is not much better than BEVE
- [x] Each module takes its own settings struct by reference, never the whole `AppConfig`. The
      microphone keeps a second pointer to the whole thing purely to persist a bell toggle, since
      writing the file is not any one section's business
- **Done when:** a config written by 1.3.0.0 is ignored cleanly - app starts with defaults, no
      error dialog, no half-populated action list - and `config.json` is legible enough to
      hand-edit a binding and have the app pick it up on restart.
- **Result:** met. Checked with a standalone round-trip harness against the real header, 8 for 8:
  identity across save and load, the version stamped on the way out, the enum surviving as
  `"MutedOrTalk"`, the trigger nested, the file prettified, the combination stored as
  `"CTRL + SHIFT + M"`, and a `Version: 2` file falling back to defaults whole. The app starts
  with the same footprint as the step 0 baseline - 10 threads, 22.6 MB. Binary 854 -> 904 KB;
  see the serialization note in section 4 for where that went and why the earlier estimate of
  zero was wrong.

  The struct is still called `AppConfig` rather than `Config`. The rename buys nothing, and
  `Config` is a name that collides with every other meaning of the word in a Win32 codebase.

### Step 4 - Folder layout

- [x] `git mv` into `Core/ Platform/ Features/ UI/` per section 3
- [x] Update `target_include_directories` in `CMakeLists.txt`
- [x] Add the slice-rule grep to `build.ps1`: fail the build if `Features/X` includes `Features/Y`
- **Done when:** the tree matches section 3 and the slice check passes.
- **Result:** met, binary unchanged at 904 KB. The include path is `src`, `src/Core`,
  `src/Platform`, `src/UI` - deliberately **not** `src/Features`, so reaching into another
  feature has to be spelled `"Features/X/..."` and the slice check has something to grep for. A
  file inside its own slice still says `"CommandRunner.hpp"`, which a quoted include resolves
  relative to the includer.

  The check was tested both ways: it passes on the tree as it stands, and an `#include
  "Features/Launcher/Launcher.hpp"` added to `Features/Keyboard/Keyboard.cpp` fails the build
  with the file and line.

  Two deviations from the sketch in section 3. `Platform/` is flat rather than carrying
  `Uac/ Shell/ Crash/ Update/ Version/` - those are one or two files each, and a folder per file
  is filing, not structure; only `UIAccess/` stayed a folder because it is one. And `UI/` is flat
  apart from `Settings/`, since `Core/` and `Components/` were splitting eight files four ways.

### Step 5 - Settings page registry

- [x] Add `Core/SettingsHost.hpp` - modules contribute `{title, build}` pages
- [x] `SettingsWindow` walks the registry instead of the static `Categories[]`
- [x] `HandleSectionChange` switch replaced by the page's own `Build`
- **Done when:** `SettingsWindow.cpp` has no `IDD_SETTINGS_*` knowledge beyond the frame itself.
- **Result:** met. The tree view carries a page index rather than a template id, and the only
  `IDD_SETTINGS_*` left in the window is `IDD_SETTINGS_MAIN`, the frame.

  Ordering is a sort key, which settles open question 10: `SettingsHost::First` and
  `SettingsHost::Last` pin General and About, everything else keeps registration order at the
  default key, and `main` registers the frame's pages before the modules so a module's page lands
  after Hotkeys and ahead of About.

  `SettingsPage::Build` is a plain function pointer, so a page cannot capture a view model that
  is created per window. The frame's pages reach the live one through a file-static `_current`,
  which is sound because `MainWindowViewModel::OpenSettings` refuses to make a second window.

  **No module contributes a page yet**, and the Sounds page - which is microphone settings
  through and through - is still registered by the frame. Moving it would split it in half: its
  builder would go to `Features/Microphone` while its five `IDC_SETTINGS_SOUNDS_*` cases stayed
  behind in the view model's switches. That is step 6's job, and until then a whole page is
  better than half of one in each place.

### Step 6 - Declarative settings rows

- [x] Build the row builder in `UI/Settings/` - `Check`, `Combo`, `Slider`, `Text`, `SoundPicker`,
      plus `Group` and `Custom`, with dialog-unit layout and config binding
- [x] A row carries `Changed` and `Commit` hooks, so the cases that are not a plain field
      assignment land in the table with their row instead of staying behind in a switch
- [x] Port General, Indicator, Sounds, Hotkeys and About pages to row tables
- [x] Delete the ported `IDD_SETTINGS_*` templates from `Resource.rc` and their ids from `Resource.h`
- [x] Delete `Initialize*Section` and the settings half of `HandleButtonClick`
- [x] Apply/Cancel generic over the rows - the `_cfgPrev` snapshot stays, see Result
- [x] Keep `IDD_ACTION_EDIT` and `IDD_UPDATE_DIALOG` as DIALOGEX
- **Done when:** adding a settings checkbox is one row plus one config field.
- **Result:** met. The row contract (`SettingsRow`, `RowField`, `Bind`) is in
  `Core/SettingsHost.hpp`, because step 9 declares pages from inside a feature; the builder is
  `UI/Settings/SettingsRows`. Every settings page is one empty `IDD_SETTINGS_PAGE` filled by its
  rows, the window forwards the page's WM_COMMAND and WM_HSCROLL through one `OnPageInput`, and
  the view model routes them to the open page's rows. `SettingsWindowViewModel.cpp` went from 556
  lines to 395, and the binary grew 2.5 KB.

  It was done whole, as the note left here asked: a binding table over the controls the
  templates already placed would have left the elevation prompts, the updates box, the size
  relayout and autostart as switch cases beside it. Three departures from the plan, all deliberate:

  - **Two hooks, not one `Apply`.** Autostart and the UAC task reach the OS on OK and never on a
    click. A single click-time hook could not say that without Cancel leaving the registry changed.
  - **`_cfgPrev` stays.** The checklist asked to drop the copy-and-diff; the copy turned out to be
    the generic mechanism already - Cancel is one assignment - and what actually went was the
    per-field diff in `CommitPrivilegedSettings`, which became two rows' `Commit`.
  - **About and Hotkeys are rows too**, through `Custom`: section 6 keeps only the action and
    update dialogs as DIALOGEX. The list's WM_NOTIFY and the log's WM_LOG_REFRESH stay in the page
    proc, which knows those two controls by their fixed ids.

  The rows take their width from the page rather than a hardcoded 240: the templates were wider
  than the group box interior, and the Hotkeys list and slider used to run past its right edge.

  Checked on screen, as this step asked for: all five pages captured with `PrintWindow` from the
  DIALOGEX build and from this one and compared side by side, then the General and Indicator pages
  driven by control id - updates off clears and disables auto-update, a pending edit survives
  leaving the page, the threshold slider holds 29, 57, 58 and 59 mid-drag, Cancel writes nothing.

  What the passes caught. Before implementation, on the design: a sync that compares a picker's
  selection with its field misses a list gone stale under it, and since a sound combo maps its
  selection back to `RecentSounds` by position, a stale list resolves a later pick to the wrong
  file - the sync compares the item count too. After it, two lanes independently: the threshold
  row read its float back truncated, `0.29f * 100` came out 28, and the sync dragged the slider
  back mid-drag - now rounded. The log subscription outliving the About page it posts to was
  older than this step and is closed with it. Quality removed a `force` flag the diffing sync
  never needed and a second way of reading a control id. A re-check pass over those four fixes
  found nothing.

### Step 7 - Group Core, and free the drawing helpers

- [x] `git mv` `HotkeyService.*`, `KeyNames.*`, `HotkeyCapture.hpp` and `Bindings.hpp` into
      `Core/Hotkeys/`. Six of Core's sixteen files are one subject, and the rest are not
- [x] `UI/GdiRenderer.hpp` -> `Platform/Gdi.hpp`, and give it what a pill needs: the rounded path
      it already has, plus `PillFont`, `MeasureText` and `DrawCentred` lifted out of
      `IndicatorLayout`. A feature that draws must not have to include `UI/`
- [x] `UI/TrayIconTheme.hpp` -> `Platform/`: taskbar theme detection and pixel darkening know
      nothing about a tray, and step 11's provider needs them from a feature
- [x] No new include directories: a file inside `Core/` says `"Hotkeys/KeyNames.hpp"`, everyone
      else says `"Core/Hotkeys/KeyNames.hpp"` - the same convention the slices already use
- [x] Extend the `build.ps1` check next to the slice rule: no file under `Features/` may include a
      header that lives under `src/UI`. It holds today by convention only, and `src/UI` is on the
      include path, so `#include "MainWindow.hpp"` from a feature compiles - match on the basename
      of every file under `src/UI`, which is what makes the unqualified form catchable
- **Done when:** the tree matches section 3, the build is byte-identical, and the new check fails
  on a deliberately added `UI/` include from a feature.
- **Result:** met, with one honest correction to "byte-identical". The exe is the same size to
  the byte - 927,744, 906 KB - but not the same bytes: `.text` grew 64, `.rdata` 24 and `.pdata`
  12, which is one more out-of-line function (`Gdi::DrawCentred`, which used to be inlined code
  inside `IndicatorLayout::Render`). All three fit inside the existing file alignment, so the
  size did not move. The hash differs for a second reason as well - link order follows the object
  paths, and the move changed them. App starts at the step 0 footprint, 10 threads and 22.6 MB.

  Two things the move decided that the plan did not spell out. `namespace GDIRenderer` would
  have been a namespace named after a deleted file, so `LayeredWindow.hpp` owns its own name now
  - `LayeredWindow::Render`, `LayeredWindow::Surface` - and `RenderContext` and `RenderCallback`
  moved there with it, since the layered window is the only thing that has ever produced one.
  `IndicatorLayout::Render` takes a `Gdiplus::Graphics&` rather than that `RenderContext`: it
  only ever used the canvas, and that is what lets a layout stop knowing a `UI/` type at all -
  the same shape `OverlayCanvas::Canvas` needs in step 8.

  The new check is tested on five deliberate violations from `Features/Keyboard`: an unqualified
  `"MainWindow.hpp"`, a qualified `"UI/MainWindow.hpp"`, an angle-bracket `<UI/MainWindow.hpp>`,
  a cross-slice `"Features/Launcher/Launcher.hpp"` and a relative `"../Launcher/Launcher.hpp"`.
  All five fail the build with the file and the line, and both rules are one pass over the
  feature files instead of two.

  **What the review pass changed.** The last two of those five were holes the first version of
  the check had - it read only quoted includes and only the literal `Features/X/` prefix, so
  angle brackets and a `..` walk out of the slice both passed. Two reviewers found it
  independently, which is the argument for running them. The check resolves the path now and the
  redundant `UI/` pattern match went with the rewrite.

  Three real leaks the grouping exposed, all of them dead weight the old flat `Core/` hid.
  `Feedback.hpp` included `Hotkeys/HotkeyService.hpp` and never used it. `Feedback::Compose`
  took a `uint64_t` mask purely to call `KeyNames::Format`, so it now takes the formatted name
  from its one caller, `Bindings::Apply`, which already holds the mask - `Feedback` includes
  nothing from `Hotkeys/` at all any more, which is what the folder was supposed to make
  visible. The price is that `Format` is no longer lazy: it runs per binding at Apply time
  instead of only when `{key}` appears, which is a string build on config restore, not on any
  hot path. `MainWindowViewModel.hpp` included `Core/Dispatcher.hpp` unused, and
  `TrayIconTheme.hpp` was included by `MainWindow.hpp` for a call that only `MainWindow.cpp`
  makes.

  `Gdi::PillFont` became `Gdi::TextFont`: `Platform/` has no business naming a shape that
  belongs to the overlay, and it is the one font the app draws text with. `MeasureText` also
  scopes its `Graphics` so it dies before the DC it was built on - the old code in
  `IndicatorLayout` released the DC first, which is why that one is a fix rather than a move.

  Two findings were rejected, recorded so they are not re-litigated. "Delete `DrawCentred`, it
  has one caller" - it is the reason the file moved at all: step 8's notification layer and step
  10's keyboard layer both draw centred text, and a feature may not include `UI/`. And "derive
  the corner radius from the rect height and delete `CornerRadius`" - that changes pixels. The
  pill is 32 high at the default size with a radius of 10, so its ends are rounded and not
  semicircular; step 7 may not change what is drawn, and step 8 moves the radius into the
  overlay, which is where the look belongs.

### Step 8 - The overlay becomes a surface

- [x] Add `Core/Overlay.hpp` per section 4: the layer registry, `OverlayCell`, `Invalidate` as a
      bound function pointer, the `Order` sort key with `Last`
- [x] `UI/Overlay/OverlaySurface.hpp` takes the anchor, the layout pass, the visibility decision
      and the paint from `MainWindowViewModel`, and holds `OverlaySettings&` alone.
      `IndicatorLayout` -> `UI/Overlay/OverlaySlots.hpp`, measuring N slots instead of the two it
      has hardcoded. `MainWindow` keeps its name and its frame jobs - HWND, tray, message
      routing, `Dispatcher::BindUi` - and keeps `const AppConfig&` for the two things it still
      owns: the initial window size and the bell menu item, both of which are step 11's
- [x] The notification text becomes the overlay's own layer, pinned `Last`. `Feedback::Post` keeps
      its `PostFn` and stops being the reason the view model holds a notification timer
- [x] `Features/Microphone` takes the mic pill, the peak meter and `IndicatorIcons` - as
      `MicLayer` and `MicIcons`, out of `MainWindowViewModel` entirely. The layer's state is a
      file static set in `Register`, which is how the module already holds `_settings` and `_fb`
- [x] Split `IndicatorSettings` into `OverlaySettings` and the three fields that were always the
      microphone's; move `Notifications` out of `CoreSettings`; bump `CurrentVersion` 3 -> 4
- [x] Delete the phantom window: the peak timer runs off the layer's `WantsTick`, so "muted or
      talking" no longer keeps an empty window on screen to host a timer
- **Done when:** nothing under `UI/Overlay/` includes anything microphone-shaped, the view model
  is under 250 lines (step 2's unmet target), and the mic pill, the peak meter, the notification
  pill and the drag-to-position preview all behave as they do today.

  The tray is deliberately *not* part of this step: the icon and the bell menu item still reach
  into `Mic::` from the view model and the window afterwards. That is step 11, and it is the only
  feature knowledge left above `Features/` once step 8 lands.

  **What the design review changed, before any of it was written.** The plan said
  `MainWindow` -> `UI/Overlay/OverlayWindow.*`. That rename is a lie: the same object owns the
  tray icon, the message routing and `Dispatcher::BindUi`, and an overlay window that owns the
  app's message loop is the old god object with a new name. Splitting it in two costs a second
  window class and a second HWND and buys a second answer to "who does the worker post to". So
  the window keeps its name and the *surface* is what moves - see section 4. The second change
  was `OverlayCell`, which split in two rather than documenting which of its fields are lies in
  which phase. The post-change review then deleted that split again - see section 4.

  **Result.** `Core/Overlay.hpp` is 79 lines, `UI/Overlay/` is three files and 353 lines,
  `MainWindowViewModel` is 151 (step 2 asked for under 250), and nothing under `UI/Overlay/`
  contains the string `mic` in any spelling. The mic pill, the peak meter, the notification pill
  and the drag-to-position preview all still come out of the same formulas - see the harness
  below.

  The binary went 927,744 -> 931,328 bytes: `.text` +2,896, `.rdata` +786, the rest unwind data
  and alignment. That is the registry, the N-slot pass and one timer per layer where there used
  to be two fixed slots and one timer, less `IndicatorLayout::Compute` and the bodies the view
  model no longer has. Accepted rather than argued away: `MaxLayers` is a compile-time bound, so
  the `std::vector` could be a `std::array` and give back most of `.text`, and that is not worth
  spending the clarity of a one-line `Add` on to save 3 KB in a 910 KB binary.

  **Verified against a harness, because this machine has no capture device.** `slots_test.cpp`
  embeds the deleted `IndicatorLayout::Compute` formulas verbatim and runs them next to
  `OverlaySlots::Measure` with fake layers: 102 parity checks over sizes 10/16/32 x four
  notification texts x mic pill on and off, comparing total width, height, text x, text width and
  font size, plus the square floor, `WantsTick` at width 0, no window when nothing draws, no gap
  for a hidden middle layer, and three-pill placement. All pass. The one intended divergence is
  the last checkbox: nothing to draw is now width 0 and a hidden window, where it used to be one
  empty pill.

  Two deliberate behaviour changes beyond that. The phantom window is gone, and a relayout now
  arrives as a posted `WM_OVERLAY_RELAYOUT` instead of running inline - `Overlay::Invalidate` is
  callable from any thread, and a layer that noticed something must not paint from wherever it
  noticed.

  **What the post-change reviews caught.** One bug, and it was this step breaking its own
  invariant. With the phantom window gone, `Relayout` returns early to hide the overlay *before*
  re-seeding the window position from the anchor, and `RefreshPos` has already written its
  work-area clamp into that position. The next pass reads the position back as the anchor - it is
  allowed to, while the strip is no wider than one pill - and adopts the clamp. Modelled against
  the real clamp formula: from x=1850 on a 1920 work area, one 200-wide notification moved the
  stored anchor to 1682 for good, and every wider notification moved it again. The fix parks the
  position on the anchor before hiding. The old code could not reach this, because it never hid
  the window.

  The quality pass took out what the step had left behind or built without need:
  `OverlayCanvas`, `Overlay::First` (nothing used it), a stray `<sys/stat.h>` in `AppConfig.hpp`,
  a `Relayout` doc comment still claiming the view model lays the overlay out, and the surface's
  11-line class block. It also caught the frame calling `StopWatchingForCaptureSessions` on the
  microphone's own device manager, which became `Mic::Suspend()` / `Mic::Resume()` and, in step
  11, the module's own `Lifecycle` handlers. Rejected:
  replacing the sorted insert with `push_back` and deleting `Order` outright. The text pill is
  last today only because the `OverlaySurface` constructor happens to run after the module loop
  in `main.cpp` - a coupling between two files that nothing enforces, and step 10 adds a third
  layer from a third place.

  The re-check pass over the fixes found no new defect, and raised one claim about the design
  instead: with `_mainWindow->Show()` gone, the surface needs *something* on screen while the
  settings window is up so the user has a drag target, and whether that happens is decided by
  feature layers it knows nothing about - `TextLayer`, the only layer the surface owns, measures
  0 when there is no text, in preview as well. Judged and left alone. Every fix costs more than
  it buys: inflating `TextLayer` in preview puts a second pill under the cursor and the user ends
  up positioning a strip wider than the one they will actually see, and flooring the width in the
  surface brings back an invisible window to drag, which is the phantom this step deleted. It
  needs the mic module removed from `Modules[]` to happen at all, and step 10 adds a second layer
  that shows in preview. The `Size: 0` route to the same place is in docs/known-bugs.md - it
  predates this step.

  The architecture pass found the two places that still disagreed about ownership.
  `SettingsWindowViewModel::Init` called `_mainWindow->Show()`, which is a second answer to
  whether the overlay is on screen now that the preview relayout gives one. And `Feedback` still
  took an `HWND` in its `PostFn` and stored it, while `Overlay::Invalidate` is a `void (*)()`
  with no window in it anywhere - two editions of the same Core -> UI idiom. The handle is now
  one static on `MainWindow` that both posters read, `OverlaySurface` no longer keeps a copy, and
  `Overlay::Invalidate = &MainWindow::PostRelayout` binds with nothing in between. `Feedback`
  keeps the `HINSTANCE` it plays sounds with, which is the rule the fix leaves behind: the kernel
  may hold a handle it uses, never one it only hands back. Rejected: folding `ActionRegistry`,
  `SettingsHost` and `Overlay` into one generic registry - `ActionRegistry` has a recorded reason
  to keep registration order instead of a sort key, and the saving is a handful of lines.

### Step 9 - Feature-owned settings pages

Needs step 6: a feature cannot own a page while a page means a template in the shared `Resource.rc`.

- [ ] ~~`SettingsPage` gains `Id` and `Parent`; the tree view gains `TVS_HASBUTTONS` and a second
      insert pass for children~~ - deferred, see Result
- [x] `Features/Microphone` registers the Microphone page - level, keep level, chime volume,
      mute and unmute sounds, pill mode, threshold, hide when inactive
- [x] The frame's Indicator page becomes Overlay: size, on top, exclude from capture, notifications
- [x] The Sounds page is deleted, not moved (see section 6)
- [ ] ~~`Features/Keyboard` registers a Keyboard page~~ - moved to step 10, see Result
- **Done when:** `SettingsWindowViewModel` holds no field of any feature's settings section, and
  the Microphone page is registered from inside `Features/Microphone`.
- **Result:** met. The Microphone page is a row table next to `Actions[]` in `Microphone.cpp` and
  one `SettingsHost::AddPage` in `Mic::Register`; the rows moved verbatim from Sounds and
  Indicator, hooks and all. `SettingsWindowViewModel.cpp` no longer includes anything from
  `Features/` and names no `cfg.Mic` field. Sidebar: General, Overlay, Hotkeys, Microphone, About.
  Binary unchanged at 933,888 bytes.

  The page is flat. The interior is about 159 dialog units tall; the eight rows take 132, and the
  two group boxes the Sounds page had would have made it 170 and clipped the last row. The labels
  changed with it, since two rows called "Volume" only read right inside their groups.

  Two items did not happen, both because nothing would use them yet:

  - **`Id`, `Parent` and the child pass.** No page has a child - the Microphone page fits one
    screen. They arrive with the first page that nests, the rule `Radio` already follows.
  - **The Keyboard page.** `Features/Keyboard` has no settings field until step 10's
    `ShowLayout`, so a page now would be a sidebar entry with nothing on it. Step 10 registers it
    with its first row; the Microphone page already proves a feature can own one.

  Checked on screen: all five pages captured before and after and compared, the Microphone rows fit
  without clipping, and driven by control id the pill combo reads its field, the threshold holds
  29, 57, 58 and 59 mid-drag, and Cancel writes nothing. The design went through an architecture
  pass before any code, and the change through bug-hunt, quality and architecture afterwards - no
  confirmed findings in any of the four.

### Step 10 - The proof: the layout pill

Not a refactor step - the acceptance test for steps 8 and 9. Show the current keyboard layout on
the overlay, and count what it costs.

- [x] `Features/Keyboard/LayoutLayer.cpp`: `Measure` returns the width of the layout's two letters
      or 0 when the setting is off, `Render` draws them, `TickMs` watches the foreground window's
      layout (since replaced by WinEvents, see the result below)
- [x] One `Check` row on the Keyboard page, one `ShowLayout` field in `KeyboardSettings`
- [x] `Features/Keyboard` registers that Keyboard page - moved here from step 9, which had no row
      to put on it
- **Done when:** the feature is one new file, one row, and one config field, plus the page
  registration its first row needs. The only thing it may
  touch outside its own slice is the section struct in `Core/AppConfig.hpp` - that is the known
  price of a typed config in one file. Nothing under `UI/`, nothing in another slice, and no line
  of the overlay. If it costs more than that, the design in sections 4 and 6 is wrong and this is
  where it shows.
- **Result:** met, with the file count stated plainly. `LayoutLayer` is a header and a .cpp - the
  pair `MicLayer` already is, because `Keyboard.cpp` calls it - plus one `Check` row and
  `KeyboardSettings::ShowLayout`. The page registration step 9 handed over is one `AddPage` line
  in `Keyboard::Register`. Outside the slice it touched the section struct in `Core/AppConfig.hpp`
  and nothing else: nothing under `UI/`, no other slice, no line of the overlay. Sections 4 and 6
  held. Binary 933,888 -> 937,984 bytes.

  No revision bump: glaze reads a v4 file without the new key and keeps the default, so adding a
  section discards nothing.

  Windows sends no notice when another app's thread changes layout (`HSHELL_LANGUAGE` never
  arrives on 24H2), so the layer first polled the foreground window's thread every 200 ms. Later
  it went event-driven, with no timer: while the setting is on, two out-of-context WinEvent
  hooks re-read the focused thread's layout. One fires on `EVENT_SYSTEM_FOREGROUND`, the other
  on `EVENT_OBJECT_CREATE`, because a switching thread rebuilds its `MSCTFIME UI` window once
  the new layout is in, about 1 ms after the request. The layer reads the focused window, not
  the foreground one: focus can sit in another process (an embedded browser), and that is the
  thread the switch changes. A switch made through `kbd.switch_layout` is shown at once through
  `LayoutLayer::Requested`.

  Checked on screen with nobody at the desk. In the settings preview the row widens the strip from
  32 to 81 px and the capture shows the mic glyph beside "RU". After OK, with no capture device on
  this machine, the layout pill stands alone at 41 px. A probe-owned foreground window switched
  through en-US, ru-RU and uk-UA, and the captures read EN, RU, UK.

  Reviews: bug-hunt and architecture found nothing. Quality replaced a hand-written `towupper` loop
  with `CharUpperW` on the buffer `GetLocaleInfoW` had just filled; a re-check of that fix found
  nothing.

### Step 11 - The tray gets an owner the user picks

Needs step 8 for the surface split and step 9 for the `Radio` row. The icon is one slot, so the
exclusivity is real and the user is the one who resolves it - see section 4.

- [x] Add `Core/Tray.hpp`: the provider registry, `Refresh` as a bound function pointer, the
      `Order` sort key
- [x] The frame registers `"tray.app"` - `IDI_APP`, `APP_NAME` as the tooltip - pinned `Last`, so
      it is a choice on the radio and the fallback when the stored `Id` resolves to nothing. An
      empty `Provider` takes the first by `Order`, which keeps a fresh install on mic state
- [x] `Features/Microphone` registers `"mic.state"`: the muted glyph, the
      `APP_NAME - <device> [nn%]` tooltip, `RefreshTheme` behind `ThemeChanged`, and the bell
      toggle as a menu contribution
- [x] `MicIcons::Tray` and its darkened copies feed the provider - the move itself was step 8,
      this step only points them at the tray. `TrayIconTheme` is generic, so it lands in
      `Platform/` in step 7 and the provider uses it from there
- [x] Tray page: one `Radio` row over the providers that carry an `Icon`, bound to
      `TraySettings::Provider`
- [x] The menu is built from the contributions, Settings first and Exit last;
      `ID_APP_TOGGLE_BELL` leaves `Resource.h`, the view model's switch and
      `MainWindow::ShowTrayContextMenu`'s `_config` read
- [ ] ~~`TraySettings` joins the step 8 revision bump if they ship together, otherwise its own~~ -
      no bump, see Result
- **Done when:** `MainWindow` holds no `AppConfig` reference at all, the view model has no `Mic::`
  reference, and picking the tray provider in settings changes what the icon shows.

  What it buys, and the reason the choice is the user's rather than a priority nobody can see:
  after step 10 the keyboard layout becomes a candidate for that radio at the cost of one more
  registration inside `Features/Keyboard` - two letters drawn into a 16px icon, and not a line
  anywhere else.

- **Result:** met. `MainWindow` holds no `AppConfig`: its constructor takes the instance, and the
  window opens at the position and size the overlay surface seeds into its view. The view model
  names no `Mic::` - it raises `Lifecycle::Suspend` and `Restore` from `Core/Lifecycle.hpp`, the
  microphone subscribes, and `Mic::Suspend`, `Resume`, `TrayIcon`, `RefreshTheme` and `ToggleBell`
  are gone from its header. The menu reads Settings, the bell item, Exit, and a contribution's
  command id is `TrayMenuFirst` plus its provider's index. Binary 937,984 -> 946,176 bytes.

  No revision bump, for the reason step 10 gave: a v4 file without `Tray` keeps the default, and an
  empty `Provider` is exactly the "nobody has chosen yet" case.

  Two changes to the row builder came with `Radio`. `Items` is a function read when the page is
  built, because the tray list is a registry filled at startup and a `constexpr` table cannot hold
  it. Row ids moved from one per row to a block of 16, so every radio button has its own id - the
  sound picker's browse button is the block's second id, choice k its k-th. Sixteen choices is the
  ceiling, and there are two.

  `RestoreConfig` raises `Lifecycle::Restore` before `_overlay.Restore()`, not after as the design
  pass suggested: the microphone recounts its capture sessions there, and the relayout reads that
  count.

  Checked with nobody at the desk. A standalone harness against the real `Core/Tray.hpp` passed 12
  of 12 on `Owner` and `Choices`: empty takes the first by `Order`, an unknown id takes the app
  icon, a menu-only provider is not a choice. A probe read the popup through `MN_GETHMENU`, sent
  the bell command and watched `BellVolume` go to 0 and the label flip, then clicked the app icon
  on the Tray page and saw `"Provider": "tray.app"` written on OK, kept across a restart, and left
  alone on Cancel. What it could not see is the icon itself: it sits in the hidden overflow, and UI
  Automation does not reach the taskbar's XAML.

  Reviews: architecture on the design before any code, bug-hunt, quality and architecture on the
  change, and a re-check on every fix until one came back empty. Every confirmed defect was in
  `MicLayer`, and none came from this step's own lines - the first two sat on the restore path it
  rewired, the third came from step 8, the fourth is as old as the peak meter:

  - The first relayout after settings close measured the pill from the old bitmap, because the
    microphone's state event is posted and the relayout is not. The module's `Restore` handler now
    calls `MicLayer::Refresh()` itself.
  - A repaint mid-word dropped the talking glyph, and the tick would not put it back until the
    debounce ran out. `_refresh` keeps it while the mic is unmuted and the debounce still counts.
  - The peak meter ran under the muted glyph and could replace it with "talking". The old code ran
    the timer only while unmuted in "muted or talking"; step 8 has the surface tick a layer while it
    has width. The tick now checks the same `_listening()` that asks for ticks.
  - The debounce counted quiet ticks since the first loud one rather than in a row, so
    loud-quiet-loud-quiet dropped the glyph mid-sentence. Every loud tick restarts it now.

  None of the four could be run here: with no capture device the talking path was checked against
  the old `OnTimerTick` by reading, not on screen.

### Step 12 - Virtual desktops

Not a migration step: the first feature written on the finished architecture, and the test of
"one folder plus one line in `Modules[]`".

- [x] `VirtualDesktops.*`: the undocumented shell COM (24H2 layout, build gate 26100..26999),
      reconnect once after explorer restarts. The only file that knows those GUIDs
- [x] Actions `vd.switch`, `vd.move_window`, `vd.move_window_follow` (arg: number, name, `next`,
      `prev`), `vd.pin_window`, `vd.apply`, `vd.remember_window`
- [x] `DesktopSettings`: presets by desktop position, window rules (exe, frame, maximized),
      `Watch`, `Follow`, `ApplyOnStartup`, `AnnounceSwitch`
- [x] `Placer.*`: shell hook on a message-only window, placement on the threadpool; an app's k-th
      open window takes its k-th rule
- [x] Desktops page: one `Custom` row hosting a feature-owned panel (`Page.*`, `Preview.*`). The
      settings window grew to 400x240 DLU
- [x] Fullscreen tile editor: `Editor/Tiles.*` pure geometry, `Editor/Editor.*` modal layered
      window, `Painter.*` shared with the preview
- [x] `Tracker.*`: registry notification on the VirtualDesktops key -> overlay pill with the
      name, tray provider with the number

- **Result:** met. Outside the slice: the `Modules[]` line, the config section, `runtimeobject`
  and `dwmapi` linked, `LayeredWindow.hpp` moved from `UI/` to `Platform/`, `Controls.hpp` taken
  out of `SettingsRows`, the settings window size. No new row kind, no `Core/` change. MinSizeRel
  1013 KB uncompressed, ~20 KB of the growth is glaze for the new config.

  Checked on this machine: live probes (switch, move, pin, a new window placed by the watcher,
  maximize and restore), the tile geometry harness (11 cases), page screenshots. Not checked by
  hand: focus after `vd.move_window_follow`, taskbar flashing after a switch, the pill and the
  tray icon at runtime.

  Reviews caught: remember overwrote the app's first rule for its second window (now: append
  while the desktop has fewer rules than open windows, else update the nearest); Delete erased a
  preset and shifted the later ones onto the wrong desktops (now: clear it, trim the empty tail);
  Alt+F4 destroyed the editor under its own modal loop, which then never ended; an edge already
  on a snap guide jumped off it, a 0 shift read as "no guide"; overlapping registry callbacks
  raced on the last seen desktop (serialised); the switch pill read the config from the
  threadpool (now hops through `Dispatcher::ToUi`); the editor copied every tile per paint; dead
  public accessors.

  Found by hand: Qt apps (Telegram) keep a new window app-cloaked for about a second after the
  shell announces it, so the placer skipped it. It now matches the exe first and waits up to 3 s
  for the window to become an app window.

### Steps 13-16 - Input pipeline and layout conversion

Steps 13 and 14 are done, 15-16 are open. The design, the decisions and the checklist live in
`docs/INPUT.md` and move here once all four land.

---

### What the reviews caught

Each step was put through a recall-then-verify review pass. Recording the classes of defect
rather than the individual fixes, because the pattern is what the next step should watch for:

- **Behaviour that was preserved in the letter and lost in the order.** Moving the microphone
  state into a module and the device callbacks onto the UI thread changed *when* things ran. Two
  of the three real defects were ordering: a chime that read the mute state when its hop ran
  instead of when the event arrived, and a volume published before the adjust that was supposed
  to set it. Both compiled, both looked right, and neither was visible without walking the old
  call order next to the new one.
- **Lifetimes that the old shape happened to get right.** `AudioManager` moved from a `WinMain`
  static to a namespace-scope one and started outliving the config its callbacks read. A captured
  command's callback started holding a `Feedback*` where the old code had held an `HWND` by value.
  Neither was a decision; both were what the move did by default.
- **Comments that documented the intent rather than the code.** Two findings were the doc comment
  and the implementation disagreeing after an edit - which is the cheapest kind to find and the
  easiest to leave behind.
- **An invariant only the new shape could break.** Nothing about "the anchor is the user's"
  changed in step 8, but deleting the phantom window added an exit path that returns before the
  position is re-seeded from the anchor, and the clamp the previous pass wrote stayed in it. The
  guard that reads "while the strip is no wider than one pill, the window is at the anchor" was
  true only because the window was never hidden. A step that deletes a state has to be read
  against every guard that was true because that state existed. Step 11 met the same shape from
  the other side: the surface started ticking a layer whenever it has width, and the peak meter,
  which had only ever run on a live mic, never had to ask whether the mic was muted.
- **A rule enforced against the spelling it was written for.** Step 7's `Features/` include
  check caught every shape its author had in mind and none of the others: an angle-bracket
  include reaches the same `-I` directories, and `"../Other/X.hpp"` reaches the next slice
  without the word `Features` appearing at all. A check that stands in for a rule has to be
  tried against the ways of breaking the rule, not the ways of writing the include.
- **A comparison made through a lossy view of the value.** Step 6's sync writes a control only
  when it differs from its field, and both of its real defects were that test answered through a
  conversion: a float threshold read back truncated compared unequal to the slider that set it,
  and a picker's selection resolved through a list gone stale compared equal while pointing at the
  wrong file. A diff is only as good as the round trip under it - check that `Get(Set(x)) == x`
  holds for every value, not that the two sides usually agree.

The one that was rejected: an intra-slice `#include "Wasapi/AudioManager.hpp"` reported as
fragile. It is well-defined - a quoted include searches the includer's directory first - and it
is the convention this tree uses on purpose, so that a *cross*-slice include has to be spelled in
full and the build can grep for it.

---

## 10. Open questions

Nothing open. Settled so far:

- **Page ordering** - the sort key in step 5.
- **Where the indicator lives** - the layer registry in section 4. The overlay owns the window and
  the text pill; the mic pill and the peak meter belong to the slice that owns the device.
- **Who owns the tray** - the provider registry in section 4, and the user picks. The first answer
  written here was "one owner, named out loud", on the grounds that a registry is the wrong shape
  for a surface with one slot. That was wrong about which part is the constraint: one slot is a
  *display* limit, not a design one, and two features do genuinely want the icon - mute state and
  keyboard layout are both reasonable things to watch from the corner of the screen. Nobody in the
  code can know which of them a given user wants, so the exclusivity is resolved by a radio on the
  Tray page rather than by whoever registered first. The menu is a list and keeps no such limit.
