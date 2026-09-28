---
paths:
  - "src/Core/Input/**"
  - "src/Core/Hotkeys/**"
  - "src/Features/Keyboard/WordTracker*"
  - "tests/RouterTest.cpp"
  - "tests/*.ps1"
---

# Input pipeline

## Model

- One input thread (message-only loop, `THREAD_PRIORITY_HIGHEST`) owns the hooks, the router and every
  stage's state; `Input::Post` hands work to it.
- A consumer is a stage with an `Order`. Verdicts: `Next`, `Deliver` (to the app, skipping later stages),
  `Consume`.
- Hooks on demand: `WH_KEYBOARD_LL` only while an enabled stage wants keys, `WH_MOUSE_LL` only for
  buttons. Moves and the wheel go straight to `CallNextHookEx`.
- `OnKey` is O(1) and allocates only to hand work off. Slow work goes to the stage's pre-created
  `PTP_WORK` and comes back through `Post`. Never `GetAsyncKeyState`: a stage tracks modifiers from its
  own stream.

## Routing

- `dwExtraInfo` holds a signature and a level, the sending stage's index + 1. Our event at level L
  reaches only the stages after L; physical and foreign injected input starts at the first. Edits and
  replays carry `EditLevel` (0xFF) and reach no stage. This is AutoHotkey's SendLevel: a remap to
  Backspace must still reach the word buffer.
- A stage that consumes a down owns that vk until its up; a stage never gets an up without its down.
- `SendInput` from inside the proc re-enters the hook before it returns: update state before `Send`.
- A hook set change or a desktop switch (lock, UAC) resets the router and calls every `OnReset`.

## Hold and edits

- `Input::Edit(work, landed)` holds delivery (ring of 64: keys and buttons, moves pass) and runs `work`
  on the threadpool, which posts `Commit(id, edit)`: one `SendInput` of edit + replay. One hold at a time.
- The hold ends only once every event it sent came back through our hook. A 150 ms timeout or a full
  ring replays without an edit.
- Edits never queue on `Dispatcher`: its worker runs slow COM and WASAPI calls.
- Replayed input is injected: apps that reject injection must be excluded from text features.
- An `ActionFlags::EditsText` action starts its edit in-proc when the hotkey fires; its sound and
  notification run only if `Commit` sent text.

## Stages

| Id | Order | Owner | Enabled while |
|---|---|---|---|
| `hotkeys.capture` | 0 | `Core/Hotkeys` | the action dialog captures |
| `hotkeys` | 200 | `Core/Hotkeys` | a hotkey is registered |
| `kbd.text` | 300 | `Features/Keyboard` | convert is bound or autocorrect is on |

Order 100 waits for a remap stage: levels, `Send` from the proc and releasing its keys on `Disable`
already cover it.

## Test and open

- `tests/RouterTest.cpp` drives the router and hold with synthetic events; `.\tests\HotkeyProbe.ps1
  [-Autocorrect]` drives the real hooks on MinSizeRel.
- Measure the hold timeout and the thread priority; is `GetKeyState(VK_CAPITAL)` right on this thread.
