# Input pipeline

The contract of `Core/Input`: the input thread, stages, routing, hold and edits. Layout
conversion and autocorrect, the one consumer that edits text, live in `docs/LAYOUT.md`.

## Testing

- `.\build.ps1` (MinSizeRel) and `.\build.ps1 -Config Debug`. Debug skips the hooks
  (`APP_NO_GLOBAL_HOOKS`), so hook behaviour is checked on MinSizeRel.
- The router and hold rules run in a standalone harness fed with synthetic events
  (`tests/RouterTest.cpp`, `.\build.ps1 -Test`).
- `.\tests\HotkeyProbe.ps1 [-Autocorrect]` drives the real hooks end to end: no other
  EasyLauncher may run, and the machine must stay idle while it types.

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
- **`Dispatcher` is not tied to the hooks.** It stops only while the settings window is open,
  because actions read and save the config that window edits.
- **Stages never read `GetAsyncKeyState`.** After a consume or a remap the system state is not what
  the stage saw, so each stage tracks modifiers from its own stream.

## Contract: `Core/Input`

Built in step 13. Steps 14-16 add to it (marked below); nothing here is removed by them.

```
Core/Input/
  Input.hpp          the contract below
  Input.cpp          the thread, hooks by demand, SendInput, the message loop
  Router.hpp/.cpp    levels, ownership, verdicts - pure, driven by tests/RouterTest.cpp
Core/Hotkeys/        the hotkey and capture stages, KeyChord, KeyNames, Bindings
```

```cpp
namespace Input {
    using Wants = uint8_t;                  // WantKeys, WantButtons
    enum class Verdict : uint8_t { Next, Deliver, Consume };

    struct KeyEvent { uint8_t Vk; bool Down, Repeat; };  // a mouse button is its VK_*BUTTON

    struct Stage {
        std::string_view Id;                // "hotkeys.capture", "hotkeys", "kbd.text"
        int Order;
        Verdict (*OnKey)(const KeyEvent&);  // input thread, O(1), no allocation
        void (*OnReset)();                  // enabled, or hooks/desktop changed: held state is void
    };

    void Add(const Stage&);                 // before Start
    bool Start(); void Stop();              // main, around the window's lifetime
    void Enable(std::string_view id, Wants); void Disable(std::string_view id);  // any thread
    void Post(std::move_only_function<void()>);           // runs on the input thread
    UINT Send(std::span<INPUT>, std::string_view from);   // any thread, tagged with from's level
}
```

```cpp
    // Added in step 14:
    struct Stage { ...; void (*OnHold)(HoldId); };  // a hold began: snapshot and start fresh
    void Edit(std::function<void()> work,           // hold delivery, run work on the edit lane;
              std::function<void()> landed = {});   // landed: input thread, once Commit sent an edit
    HoldId CurrentHold();                           // the hold the running edit works under
    bool Commit(HoldId, std::span<const INPUT>);    // input thread: edit + held go out, no stage
                                                    // sees them (the Edit level, 0xFF)
```

A later step adds `Wants` for wheel and moves with `OnMouse` and a `MouseEvent`, with their first
consumer.

### Threads

- The input thread owns the hooks, the router and every stage's `OnKey` state. A message-only
  loop at `THREAD_PRIORITY_HIGHEST`; `Post` travels as a thread message.
- UI -> input: `Enable`/`Disable`, and the hotkey table, which is built on the UI thread, handed
  over whole through `Post` and freed on the input thread.
- Input -> out: `Dispatcher::Post` (actions), `PostMessage` (capture preview and done).
- `Foreground::Current()` is an atomic `shared_ptr` snapshot, still written on the UI thread only.
- `HotkeyCapture`'s target, captured mask and pending flag are atomics.
- `Dispatcher` and the input thread start and stop in `main`, on every exit path. `Dispatcher`
  also stops in `MainWindowViewModel::SuspendActivity` and starts again in `RestoreConfig`: an
  action reads and saves the config, so none may run while settings edits it. `Start` drops what
  a hook posted in between.

### Routing rules

1. `dwExtraInfo` holds a 56-bit signature and an 8-bit level: the sending stage's index + 1. Our
   event at level L goes to the stages after L. Everything else, foreign `LLKHF_INJECTED`
   included, counts as physical and starts at the first stage.
2. A stage that consumes a down owns that vk in that level until its up. Repeats and the up go
   to the stages up to the owner that saw the down, whatever they answer, and are swallowed. A
   disabled stage is not called, but the rest stays eaten. This replaces `_blockedKeys`.
3. Each stage has its own down bits: it never gets an up without its down, and `Repeat` is a down
   while its bit is set. The verdict of an up counts only as `Deliver`.
4. `Deliver` skips the later stages. An event that passes the last stage with `Next` is delivered.
5. A `Send` from inside `OnKey` re-enters the router before it returns.
6. Hooks follow the union of the enabled `Wants`, re-evaluated on the input thread after every
   `Enable`/`Disable`. `WM_MOUSEMOVE` and the wheel go straight to `CallNextHookEx`. Changing the
   installed hook set, or a desktop switch (lock, Ctrl+Alt+Del, UAC), resets router state and calls
   every enabled stage's `OnReset`. Enabling a stage resets that stage alone.
7. `APP_NO_GLOBAL_HOOKS` (Debug) still gates the bindings only, in
   `MainWindowViewModel::RestoreConfig`, so capture keeps working in Debug.
8. Step 14: while a hold is active, a delivered event is swallowed into a fixed ring of 64. Keys
   and mouse buttons are held; moves and wheel pass. Clicks are held so they cannot move the caret
   mid-edit. The hold ends only when every event it sent has come back through our own hook -
   until then a physical event already queued could still overtake the batch. What arrives
   meanwhile is held and goes out behind it. A desktop switch and shutdown release the hold
   without an edit.
9. Remap stage: `Disable(stage)` sends the up for every key the stage put down through `Send` and
   has not released yet, so no key stays stuck when a remap goes away.

### Hold and edits

Built in step 14. `Hold` is not public: the entry point is `Input::Edit(work)`, and the pure
state (ring, outbox, deadlines) is `Core/Input/Hold.*`, driven by `tests/RouterTest.cpp`.

- `Edit` from the input thread starts the hold in-proc, so the event being handled is held too;
  from anywhere else it starts once that thread gets to it. Every enabled stage gets `OnHold(id)`.
- One hold at a time: `Edit` during a hold is skipped.
- A timeout (150 ms, on a 50 ms thread timer) or a full ring replays without an edit and expires
  the id. `WM_TIMER` waits for an idle queue, so the deadline is also checked when an event is
  held and when `Commit` runs.
- `Commit(id, edit)` runs on the input thread: one `SendInput(edit + replay)` tagged with the
  Edit level. `SendInput` re-enters our hook before it returns (verified live), so the events are
  counted as they pass and the hold stays up until the last one came back.
- The replay keeps scan codes and the extended and up flags. A held click is replayed at the
  current cursor position. Replayed input counts as injected, so apps that reject injection
  (games, anti-cheat) must be excluded from text features.
- The edit lane runs on the threadpool and is pure: a snapshot in, an `INPUT` list out, then
  `Input::Post(apply)`. `apply` checks the id, updates the stage's state and calls `Commit`. When
  the work returns, the lane releases the hold itself - a `Commit` it posted comes first, a work
  that declined commits nothing. A work with nothing slow to do (manual conversion) only posts
  `apply`, which renders and builds the edit on the input thread: the stage's copy of the word
  is input-thread state, and a hold can expire while a slow lane still reads it.

### Actions that type

Built in step 14.

- New `ActionFlags::EditsText`. `Bindings::Apply` puts the handler on `HotkeyBinding::onEdit` and
  wraps an empty handler for the sound and the notification. The hotkey stage hands that to
  `Input::Edit` as `landed`, which posts it to the Dispatcher worker only once the edit's `Commit`
  sent text: a press that converts nothing is silent. The binding is always blocked: the firing
  key must not reach the app.
- When the hotkey fires - on the press, and on the press that completes a multi-press - the
  hotkey stage starts the edit in-proc through `Input::Edit`. A binding still waiting out the
  multi-press window defers its edit, feedback included, on the stage's threadpool timer, which hands back to the input thread; a key that ends the window early starts the edit
  in-proc. The edit never waits for the action worker.
- The lane gives the handler the hold id (`Input::CurrentHold()`, thread-local), so `ActionFn`
  keeps its signature.
- A handler that declines commits nothing; the lane's own release and the timeout are the net.

### Stages

| Id | Order | Wants | Owner | Enabled while |
|---|---|---|---|---|
| `hotkeys.capture` | 0 | Keys, Buttons | `Core/Hotkeys` | the action dialog captures a combination |
| `remap` | 100 | Keys, Buttons | future | - |
| `hotkeys` | 200 | Keys; Buttons only if a mask has a mouse button or a binding is tap-only | `Core/Hotkeys` | a hotkey is registered |
| `kbd.text` | 300 | Keys, Buttons | `Features/Keyboard` | `kbd.convert_word` is bound or autocorrect is on |

## Not built yet

- The remap stage. The contract already covers it: levels, `Send` from inside the proc, stuck-key
  release.
- Mouse motion for screen-edge triggers: take it from Raw Input, which is off the input path,
  rather than from `WH_MOUSE_LL`.
- An "OS state only" tag (PowerToys `SUPPRESS_FLAG`) for toggle keys and modifier resets. It lands
  with the remap stage.

## Open questions

- The hold timeout and the input thread priority: measure.
- Whether `GetKeyState(VK_CAPITAL)` is accurate on the input thread.
