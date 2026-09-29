---
paths:
  - "src/**"
---

# Architecture

## Threads

- Input thread: the LL hooks and the stage pipeline ([input](input.md)).
- Edit lane: the threadpool behind `Input::Edit`, a text edit under a hold.
- Action worker (`Dispatcher`): every action body. Stopped while settings are open, since actions read
  and save the config; `Start` drops what was posted meanwhile.
- Desktops lane: the `vd.*` actions run on their own one-at-a-time threadpool lane, so a desktop
  animation never queues mute or volume behind it.
- UI thread: windows and the config, reached with `Dispatcher::ToUi`. Overlay `Measure`/`Render`/`Tick`
  run here only; `Overlay::Invalidate` may be called from any thread.
- WASAPI callbacks: atomics only. No register, unregister, wait or last release inside one: hand the work
  to the threadpool.
- Threadpool: feature work that must not queue behind actions; what reads the config hops through `ToUi`.
- Moving work between threads: compare the old and new call order and lifetimes. Both kinds of defect
  compiled and looked right here.

## Kernel shapes

- `ActionRegistry`, `SettingsHost`, `Overlay`, `Tray`: POD descriptors with plain function pointers, the
  last three sorted by `Order`. No DI container, no event bus between features.
- A settings page is a `SettingsRow` table, or `Tabs` of them (one page per feature). A `Custom` row of
  height 0 fills the rest of its page. Hooks edit fields, never controls: `Changed` on a click,
  `Commit` on OK only, Cancel restores the config snapshot. A control is written only where it differs
  from its field, so `Get(Set(x)) == x` must hold for every value.
- An overlay layer asks for ticks only while it has something to watch: idle means no timer.
- Tray: providers, and the user picks who paints the icon.

## Config

- `config.json` next to the exe, glaze JSON (50 KB over BEVE, paid for a readable file). Apply rewrites
  the whole file. Nothing migrates, no compatibility shims.
- Section structs live in `Core/AppConfig.hpp`; a module sees only its section. A field goes to the
  section that would still want it if the feature were deleted.
- `AppConfig.hpp` includes no glaze: serialization is in `AppConfig.cpp`, enum names in `AppConfigJson.hpp`.
- Files go through `Platform/File.hpp`, never iostreams: their locale code costs ~100 KB of exe.
- Hotkeys are stored as names (`CTRL + SHIFT + M`); one that does not parse stays in the list unbound.
- Saved through `config.json.tmp` and a rename; a file that does not parse is renamed to `config.json.bad`
  and defaults load.
- The settings window is modeless and runs through `IsDialogMessage`: Tab, Enter (OK), Esc (Cancel).
- An update renames the running exe to `.old` and moves the download in; `.old` is deleted on the next start.

## Known, not fixed

- A single-key binding also fires inside a chord with that key (`A` and `CTRL + A`). Changing it changes
  what existing configs do: a decision, not a fix.
- `LCTRL` in the config comes back as `CTRL`: the canonical form, intended.
- `AppConfig::Load` clamps nothing: a hand-edited `Overlay.Size` of 0 hides the overlay and its drag
  preview. Validate every field in one pass, not this one alone.
