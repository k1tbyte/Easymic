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

What stood in the way before step 13 (all of it is gone since, line numbers are from then):

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
  rules in a standalone harness (not linked into the exe) that feeds it synthetic events:
  `.\build.ps1 -Test`. `.\tests\HotkeyProbe.ps1` drives the real hooks end to end; it needs no
  other EasyLauncher running, and each step extends it with its own cases.

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
    void Edit(std::function<void()> work);          // hold delivery, run work on the edit lane
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
  wraps an empty handler for the sound and the notification, which still run on the Dispatcher
  worker. The binding is always blocked: the firing key must not reach the app.
- When the hotkey fires - on the press, and on the press that completes a multi-press - the
  hotkey stage starts the edit in-proc through `Input::Edit`. A binding still waiting out the
  multi-press window defers its press on `Dispatcher` and its edit on the stage's threadpool
  timer, which hands back to the input thread; a key that ends the window early starts the edit
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

## Keyboard: layout conversion

### Moving puntish (`D:/Repositories/Clion/puntish`)

| puntish | EasyLauncher | Notes |
|---|---|---|
| `src/pack.*`, `bloom.*`, `ngram.*`, `alphabet.*` | `Features/Keyboard/Convert/` | Format unchanged: header, alphabet, bloom, trigram, bigram, mapped in place |
| `src/detector.*` | `Convert/Detector.*` | Takes positions and the pair, not "the first layout of the language" (`keyboard.cpp` `byLanguage`) |
| `src/keyboard.*` | `Convert/LayoutTable.*` | Built for the two chosen layouts only; position -> char, no reverse map |
| `src/text.*` | dropped | `CharLowerBuffW`; `trimWord` moves into `Pack::WordKey`, shared with the builder |
| `src/win/*` | dropped | Replaced by `Core/Input` and `InputLanguage` |
| `src/main.cpp`, `termio.*` | `tools/langpack/` | `pack`, `lookup`, `layouts`, `convert`, and the line mode the README benchmark uses; the pack builder (write side) lives here too, `bench.ps1` reruns the benchmark |
| `data/dict_*.txt` | not in the repo | 66 MB; the source and license of each list go in `tools/langpack/README.md` |
| `packs/*.pack` | see open questions | |

The code takes the repo style on the way in (naming, comments per `CLAUDE.md`).
`tools/langpack` includes the engine from `src/Features/Keyboard/Convert/`, so there is one source.

### Word tracker (stage `kbd.text`)

- A fixed buffer of 64 `{vk, shift, caps}`, the spaces typed after it (up to 8), the foreground
  window at word start, and the side of the pair the word is on screen in (unknown until a
  conversion sets it; until then `apply` reads the focused window's layout). `OnKey` does no
  allocation and no table lookup.
- Shift, Ctrl, Alt, Win and Caps are tracked from the stream the stage sees. Caps is seeded on
  enable (check that `GetKeyState(VK_CAPITAL)` is accurate on the input thread).
- Backspace pops a space, then a key. Space closes the word but keeps it: the conversion retypes
  the spaces, and the next typing key starts a new word. The word clears on: focus change, mouse
  button down, Enter, Tab, Esc, arrows, Home/End/PgUp/PgDn, Del, Ins, any Ctrl/Alt/Win chord
  (AltGr included), and any key outside a fixed set of typing vks (digits, letters, OEM keys). A
  position that types nothing in either layout (a dead key) declines at `apply`.
- For autocorrect, Space ends the word: `Input::Edit`, then the snapshot goes to the edit lane.
- `OnHold(id)` copies the word and the held modifiers into the edit slot and starts a fresh
  buffer. Keys typed during the hold land after the edit, so they are the next word.
- After a conversion the converted word becomes the current one, on the target side, unless
  anything was typed during the hold - so the next `kbd.convert_word` with nothing typed in
  between converts back through the same path.
- The stage always returns `Next`; it never consumes.
- Off while an IME (CJK) is active.

### Detection and the edit

- Autocorrect (step 16), on the lane: render the positions in both layouts and run the puntish
  cascade (dictionary, context, trigrams). Manual conversion needs no detection.
- `apply` on the input thread: drop unless the hold id and the foreground window are unchanged,
  and unless every key renders in both layouts. The edit is `Backspace x (keys + spaces)` and the
  other rendering plus the spaces as `KEYEVENTF_UNICODE`, wrapped in ups and downs of the
  modifiers the app had down when the hold began, or Ctrl and Alt would turn Backspace into word
  deletes and undos. A tap of the unassigned vkE8 comes first when Alt or Win is down, so their
  lone release opens no menu; only Shift and Ctrl go back down after.
- The layout switch (`InputLanguage::SwitchTo`) is posted before `Commit`: an app reads posted
  messages before input, so the held keys replay in the new layout. A refused `Commit` switches
  back. Then `LayoutLayer::Requested`.
- The context (`LanguageContext`) clears on focus change, as in puntish.
- The English list (`words_alpha`) contains junk such as `yee`; replace it with a frequency list
  before autocorrect ships.

### Settings and config

- Step 15: `KeyboardSettings` gains `PairA` and `PairB` (KLID strings; empty takes the first and
  the second installed layout). No revision bump: glaze keeps the defaults for missing keys.
- Step 16, with the behaviour that reads them: `AutoCorrect` (false) and `Exclude` (an exe list in
  `Foreground::CanonicalApp` form).
- Keyboard page:
  - Two `Combo` rows over `InputLanguage::Installed()`, titled by the layout's own name and
    locale ("Russian - ru-RU"). `Get`/`Set` map index <-> KLID through the list `Items` built, so
    no new row kind is needed. Step 16 adds whether a pack was found ("... - no pack").
  - Step 16: an `AutoCorrect` check, enabled only when both packs exist, and `Exclude` as a
    `Text` row.
- The pair's layout tables are built on `Lifecycle::Restore` when the stage is needed, on the UI
  thread, and handed to the input thread whole. Step 16 loads the packs there too and unloads them
  on `Suspend`.
- `kbd.convert_word` carries `EditsText`. Its `Make` marks the tracker as needed, so the stage is
  enabled exactly when the action is bound or autocorrect is on (`Bindings::Apply` runs before
  `Lifecycle::Restore`).

## Checklist

### Step 13 - `Core/Input`, no user-visible change

- [x] `Core/Input/`: the thread, the router, stages, hooks by demand, `Send` with levels, key
      ownership, per-stage down bits, a reset on desktop switch
- [x] `HotkeyService` -> stage `hotkeys`: the table is built on UI and handed to the input thread;
      it asks for `Buttons` only when a mask has a mouse button or a binding is tap-only
- [x] `HotkeyCapture` -> stage `hotkeys.capture`: returns `Deliver` and no longer disposes anyone's
      hooks
- [x] `KeyChord`: the sequence-mask arithmetic both stages share
- [x] `Foreground::Current()` readable from the input thread; capture state atomic
- [x] `Dispatcher::Start/Stop` and `Input::Start/Stop` in `main`, on every exit path
- [x] `MainWindowViewModel` stops calling `HotkeyService::Initialize/Dispose`; `Bindings::Apply`
      publishes, Suspend publishes an empty table
- [x] "Chord release fires against the wrong mask": stale, reworded in `docs/known-bugs.md` (see
      Result)
- [x] Docs: the threading contract in `ARCHITECTURE.md` section 5, the layout there and in
      `CLAUDE.md`; `AGENTS.md` is `CLAUDE.md` again
- **Done when:**
  - multi-press, push-to-talk, tap-only, per-app bindings and capture behave as before;
  - `WH_MOUSE_LL` is not installed without a mouse or tap-only binding;
  - the harness passes: down/up, autorepeat, ownership, levels, foreign injection, re-entrant
    `Send`;
  - Win+L and Ctrl+Alt+Del leave no modifier stuck;
  - the binary size change is recorded.
- **Result:** met where it could be checked here; the gaps are listed.
  - Binary 1,109,504 -> 1,119,232 bytes (+9.5 KB): the thread, `std::move_only_function`, the
    atomic `shared_ptr`, the router.
  - `.\build.ps1 -Test`: 13 router cases pass. Debug builds clean.
  - Live probe on MinSizeRel: a separate process injects with `SendInput` and keeps its own LL
    hook below ours, so it sees what was swallowed. Per-app scope (bound app fires, other app does
    not), a blocked key (no down, repeat or up leaks), one vs two presses, tap-only tapped vs held
    over another key, LCtrl chord, settings open (hotkeys off, keys pass) and closed (hotkeys
    back), exit code 0.
  - Not checked: capture in the action dialog (by hand), push-to-talk (no capture device here),
    the mouse hook's absence (no API shows another process's hooks; it follows from `Needed()`),
    Win+L and Ctrl+Alt+Del (a probe cannot unlock the session) - the desktop-switch reset is in,
    unverified.
  - Behaviour change: without a mouse or tap-only binding, a held mouse button no longer enters
    keyboard chords, so Ctrl+M fires while dragging.
  - The chord-release entry was stale: release claims are taken at press time, so the release
    order no longer matters. What remains is that a single-key binding also fires inside a chord,
    recorded as its own entry.
  - Reviews. Architecture on the design: 3 findings, 2 already handled (queue before the first
    post, an owned up whose owner is disabled), 1 rejected (the input thread may free a replaced
    `Foreground` snapshot - one small free, and the hook already allocates in `Dispatcher::Post`).
    Quality, bug-hunt and architecture on the change: 2 defects, both fixed. Stages before a
    consuming stage never got the up, kept a stale down bit and saw the next press as a repeat;
    the up and repeats now go to every stage up to the owner. Capture teardown had lost the
    `_pending` reset, so a `WM_CAPTURE_DONE` after Cancel called an empty `std::function`. A
    re-check of both fixes found nothing.

### Step 14 - Hold and the edit lane

- [x] `Edit`/`Commit`, `OnHold`, the ring of 64, the timeout; the hold outlives its batch until
      every sent event came back through the hook
- [x] Edit lane on the threadpool; the hold id reaches the handler (`CurrentHold`)
- [x] `ActionFlags::EditsText` through `Bindings::Apply` and the hotkey stage, always blocked,
      press-only
- **Done when:** the harness types through a hold at full speed and nothing is lost or reordered; a
  timeout replays without an edit; a late `Commit` does nothing.
- **Result:** met where it could be checked here; the gaps are listed.
  - Binary 1,119,232 -> 1,127,424 bytes (+8 KB): the hold, the lane, the timer.
  - `.\build.ps1 -Test`: 20 cases pass - typing through a hold (incl. arrivals mid-batch), timeout
    replay, late commit, one hold at a time, full ring pending and in flight, `Unsent`, a stuck
    drain, `NotifyHold` to enabled stages only.
  - Live OS check (standalone probe, not in the repo): `SendInput` re-enters our own LL hook
    before it returns, and the `dwExtraInfo` tag survives intact - the re-entry guard and the
    Edit level rest on that.
  - Regression probe passes; it now focuses its own Notepad first, so the per-app case no longer
    depends on what the user has focused.
  - Not checked live: a real hold with a real edit - no `EditsText` action ships before step 15,
    so the hold path is verified by the harness and the OS check only.
  - Reviews. Architecture on the design: no findings. Quality, bug-hunt and architecture on the
    change: a multi-press binding that completes now starts its edit in-proc (a Dispatcher round
    trip let the firing key and the next keystrokes leak ahead of the hold); a stray condition
    fired a multi-press edit binding on its first press; an edit binding is always blocked, or the
    firing key lands in the text; the duplicate-registration check counts `onEdit`; the hold
    buffers reserve once instead of allocating in the hook; a desktop switch drains the hold
    instead of waiting out the timeout. A re-check found the `onEdit` duplicate check; the rest
    came back clean.
  - Follow-up review: `Commit` took an edit past the deadline when no `WM_TIMER` had run, so
    `Commit` and each held event now check it; a deferred multi-press edit waited on the action
    worker and now resolves on the input thread. The hotkey stage reads the foreground snapshot
    only for per-app bindings, precomputes `block` per scope, and drops release claims in one
    pass. 1,127,424 -> 1,126,912 bytes; 22 harness cases.

### Step 15 - Engine and manual conversion

- [x] Engine into `Features/Keyboard/Convert/` per the table above
- [x] `tools/langpack` target, built through a `build.ps1` switch (`-Tools`)
- [x] Word tracker stage `kbd.text`
- [x] `KeyboardSettings` fields and the Keyboard page rows: the pair; the rest moved to step 16
- [x] `kbd.convert_word`: a repeat press converts back, and the layout follows
- [x] The slice check still passes
- **Done when:**
  - the puntish README benchmark numbers reproduce through `tools/langpack`;
  - live runs in Notepad, Chromium, an Electron app and Windows Terminal convert and convert back;
  - typing during a conversion lands after it, in the new layout;
  - an elevated window without SkipUac is left alone.
- **Result:** the benchmark is met; the live criteria are not checked yet.
  - Binary 1,126,912 -> 1,145,344 bytes (+18 KB): the tracker, `LayoutTable`, the pair rows,
    the KLID list and titles in `InputLanguage`. The rest of the engine compiles in and the linker
    drops it until step 16 - the map shows only `LayoutTable` linked.
  - `tools/langpack`: en and ru packs rebuilt from the word lists are byte-identical to puntish's.
    `bench.ps1` on the en/ru pair: 0 false switches for correct en and ru (3000 each), mixed
    (3000) and short tokens (800); 0 missed for ru typed in en and en typed in ru (3000 each).
  - `.\build.ps1 -Test`: 22 cases pass. Debug builds clean.
  - Not checked live: `tests\HotkeyProbe.ps1` now types into a text box of its own (the new
    Notepad restores the user's tabs) and has conversion cases - convert, back, after a space,
    typing during the conversion - but has not run, since the machine was in use. Notepad,
    Chromium, Electron and Windows Terminal need a hand. An elevated window: our hook sees
    nothing typed there and `apply` checks the foreground window - unverified.
  - Deviations, recorded above: the edit is built in `apply` on the input thread; Space keeps
    the word; the layout switch is posted before `Commit`; an empty pair takes the first two
    installed layouts; `AutoCorrect`, `Exclude`, the "no pack" suffix and pack loading moved to
    step 16.
  - Reviews. Architecture on the design: no findings. Quality, bug-hunt and architecture on the
    change: 1 defect, fixed - a pack with zero bloom bits loaded and then divided by zero on the
    first lookup, and the alphabet was read before the truncation check; the loader now also
    refuses a bloom larger than the file and a hash count above the builder's limit. A style
    pass brought the moved code to the repo's braces and `const`. A re-check of the fix and the
    pass found nothing.

### Step 16 - Autocorrect

- [ ] Space -> hold -> detection -> edit, off by default
- [ ] `AutoCorrect` and `Exclude` in `KeyboardSettings` and on the page; the pair combos say
      whether a pack was found; packs load on `Restore` and unload on `Suspend`
- [ ] The lane owns the snapshot it detects on (the slot is input-thread state, see "Hold and
      edits")
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
- An app lagging behind its input queue reads the layout switch before keys typed ahead of the
  conversion, so those come out in the new layout.
- Same-script pairs (EN/DE, EN/PL) are out of reach for this detector, and AltGr characters are not
  in the tables.
- Password fields are not detected; the exclusion list is the answer.
