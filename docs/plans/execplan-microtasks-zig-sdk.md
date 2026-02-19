# Implement Host-Scheduled MicroTasks (No `ppTick`) and Expose to Zig SDK

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and `Outcomes & Retrospective` must be kept up to date as work proceeds.

There is no `PLANS.md` file in this repository at the time of writing.

## Purpose / Big Picture

After this change, Paper Portal WASM apps can create **MicroTasks**: small, cooperative units of work that execute on the app’s main thread, do a bounded amount of work, then yield so other parts of the system can run.

Key requirement: **do not use `ppTick`** to drive app logic. The firmware must not periodically call a `ppTick` callback because it causes unnecessary wakeups and is bad for battery life.

Instead:

- The firmware (C++) owns a **MicroTask scheduler** that only wakes when there is real work to do (MicroTask due time, touch input polling, queued host events, idle timeout).
- When a MicroTask is due, the firmware calls a WASM export: `portalMicroTaskStep(handle, now_ms)` which advances one step and returns a scheduling action (done / yield / sleep).
- Zig apps use `../zig-sdk` to author stateful tasks as `struct`s. `../zig-sdk` also provides the `portalMicroTaskStep` export (similar to `portalAlloc`/`portalFree`) so individual apps do not implement the dispatcher manually.

Observable result: a demo WASM app can start multiple MicroTasks (periodic, delayed, and chunked) and the device remains responsive to touch while tasks run, and when tasks are idle the firmware does not wake periodically just to call into WASM.

## Progress

- [x] (2026-02-18) Update plan to reflect “no `ppTick`” and host-driven scheduling.
- [x] (2026-02-18) Write `docs/specs/spec-microtasks.md` describing semantics + battery intent.
- [ ] (2026-02-18) Implement firmware MicroTask scheduler + integrate into `main/host/event_loop.cpp`.
- [ ] (2026-02-18) Add WASM import module `portal_microtask` (start/cancel/clear).
- [ ] (2026-02-18) Add host→WASM `portalMicroTaskStep` export + calling plumbing in `WasmController`.
- [ ] (2026-02-18) Implement Zig SDK module `sdk/microtask.zig` (authoring + dispatch + FFI).
- [ ] (2026-02-18) Add Zig tests for encoding/dispatch.
- [ ] (2026-02-18) Add `apps/microtask-demo/` proving behavior without `ppTick`.
- [ ] (2026-02-18) Validate on hardware; record evidence and battery-related observations.

## Surprises & Discoveries

Record unexpected behaviors that affect semantics, correctness, or battery usage.

- Observation: (fill in)
  Evidence: (fill in)

## Decision Log

- Decision: MicroTask scheduling and wakeups are implemented in firmware (C++). Apps do not rely on `ppTick`.
  Rationale: Removing periodic tick wakeups improves battery life; the host can sleep/wait until the next due time or input/event.
  Date/Author: 2026-02-18 / GPT-5.2

- Decision: App-side tasks are stateful Zig `struct` instances with a `step()` method; the host sees only integer handles. The WASM module exports a single dispatcher `portalMicroTaskStep(handle, now_ms)` that routes to the right task instance, and for Zig apps this dispatcher export is implemented by `../zig-sdk` (similar to `portalAlloc`/`portalFree`).
  Rationale: This keeps the host ABI small/stable while enabling idiomatic stateful tasks in Zig, matching patterns like `ui.Scene.from(...)`.
  Date/Author: 2026-02-18 / GPT-5.2

- Decision: `.yield` is defined as “run again after a battery-friendly minimum quantum” (default 50ms), not “run immediately”.
  Rationale: Immediate reschedules can create a busy-loop that drains battery; tasks that truly need high rate must opt in via smaller sleeps.
  Date/Author: 2026-02-18 / GPT-5.2

- Decision: Use wrap-around-safe `u32` time arithmetic derived from the signed `now_ms: i32` host time.
  Rationale: The millisecond timer wraps; scheduling must be correct across wrap.
  Date/Author: 2026-02-18 / GPT-5.2

## Outcomes & Retrospective

Fill in as milestones complete. At completion, include:

- What shipped (APIs, examples, docs).
- What was deferred (e.g., deeper power management, touch interrupt support).
- What to improve next (e.g., better coalescing, jitter handling, profiling).

## Context and Orientation

Repository roots:

- Firmware repo: `/Users/mika/code/paperportal/portal`
- Zig SDK repo: `/Users/mika/code/paperportal/zig-sdk`

Relevant firmware code:

- Host event loop that currently dispatches periodic ticks:
  - `/Users/mika/code/paperportal/portal/main/host/event_loop.cpp`
  - It currently uses `kTickIntervalMs = 50` and calls `wasm->CallTick(now_ms)` on that cadence.
- WASM app contract constants:
  - `/Users/mika/code/paperportal/portal/main/wasm/app_contract.h`
- WASM controller that looks up exports and dispatches host→WASM calls:
  - `/Users/mika/code/paperportal/portal/main/wasm/wasm_controller.h`
  - `/Users/mika/code/paperportal/portal/main/wasm/wasm_controller_instance.cpp`
  - `/Users/mika/code/paperportal/portal/main/wasm/wasm_controller_dispatch.cpp`
- WAMR native API modules (WASM imports) live under:
  - `/Users/mika/code/paperportal/portal/main/wasm/api/`
  - This plan adds a new import module named `portal_microtask` following the existing pattern in that directory.

Relevant Zig SDK patterns:

- `ui.Scene.from(...)` shows the type-erased pointer + vtable pattern used for stateful objects:
  - `/Users/mika/code/paperportal/zig-sdk/sdk/ui/scene.zig`
- FFI extern declarations live in:
  - `/Users/mika/code/paperportal/zig-sdk/sdk/ffi.zig`

Terms:

- MicroTask: cooperative task; must not block; advances in small steps.
- Due time: the earliest millisecond when the task is eligible to run again.
- Yield: request to run again soon, but not immediately (to avoid busy loops).

## Plan of Work

### 1) Write the MicroTasks spec (source of truth)

Create:

  /Users/mika/code/paperportal/portal/docs/specs/spec-microtasks.md

This spec must define semantics independent of implementation details. It must include:

- Why `ppTick` is not used and the battery rationale.
- The host-driven scheduling model and what causes the device to wake.
- The MicroTask step contract: one step must be small and must return quickly.
- The scheduling actions:
  - DONE: remove task
  - YIELD: reschedule after default yield quantum (50ms unless changed)
  - SLEEP_MS(ms): reschedule no earlier than `now + ms` (host may clamp `ms` to avoid immediate busy loops)
- Periodic tasks:
  - how period is defined (ms)
  - drift policy (schedule forward to the next future boundary; do not “catch up” by running multiple times back-to-back)
- Wrap-around-safe time comparisons (treat `now` as wrapping `u32`).
- Budgeting/fairness: host runs at most N steps per wake to keep the system responsive.

### 2) Implement the firmware MicroTask scheduler

Add:

  /Users/mika/code/paperportal/portal/main/host/microtask_scheduler.h
  /Users/mika/code/paperportal/portal/main/host/microtask_scheduler.cpp

Core responsibilities:

- Provide a stable `int32_t` handle space with generation counters.
- Track for each task:
  - occupied/gen
  - next_run_ms (u32)
  - period_ms (u32; 0 for non-periodic)
- APIs:
  - `int32_t Start(uint32_t start_after_ms, uint32_t period_ms)`
  - `int32_t Cancel(int32_t handle)`
  - `void ClearAll()`
  - `bool HasDue(uint32_t now_ms)` / `uint32_t NextDueMs()` (for sleeping decisions)
  - `void RunDue(WasmController *wasm, uint32_t now_ms, int max_steps)` (calls into WASM)

Hot-path constraints (battery and responsiveness):

- No allocations while running due tasks.
- Round-robin fairness: do not starve higher-index tasks.
- Clamp `.yield` to `kDefaultYieldDelayMs` (default 50ms).
- If the WASM call throws/raises an exception, treat it like other dispatch failures: disable dispatch and stop running tasks until recovery.

### 3) Define `portalMicroTaskStep` and plumb host→WASM calls

Update:

  /Users/mika/code/paperportal/portal/main/wasm/app_contract.h

Add:

- Export name constant:

    constexpr const char *kExportPortalMicroTaskStep = "portalMicroTaskStep";

- Document the signature:

    // int64_t portalMicroTaskStep(int32_t handle, int32_t now_ms)

- Define an action encoding that is simple to parse in both C++ and Zig:

    high 32 bits: kind (0=DONE, 1=YIELD, 2=SLEEP_MS)
    low  32 bits: arg  (ms for SLEEP_MS; 0 otherwise)

Extend `WasmController`:

- In `/Users/mika/code/paperportal/portal/main/wasm/wasm_controller.h`:
  - Cache `exports_.microtask_step` (optional).
  - Add:
    - `bool HasMicroTaskStepHandler() const;`
    - `bool CallMicroTaskStep(int32_t handle, int32_t now_ms, int64_t *out_action);`

Because `i64` return marshalling can be subtle, add an explicit prototyping milestone:

- In `../zig-sdk`, implement a temporary `portalMicroTaskStep` export that returns a fixed known `i64` pattern (via `@export`, similar to `portalAlloc`), and build a demo app that links the SDK so the host can call it.
- Call it from the host and confirm `out_action` sees the exact bits expected (bit-for-bit).
- Only then finalize the action encoding and wire scheduler rescheduling to it.

### 4) Add the WASM import module `portal_microtask`

Add:

  /Users/mika/code/paperportal/portal/main/wasm/api/microtask.cpp

Register as module `portal_microtask` and expose:

- `microtaskClearAll() -> i32`
- `microtaskStart(start_after_ms: i32, period_ms: i32, flags: i32) -> i32` (returns handle)
- `microtaskCancel(handle: i32) -> i32`

Rules:

- Validate arguments (non-negative; reasonable upper bounds if needed).
- Return `kWasmOk` / `kWasmErrInvalidArgument` / `kWasmErrNotFound` with `wasm_api_set_last_error(...)`.
- `flags` is reserved for future; must be 0 in v0.

Wire registration:

- `/Users/mika/code/paperportal/portal/main/wasm/api.h`: add `wasm_api_register_microtask()`.
- `/Users/mika/code/paperportal/portal/main/wasm/api/core.cpp`: call it in `wasm_api_register_all()`.
- `/Users/mika/code/paperportal/portal/main/CMakeLists.txt`: add the new source.

### 5) Integrate scheduler into the host event loop (battery)

Modify:

  /Users/mika/code/paperportal/portal/main/host/event_loop.cpp

Changes:

- Stop calling `wasm->CallTick(now_ms)` as a periodic heartbeat. If `HostEventType::Tick` remains for internal use, it must not drive WASM.
- Replace the fixed short `xQueueReceive(..., pdMS_TO_TICKS(10))` rhythm with a calculated timeout based on the earliest next deadline:
  - next due MicroTask time
  - next touch poll time (see below)
  - idle sleep deadline
  - any other required periodic host-only work
- Run touch polling and MicroTasks only when they are due, not on every spin.
- On app unload/switch/exit and devserver reload paths (where `clear_custom_gestures()` is called), also call `microtask_scheduler().ClearAll()` so tasks cannot leak across apps.

Touch polling (battery tradeoff):

- Add an explicit touch poll schedule:
  - When no touch is active: poll at a lower rate (start with 50ms; adjust if UX suffers).
  - When touch is active: poll at a higher rate (start with 20–50ms) to preserve gesture responsiveness.
- Document the chosen rates and rationale in `spec-microtasks.md`.

### 6) Add Zig SDK support (authoring + dispatch + FFI)

In `/Users/mika/code/paperportal/zig-sdk` add:

- `/Users/mika/code/paperportal/zig-sdk/sdk/microtask.zig` implementing:
  - `Action` type and `encode()` returning `i64` per the ABI.
  - `Task` with `from(T, *T)` vtable generation:
    - required: `pub fn step(self: *T, now_ms: u32) anyerror!Action`
    - optional: `pub fn onCancel(self: *T) void`
  - A dispatch table keyed by host handles:
    - `register(handle, Task)`
    - `unregister(handle)`
    - `dispatch(handle, now_ms_i32) i64` called by `portalMicroTaskStep`
  - Thin wrappers:
    - `start(task, start_after_ms, period_ms) -> Error!Handle` which calls host `microtaskStart` and registers mapping.
    - `cancel(handle)` which unregisters mapping then calls host `microtaskCancel`.

Implement the WASM export inside the SDK (not in apps):

- Update `/Users/mika/code/paperportal/zig-sdk/sdk/internal/exports.zig` to add:
  - `pub fn portalMicroTaskStep(handle: i32, now_ms: i32) callconv(.c) i64 { ... }` which forwards to `sdk/microtask.zig` dispatch.
- Update `/Users/mika/code/paperportal/zig-sdk/sdk.zig` to `@export` `portalMicroTaskStep` with strong linkage (similar to `portalAlloc`/`portalFree`), so apps linking the SDK automatically provide the dispatcher export.

Update:

- `/Users/mika/code/paperportal/zig-sdk/sdk/ffi.zig` to add `extern "portal_microtask"` functions.
- `/Users/mika/code/paperportal/zig-sdk/sdk.zig` to re-export `microtask`.
- `/Users/mika/code/paperportal/zig-sdk/README.md` and add `/Users/mika/code/paperportal/zig-sdk/docs/microtasks-module-guide.md`.

### 7) Add tests and a demo app

Zig tests:

- Add `test` blocks in `/Users/mika/code/paperportal/zig-sdk/sdk/microtask.zig` validating:
  - action encoding correctness
  - dispatch table behavior (register/dispatch/unregister)
  - cancellation behavior (best-effort)

Demo app:

- Create `/Users/mika/code/paperportal/portal/apps/microtask-demo/` (same style as other apps in `apps/`).
- The demo must export:
  - `ppInit`, `ppShutdown`, `ppOnGesture` (optional but recommended).
  - It must not rely on `ppTick`.
- `portalMicroTaskStep` must come from linking `../zig-sdk` (the demo must not define it).
- Demo tasks:
  - periodic heartbeat (500ms)
  - delayed one-shot (2000ms)
  - chunked worker (small increments with `.yield` or short sleeps)
- The demo should log enough to verify scheduling and should remain responsive to taps while tasks run.

## Concrete Steps

1) Firmware build:

    cd /Users/mika/code/paperportal/portal
    idf.py build

2) Zig SDK build + tests:

    cd /Users/mika/code/paperportal/zig-sdk
    zig fmt .
    zig build
    zig test sdk/microtask.zig

3) Demo app build:

    cd /Users/mika/code/paperportal/portal/apps/microtask-demo
    zig build

4) On-device validation (record exact steps and logs in `Artifacts and Notes`):

- Upload and run `microtask-demo.wasm` via your existing devserver workflow.
- Verify:
  - logs show heartbeat and delayed firing at expected approximate times
  - taps are handled while chunked work progresses
  - after tasks complete (or if you stop scheduling), the firmware does not call into WASM periodically just to “tick”

## Validation and Acceptance

Accepted when:

- MicroTasks work without `ppTick` being called by the firmware.
- The firmware calls `portalMicroTaskStep` only when tasks are due (or when tasks intentionally request frequent scheduling).
- The event loop blocks/sleeps using computed deadlines rather than waking on a fixed short period when idle.
- `zig build` succeeds in `/Users/mika/code/paperportal/zig-sdk` and the demo app.
- The demo remains responsive to input while tasks run.

## Idempotence and Recovery

- Re-running `idf.py build`, `zig build`, and `zig test` is safe.
- If `portalMicroTaskStep` return marshalling is wrong, fall back to the prototype milestone and verify bit-exact return decoding before proceeding.
- If an exception occurs in MicroTask dispatch, follow existing crash recovery paths (devserver uploaded app recovery) and ensure MicroTasks are cleared on unload.

## Artifacts and Notes

Keep short evidence here:

- Build/test transcripts.
- A small example log sequence from `microtask-demo`.
- Any battery-related observations (e.g., reduced wake frequency when idle; changes in responsiveness with chosen touch poll intervals).

## Interfaces and Dependencies

- New firmware-side WASM import module: `portal_microtask`.
- New WASM export (provided by `../zig-sdk` for Zig apps): `portalMicroTaskStep(handle, now_ms) -> i64`.
- Zig SDK adds `sdk.microtask` as the recommended app-facing interface for MicroTasks.
