# MicroTasks (host-scheduled cooperative tasks) specification

This file specifies MicroTasks: cooperative tasks that execute inside the currently running WASM app, but are **scheduled by the firmware** so the device can avoid periodic wakeups when idle.

## Overview

A MicroTask is a unit of app-defined work that:

- Runs on the app’s main thread (the firmware thread that dispatches host events into WASM).
- Performs a small amount of work per invocation.
- Yields by returning a scheduling action to the host (run again soon, sleep, or done).

## Goals

- Enable apps to do incremental work (chunked computation, staged I/O, UI animations) without blocking the device.
- Support delayed execution (“sleep N ms”) and periodic execution (“run every N ms”).
- Minimize battery drain by avoiding periodic tick wakeups when there is no due work.
- Keep the host ABI small and stable: integer task handles, one exported step function, small import surface for scheduling.

## Non-goals (v0)

- Preemptive multitasking or background threads inside WASM.
- Running tasks while the system is asleep without a wake source.
- Precise real-time scheduling (microsecond-level accuracy). Timing is millisecond-based and subject to host jitter.

## Terms

- **Due time**: the earliest millisecond time at which a task is eligible to run.
- **Yield**: the task requests to run again soon (but not immediately; see battery rules).
- **Step**: one invocation of a task’s work function.

## Battery intent and scheduling model

The firmware must avoid “polling for work”. When:

- There are no due tasks, and
- There is no active touch input requiring frequent polling, and
- No host events are queued,

the host event loop should block/sleep until the next meaningful deadline (next task due time, next touch poll time, or idle power-off deadline).

This is the primary reason MicroTasks are host-scheduled rather than app-tick-driven.

## Time model and wrap-around

Host time passed to WASM uses `now_ms: i32` (milliseconds since boot). This value wraps due to 32-bit overflow. Scheduling must therefore treat times as wrapping `u32`.

Wrap-safe comparison rule (required in both firmware and Zig SDK):

- Interpret times as `u32`.
- `now` is “after or equal to” `t` if `(now -% t) < 0x8000_0000`.

This matches the common “half-range” modular comparison rule.

## Host ↔ WASM interface

### WASM export: `portalMicroTaskStep`

The firmware calls a single WASM export to advance MicroTasks:

```c
// handle: MicroTask handle allocated by the host
// now_ms: host time in ms (wrapping i32)
// Returns: encoded scheduling action (see below)
int64_t portalMicroTaskStep(int32_t handle, int32_t now_ms);
```

For Zig apps built using `../zig-sdk`, this export is expected to be **provided by the Zig SDK itself**, similar to how `portalAlloc`/`portalFree` are exported from the SDK. Apps should not implement `portalMicroTaskStep` manually; they should register MicroTasks via the SDK’s MicroTask APIs, and the SDK’s exported dispatcher will route `handle` to the correct task instance.

If the export is missing (for example, a non-Zig app, or an app that does not link the Zig SDK), the firmware still accepts MicroTask scheduling calls (e.g. `microtaskStart`), but it must treat that as “task cannot run” and return `kWasmErrNotReady` from start, with a clear last-error message.

### Action encoding (return value of `portalMicroTaskStep`)

The return value is an `i64` packed as:

- High 32 bits: `kind` (signed/unsigned does not matter; treat as `u32`).
- Low 32 bits: `arg` (meaning depends on `kind`).

Kinds:

- `0 = DONE` (arg ignored): task is complete; host removes it.
- `1 = YIELD` (arg ignored): task requests to run again “soon”.
- `2 = SLEEP_MS` (arg = ms): task requests to run again no earlier than `now + ms`.

Host parsing:

- `kind = (uint32_t)((uint64_t)ret >> 32)`
- `arg = (uint32_t)((uint64_t)ret & 0xFFFFffffu)`

Invalid `kind` values must be treated as `DONE` and should be logged as an app error (best-effort).

### New WASM import module: `portal_microtask`

Apps interact with the host scheduler via imports:

- `microtaskClearAll() -> i32`
  - Clears all tasks for the current app.
  - Returns `kWasmOk` always (idempotent).

- `microtaskStart(start_after_ms: i32, period_ms: i32, flags: i32) -> i32`
  - Creates a host-scheduled task and returns a positive handle.
  - `start_after_ms`:
    - `0`: eligible immediately (subject to yield/battery clamping).
    - `>0`: due at `now + start_after_ms`.
  - `period_ms`:
    - `0`: non-periodic (one-shot unless the task yields/sleeps).
    - `>0`: periodic (see periodic semantics).
  - `flags`:
    - v0 must be `0`. Non-zero is `kWasmErrInvalidArgument`.
  - Errors:
    - `kWasmErrInvalidArgument` for negatives or invalid flags.
    - `kWasmErrNotReady` if the app does not export `portalMicroTaskStep`.
    - `kWasmErrInternal` if the host cannot allocate a slot/handle.

- `microtaskCancel(handle: i32) -> i32`
  - Cancels a task by handle.
  - Returns `kWasmOk` if cancelled, `kWasmErrNotFound` if the handle is invalid/already complete.

## Scheduling semantics

### Step contract

Each call to `portalMicroTaskStep` must:

- Do a small bounded amount of work.
- Return quickly (no long loops, no blocking sleeps).
- Return an action that indicates when to run again (or completion).

### Yield semantics and battery clamping

`YIELD` must not cause a “run immediately again” loop. The host enforces a minimum reschedule delay:

- `kDefaultYieldDelayMs = 50` (initial default).

Host behavior:

- If the task returns `YIELD`, schedule it at `now + kDefaultYieldDelayMs` (or at the next period boundary if periodic; see below).
- If the task returns `SLEEP_MS(0)`, the host may clamp it to the same minimum delay to prevent busy loops.

Apps that need faster cadence (e.g. animations) must explicitly request it with `SLEEP_MS(16)` or similar, understanding it may cost battery.

### Periodic semantics

If a task is spawned with `period_ms > 0`, it is periodic.

Baseline policy (battery-friendly):

- After a periodic task runs, its next due time is based on a period boundary rather than “run immediately until caught up”.
- If the system was asleep or busy and misses multiple periods, the host schedules the next run at the **first future** boundary, not multiple back-to-back runs.

Interaction with returned actions:

- If a periodic task returns `YIELD`, the host schedules it at the next period boundary.
- If it returns `SLEEP_MS(ms)` and that would schedule later than the next period boundary, the explicit sleep wins for that cycle.
- If it returns `DONE`, the task is removed.

### Error handling

If `portalMicroTaskStep` traps/throws an exception:

- The host must handle it like other host→WASM dispatch failures (disable dispatch for that module, trigger crash recovery for uploaded apps if applicable).
- All MicroTasks for the current module must be cleared on unload/switch so they cannot leak into the next app.

If `portalMicroTaskStep` returns an invalid action encoding:

- Treat as `DONE` and log an error (best-effort).

## Fairness and budgeting

To preserve responsiveness, the host scheduler must cap how much work it runs per wake:

- Default: `max_steps_per_wake = 16` (tunable).
- Must be round-robin: do not always start from slot 0.

This prevents a large task set from starving touch handling and other host events.

## Event loop integration (host)

The host event loop should:

- Block on the event queue with a timeout computed from:
  - next due MicroTask time
  - next touch poll time (see below)
  - idle sleep deadline
- When it wakes:
  - Dispatch queued host events.
  - Poll/process touch if due.
  - Run due MicroTasks up to the budget.
  - Repeat (recompute deadlines).

Touch polling vs. battery:

- When touch is inactive, touch polling should be less frequent.
- When touch is active (pressed/dragging), touch polling should be more frequent to keep gestures responsive.

Current firmware values (v0 implementation):

- Inactive touch polling interval: **50ms**
- Active touch polling interval: **20ms**

Rationale:

- 50ms idle polling lowers baseline wakeups compared with a tight spin loop.
- 20ms active polling keeps drag/flick responsiveness acceptable while a task workload is running.

## Zig SDK expectations

The Zig SDK (`../zig-sdk`) must provide helpers so apps can implement MicroTasks as stateful structs:

- A `Task.from(T, *T)` helper that produces a type-erased vtable-backed task object.
- A dispatch table mapping host handles to tasks.
- An implementation of the `portalMicroTaskStep` export inside the SDK (via `@export` from the SDK’s entrypoint, similar to `portalAlloc`), so apps do not need to define it.

The SDK must also provide thin wrappers for `portal_microtask` imports so apps can `start`/`cancel` tasks.

## Compatibility notes

- MicroTasks are per-app. The host must clear all tasks when switching/unloading the current WASM module.
