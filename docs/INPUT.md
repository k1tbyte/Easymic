# Input pipeline and layout conversion

Plan for steps 13-16: a shared input pipeline in `Core/Input`, then layout conversion in
`Features/Keyboard` built on top of it (the puntish PoC moved in). Living file: tick items as they
land. Once all four steps are done, the contract moves into `ARCHITECTURE.md` section 4 and this
file is deleted.

## Why

Hotkeys are the only consumer of the LL hooks today, and the hooks are shaped for them. Layout
conversion has to watch typed text and rewrite it; remapping, snippets and mouse triggers come
after. They all need the same things: input on demand, a synchronous verdict, injection without
loops, and ordering. That is built once, in `Core/`.

What in the current code stands in the way:

- Hooks live on the UI thread and go up only when `Bindings::Apply` registered something
  (`src/UI/MainWindowViewModel.hpp:67-70`). A feature with no hotkey gets no input.
- Keyboard and mouse hooks go up together (`src/Core/Hotkeys/HotkeyService.cpp:453-454`), so every
  mouse move crosses into the UI thread even when no binding uses a mouse button.
- `Dispatcher` starts and stops with the hooks (`HotkeyService.cpp:451`, `Dispose`).
- The hook proc reads UI-only state: `Foreground::CurrentOnUi()` (`HotkeyService.cpp:205`).
- `HotkeyCapture` installs and disposes every hook itself (`HotkeyCapture.hpp`, `Start` and
  `_teardown`).
- Nothing tags injected input; nothing in `src/` calls `SendInput` yet.
- "Chord release fires against the wrong mask" in `docs/known-bugs.md` waits for the keyboard
  and mouse procs to merge.

## Workflow

- Implementation and reviews through Pi agents (`pi-subagents`): bug-hunt, quality and
  architecture after each step, one fix round, then a re-check.
- Every step builds with `.\build.ps1` (MinSizeRel) and `.\build.ps1 -Config Debug`. Debug skips
  the hooks (`APP_NO_GLOBAL_HOOKS`), so hook behaviour is checked on MinSizeRel, and the router's
  rules in a standalone harness (not linked into the exe) that feeds it synthetic events.

## Decisions (fixed)

### Input

- **Dedicated input thread.** Hooks leave the UI thread, so overlay paints, WinEvent callbacks and
  `ToUi` work cannot delay input on the whole desktop. AutoHotkey keeps its hooks on their own
  thread for the same reason. Cost: one sleeping thread and handing tables over by message.
- **Stages, not callbacks.** A consumer is an `InputStage` with an `Order`, and events walk the
  stages in that order. A POD with function pointers, the same shape as `OverlayLayer` and
  `TrayProvider`.
- **Levels, not "skip our own input".** Input a stage sends is seen only by the stages after it.
  Physical input and foreign injections are seen by all stages; text edits and hold replays by
  none. This is AutoHotkey's SendLevel/#InputLevel scheme. A remap to Backspace has to reach the
  word buffer, and a remap to Ctrl+M has to fire a Ctrl+M hotkey.
- **Three verdicts:** `Next` (on to later stages), `Deliver` (to the app, skipping later stages -
  what hotkey capture does now), `Consume`.
- **Hooks on demand, per OS hook.** `WH_KEYBOARD_LL` only while an enabled stage wants keys,
  `WH_MOUSE_LL` only while one wants buttons, wheel or moves. Filtering moves inside the proc is
  not enough: with the hook up, every move has already crossed into our thread.
- **A remap sends from inside the proc.** `SendInput` in an LL hook re-enters the hook for the
  injected events before it returns (PowerToys KBM relies on this). A stage updates its state
  before it calls `Send`.
- **Hold is a core primitive.** Stages see input at once; delivery to apps waits while a text edit
  is in flight; the edit and the replay go out in one `SendInput`, which Windows never interleaves
  with other input. Conversion, autocorrect and any future action that types need it.
- **Edits never queue on `Dispatcher`.** Its worker runs desktop COM calls and WASAPI, and an edit
  stuck behind them misses the hold timeout. Detection runs on the threadpool; the edit is applied
  on the input thread.
- **`Dispatcher` lives as long as the app**, not as long as the hooks.
- **Stages never read `GetAsyncKeyState`.** After a consume or a remap the system state is not what
  the stage saw, so each stage tracks modifiers from its own stream.

### Keyboard

- **puntish moves in whole and is frozen.** No second copy: the engine goes to
  `Features/Keyboard/Convert/`, and the pack builder and console go to `tools/langpack/`, a separate
  target that is not linked into the exe.
- **The user picks two installed layouts** (two Combo rows). The pack is found by the layout's
  language in `packs/` next to the exe. Layouts bind by KLID, not "the first layout of the
  language", so two layouts of one language work, and so does any Cyrillic language that has its
  own pack.
- **Only the chosen pair is mapped.** Packs stay external files (ru is 2.9 MB), not resources.
- **The buffer holds key positions** (vk, Shift, Caps) plus the layout, never characters. The hook
  does no table lookups; conversion reads the same positions in the other layout.
- **Undo is converting again.** One action, `kbd.convert_word`: pressing it again with nothing
  typed in between converts back. The same press undoes an autocorrection.
- **Autocorrect is off by default** and fires on Space only. Enter and Tab clear the word, because
  the line is usually sent by then.
- Case folding through `CharLowerBuffW` (puntish `text.cpp` goes). Layout switching through the
  existing `InputLanguage`, which targets the focused window rather than the foreground one as
  puntish `win/input.cpp` does.

## Contract: `Core/Input`

```
Core/Input/
  Input.hpp          the contract below
  Router.hpp/.cpp    levels, ownership, verdicts, hold ring - pure, harness-tested
  InputThread.cpp    the thread, hooks by demand, SendInput, the message loop
Core/Hotkeys/        the hotkey stage, the capture stage, KeyNames, Bindings
```

```cpp
enum class Wants : uint8_t { Keys = 1, Buttons = 2, Wheel = 4, Move = 8 };
enum class Verdict : uint8_t { Next, Deliver, Consume };

struct KeyEvent {
    uint16_t Vk, Scan;
    uint32_t Flags;      // KBDLLHOOKSTRUCT::flags
    uint32_t Time;
    bool Down, Repeat;   // Repeat: a down while already down, computed by the router
};

struct MouseEvent {
    enum Kind : uint8_t { Button, Wheel, Move } Kind;
    uint8_t Vk;          // VK_LBUTTON..VK_XBUTTON2 for Button
    bool Down;
    int16_t Delta;       // Wheel
    POINT Pt;
};

using HoldId = uint32_t; // 0 = none

struct InputStage {
    std::string_view Id;                              // "hotkeys.capture", "hotkeys", "kbd.text"
    int Order;
    Verdict (*OnKey)(const KeyEvent&) = nullptr;      // input thread, O(1), no allocation
    Verdict (*OnMouse)(const MouseEvent&) = nullptr;
    void (*OnHold)(HoldId) = nullptr;                 // freeze what the edit will act on
    void (*OnReset)() = nullptr;                      // hooks reinstalled, state is stale
};

namespace Input {
    void Add(const InputStage&);                      // at startup, before the thread runs
    void Enable(std::string_view id, Wants);          // any thread
    void Disable(std::string_view id);
    void Post(std::function<void()>);                 // runs on the input thread, cold path
    void Send(std::span<const INPUT>, std::string_view from);  // any thread, tagged
    HoldId Hold(std::chrono::milliseconds limit);     // from OnKey/OnMouse only
    bool Commit(HoldId, std::span<const INPUT> edit); // input thread; false once expired
}
```

The shapes are a sketch; the rules below are the contract.

### Threads

- The input thread owns the hooks, the router, every stage's `OnKey` state and the hold ring. It
  runs a message-only loop at raised priority (measure how high).
- UI -> input: `Enable`/`Disable`, and the hotkey table, which is built on the UI thread, handed
  over whole through `Post` and freed on the input thread.
- Input -> out: `Dispatcher::Post` (actions), `PostMessage` (capture preview and done), the
  threadpool (edit lane).
- `Foreground` publishes a snapshot the input thread can read (atomic handoff); the hotkey stage
  stops calling `CurrentOnUi`.
- `HotkeyCapture`'s statics become atomics, or travel as `PostMessage` payloads.

### Routing rules

1. `dwExtraInfo` holds a 48-bit signature and a level: the sending stage's index + 1, or `Edit`
   (the maximum). Our event at level L goes to the stages after L; `Edit` goes to no stage.
   Everything else, foreign `LLKHF_INJECTED` included, counts as physical and starts at the first
   stage.
2. A stage that consumes a down owns that vk until its up: repeats and the up go to the owner only
   and are swallowed. This replaces `_blockedKeys`. The router also records the mask each key went
   down under, which fixes the chord-release known bug.
3. `Deliver` skips the later stages. An event that passes the last stage with `Next` is delivered.
4. While a hold is active, a delivered event is swallowed into a fixed ring of 64. Keys and mouse
   buttons are held; moves and wheel pass. Clicks are held so they cannot move the caret mid-edit.
5. A `Send` from inside `OnKey` re-enters the router before it returns.
6. `Disable(stage)` sends the up for every key the stage put down through `Send` and has not
   released yet, so no key stays stuck when a remap goes away.
7. Hooks follow the union of the enabled `Wants`, re-evaluated on the input thread after every
   `Enable`/`Disable`. When no stage wants `Move`, a move goes straight to `CallNextHookEx`.
   Reinstalling hooks resets router state and calls every `OnReset`. Under `APP_NO_GLOBAL_HOOKS`
   no hook is installed; the router still runs, and so does the harness.
8. An event no stage wants costs one level check and a `CallNextHookEx`.

### Hold and edits

- `Hold` calls every enabled stage's `OnHold(id)` in-proc. The event that started the hold is held
  too, so an edit on Space does not have to erase the Space.
- One hold at a time: `Hold` during a hold returns 0, and the second edit is skipped.
- A timeout (start at 150 ms, then measure) or a full ring replays without an edit and expires
  the id.
- `Commit(id, edit)` runs on the input thread: one `SendInput(edit + replay)` tagged `Edit`.
- The replay keeps scan codes and the extended and up flags. A held click is replayed at the
  current cursor position. Replayed input counts as injected, so apps that reject injection
  (games, anti-cheat) must be excluded from text features.
- The edit lane runs on the threadpool and is pure: a snapshot in, an `INPUT` list out, then
  `Input::Post(apply)`. `apply` checks the id, updates the stage's state and calls `Commit`.

### Actions that type

- New `ActionFlags::EditsText`. `Bindings::Apply` marks the hotkey. When the hotkey fires, the
  hotkey stage calls `Hold` in-proc and submits the handler to the edit lane instead of
  `Dispatcher::Post`.
- The lane gives the handler the hold id (thread-local, set around the call), so `ActionFn` keeps
  its signature.
- A handler that declines still releases the hold with `Commit(id, {})`. The timeout is only a
  safety net.

### Stages

| Id | Order | Wants | Owner | Enabled while |
|---|---|---|---|---|
| `hotkeys.capture` | 0 | Keys, Buttons | `Core/Hotkeys` | the action dialog captures a combination |
| `remap` | 100 | Keys, Buttons | future | - |
| `hotkeys` | 200 | Keys; Buttons only if a mask has a mouse button | `Core/Hotkeys` | a hotkey is registered |
| `kbd.text` | 300 | Keys, Buttons | `Features/Keyboard` | `kbd.convert_word` is bound or autocorrect is on |

## Keyboard: layout conversion

### Moving puntish (`D:/Repositories/Clion/puntish`)

| puntish | EasyLauncher | Notes |
|---|---|---|
| `src/pack.*`, `bloom.*`, `ngram.*`, `alphabet.*` | `Features/Keyboard/Convert/` | Format unchanged: header, alphabet, bloom, trigram, bigram, mapped in place |
| `src/detector.*` | `Convert/Detector.*` | Takes positions and the pair, not "the first layout of the language" (`keyboard.cpp` `byLanguage`) |
| `src/keyboard.*` | `Convert/LayoutTable.*` | Built for the two chosen layouts only; position -> char, no reverse map |
| `src/text.*` | dropped | `CharLowerBuffW`; `trimWord` moves into the detector |
| `src/win/*` | dropped | Replaced by `Core/Input` and `InputLanguage` |
| `src/main.cpp`, `termio.*` | `tools/langpack/` | `pack`, `lookup`, `layouts`, `convert`, and the line mode the README benchmark uses |
| `data/dict_*.txt` | not in the repo | 66 MB; the source and license of each list go in `tools/langpack/README.md` |
| `packs/*.pack` | see open questions | |

The code takes the repo style on the way in (naming, comments per `CLAUDE.md`).
`tools/langpack` includes the engine from `src/Features/Keyboard/Convert/`, so there is one source.

### Word tracker (stage `kbd.text`)

- A fixed buffer of 64 `{vk, shift, caps}`, plus the layout and the focused window at word start.
  `OnKey` does no allocation and no table lookup.
- Shift, Ctrl, Alt, Win and Caps are tracked from the stream the stage sees. Caps is seeded on
  enable (check that `GetKeyState(VK_CAPITAL)` is accurate on the input thread).
- Backspace pops a key. The word clears on: focus change, mouse button down, Enter, Tab, Esc,
  arrows, Home/End/PgUp/PgDn, Del, Ins, any Ctrl/Alt/Win chord, and a key with no character in the
  layout.
- For autocorrect, Space ends the word: `Hold`, then the snapshot goes to the edit lane.
- `OnHold(id)` copies the word into the edit slot and starts a fresh buffer. Keys typed during the
  hold land after the edit, so they are the next word.
- After a conversion the tracker remembers which side of the pair the word is rendered in, so the
  next `kbd.convert_word` with nothing typed in between converts back.
- The stage always returns `Next`; it never consumes.
- Off while an IME (CJK) is active.

### Detection and the edit

- On the lane: render the positions in both layouts, run the puntish cascade (dictionary, context,
  trigrams), and build `Backspace x n` plus the other rendering as `KEYEVENTF_UNICODE`.
- `apply` on the input thread: drop unless the hold id and the focused window are unchanged. Then
  `Commit`, then switch the window's layout through `InputLanguage` and
  `LayoutLayer::Requested`.
- The context (`LanguageContext`) clears on focus change, as in puntish.
- The English list (`words_alpha`) contains junk such as `yee`; replace it with a frequency list
  before autocorrect ships.

### Settings and config

- `KeyboardSettings` gains `PairA` and `PairB` (KLID strings), `AutoCorrect` (false) and `Exclude`
  (an exe list in `Foreground::CanonicalApp` form). No revision bump: glaze keeps the defaults for
  missing keys.
- Keyboard page:
  - Two `Combo` rows over `InputLanguage::Detail::Installed()`. The item text says whether a pack
    was found ("Russian", "Ukrainian - no pack"). `Get`/`Set` map index <-> KLID through the list
    `Items` built, so no new row kind is needed.
  - An `AutoCorrect` check, enabled only when both packs exist.
  - `Exclude` as a `Text` row.
- Packs load on `Lifecycle::Restore` when the stage is needed and unload on `Suspend`.
- `kbd.convert_word` carries `EditsText`. Its `Make` marks the tracker as needed, so the stage is
  enabled exactly when the action is bound or autocorrect is on (`Bindings::Apply` runs before
  `Lifecycle::Restore`).

## Checklist

### Step 13 - `Core/Input`, no user-visible change

- [ ] `Core/Input/`: the thread, the router, stages, hooks by demand, `Send` with levels, key
      ownership, the per-key down mask
- [ ] `HotkeyService` -> stage `hotkeys`: the table is built on UI and handed to the input thread;
      it asks for `Buttons` only when a mask has a mouse button
- [ ] `HotkeyCapture` -> stage `hotkeys.capture`: returns `Deliver` and no longer disposes anyone's
      hooks
- [ ] `Foreground` snapshot readable from the input thread; capture state thread-safe
- [ ] `Dispatcher::Start/Stop` move to `main`
- [ ] `MainWindowViewModel` stops calling `HotkeyService::Initialize/Dispose`; Suspend and Restore
      go through `Enable`/`Disable`
- [ ] Close "Chord release fires against the wrong mask" in `docs/known-bugs.md`
- [ ] Docs: the threading contract in `ARCHITECTURE.md` section 5, the layout in `CLAUDE.md`;
      align `AGENTS.md` with `CLAUDE.md` (it still says "Refactor in progress")
- **Done when:**
  - multi-press, push-to-talk, tap-only, per-app bindings and capture behave as before;
  - `WH_MOUSE_LL` is not installed without a mouse binding;
  - the harness passes: down/up, autorepeat, ownership, levels, foreign injection, re-entrant
    `Send`, capture, stuck-key release on `Disable`;
  - Win+L and Ctrl+Alt+Del leave no modifier stuck;
  - the binary size change is recorded.

### Step 14 - Hold and the edit lane

- [ ] `Hold`/`Commit`/`Post`, `OnHold`, the ring of 64, the timeout
- [ ] Edit lane on the threadpool; the hold id reaches the handler
- [ ] `ActionFlags::EditsText` through `Bindings::Apply` and the hotkey stage
- **Done when:** the harness types through a hold at full speed and nothing is lost or reordered; a
  timeout replays without an edit; a late `Commit` does nothing.

### Step 15 - Engine and manual conversion

- [ ] Engine into `Features/Keyboard/Convert/` per the table above
- [ ] `tools/langpack` target, built through a `build.ps1` switch
- [ ] Word tracker stage `kbd.text`
- [ ] `KeyboardSettings` fields and the Keyboard page rows
- [ ] `kbd.convert_word`: a repeat press converts back, and the layout follows
- [ ] The slice check still passes
- **Done when:**
  - the puntish README benchmark numbers reproduce through `tools/langpack`;
  - live runs in Notepad, Chromium, an Electron app and Windows Terminal convert and convert back;
  - typing during a conversion lands after it, in the new layout;
  - an elevated window without SkipUac is left alone.

### Step 16 - Autocorrect

- [ ] Space -> hold -> detection -> edit, off by default
- [ ] The `Exclude` list is honoured; the tracker is off in excluded apps
- [ ] Decision log through `Logger` (word, verdict, reason, margin, preference) for tuning the
      thresholds
- [ ] A frequency-based English list
- **Done when:** a day of real ru/en typing produces no false switch that the log cannot explain.

### Not now

- The remap stage. The contract already covers it: levels, `Send` from inside the proc, stuck-key
  release.
- Mouse motion for screen-edge triggers: take it from Raw Input, which is off the input path,
  rather than from `WH_MOUSE_LL`.
- An "OS state only" tag (PowerToys `SUPPRESS_FLAG`) for toggle keys and modifier resets. It lands
  with the remap stage.
- Converting a selection; downloading packs from settings.

## Open questions

- Pack distribution: release assets placed next to the exe, committed binaries, or built in CI.
- The license of each source word list, checked before any pack ships.
- The hold timeout and the input thread priority: measure.
- Whether `GetKeyState(VK_CAPITAL)` is accurate on the input thread.

## Known limits (accepted)

- A Backspace replacement cannot prove the caret is still where the word ended (autocomplete
  popups, editors that reformat). Hold removes the typing race, not this.
- Non-elevated hooks see nothing in elevated windows, and `SendInput` into them fails silently
  (UIPI). SkipUac covers it.
- Same-script pairs (EN/DE, EN/PL) are out of reach for this detector, and AltGr characters are not
  in the tables.
- Password fields are not detected; the exclusion list is the answer.
