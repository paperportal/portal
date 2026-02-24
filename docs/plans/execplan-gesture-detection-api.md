# Add Custom Polyline Gesture Recognition (C++ Host + Zig SDK)

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and `Outcomes & Retrospective` must be kept up to date as work proceeds.

There is no `PLANS.md` file in this repository at the time of writing.

## Purpose / Big Picture

After this change, a WASM app can register one or more single-touch “polyline” gestures (a sequence of waypoints) with the firmware, and the firmware will recognize them using low-rate, noisy touch sampling and notify the app **only on touch Up** when a gesture is recognized. Multiple registered gestures can track the same touch simultaneously; on Up the firmware selects a winner by priority (higher wins) and then by “closeness” to the final waypoint (lower distance wins). This is intended to be e‑ink friendly (polling at ~50ms in the current host event loop) and robust to skipped move samples.

Apps observe recognition via the existing `portalGesture(...)` callback, using a new `kind` for “custom polyline gesture” and the winning gesture’s handle in the `flags` parameter. Apps register/clear/remove gestures via a new host-imported WASM API (and a Zig SDK wrapper in `../zig-sdk`).

## Progress

- [x] (2026-02-10) Add/refresh a gesture recognition spec in `docs/specs/` and align it with current firmware touch polling (`main/input/touch_tracker.*` and `main/host/event_loop.cpp`).
- [x] (2026-02-10) Implement a small C++ gesture engine (`TouchEvent` stream + recognizers + winner selection) under `main/input/`.
- [x] (2026-02-10) Wire the gesture engine into `main/host/event_loop.cpp:process_touch()` so it receives Down/Move/Up/Cancel derived from `TouchTracker` output.
- [x] (2026-02-10) Extend the app contract gesture kinds to include a “custom polyline” kind and define how `flags` carries the winning gesture handle.
- [x] (2026-02-10) Add a new WASM native module (e.g. `m5_gesture`) with APIs to register/remove/clear gestures; register it in `wasm_api_register_all()`.
- [x] (2026-02-10) Update the Zig SDK at `../zig-sdk` to expose the new gesture registration API (FFI declarations + safe wrapper module).
- [x] (2026-02-10) Add a small demo/test WASM app (or a devserver-uploadable sample) that registers 2–3 gestures and logs/visualizes recognition on Up.
- [ ] (2026-02-10) Validate on hardware: confirm recognition with low sampling (default `kTickIntervalMs = 50`) and confirm winner selection + duration/tolerance behavior.

## Surprises & Discoveries

- Observation: Touch is currently polled (not interrupt-driven) in `main/host/event_loop.cpp` with `kTickIntervalMs = 50`, and the host’s “built-in” gestures (tap/flick/drag/long-press) are synthesized inside `process_touch()` using `TouchTracker` state.
  Evidence: `/Users/mika/code/paperportal/portal/main/host/event_loop.cpp` (functions `process_touch`, `emit_gesture`).
- Observation: WASM apps can also read raw touch state directly via the `m5_touch` native module, but the host already has an event callback path (`portalGesture`) suitable for “notify only on Up”.
  Evidence: `/Users/mika/code/paperportal/portal/main/wasm/api/touch.cpp` (module `m5_touch`) and `/Users/mika/code/paperportal/portal/main/wasm/app_contract.h` (export `portalGesture`).

## Decision Log

- Decision: Deliver custom polyline recognition results via the existing `portalGesture(kind, x, y, dx, dy, duration_ms, now_ms, flags)` callback, using a new `kind` value and placing the winning gesture handle into `flags`.
  Rationale: This reuses the repo’s existing “gesture event” plumbing (`emit_gesture` → `WasmController::CallOnGesture`) and keeps the host→app event surface small.
  Date/Author: 2026-02-10 / GPT-5.2
- Decision: Expose registration/removal of gestures via a new WAMR native module (proposed name `m5_gesture`) and wrap it in the Zig SDK under `../zig-sdk` (even though the prompt called it `../wasm-sdk`, which does not exist in this repo today).
  Rationale: The repository’s existing public app SDK is `../zig-sdk` (used by built-in apps in `apps/`); adding a new module there matches established patterns and keeps WASM imports consistent.
  Date/Author: 2026-02-10 / GPT-5.2
- Decision: Keep the existing built-in host gestures (tap/flick/drag/long-press) behavior unchanged in v0, and emit custom polyline recognition as an additional `portalGesture` event on Up when a registered gesture wins.
  Rationale: Keeping existing host gestures unchanged reduces risk and makes it easier to validate the new recognizer in isolation.
  Date/Author: 2026-02-10 / GPT-5.2
- Decision: Interpret `options` bit 0 as “disable segment constraint when set” (default enabled when `options == 0`).
  Rationale: The v0 API always includes an `options` argument, so using “disable when set” preserves a safe default without requiring callers to set a bit.
  Date/Author: 2026-02-10 / GPT-5.2

## Outcomes & Retrospective

- Implemented a firmware-side polyline gesture engine with priority+closeness winner selection on Up.
- Added `m5_gesture` WASM imports and Zig SDK wrappers, plus a devserver-uploadable `apps/gesture-demo`.
- Remaining: on-device validation and potential tuning of tolerances/constraints based on real touch noise.

## Context and Orientation

This firmware project lives at `/Users/mika/code/paperportal/portal` and runs a WAMR-based WASM runtime on the device.

Relevant code paths:

- Touch sampling and raw touch details:
  - `/Users/mika/code/paperportal/portal/main/input/touch_tracker.h`
  - `/Users/mika/code/paperportal/portal/main/input/touch_tracker.cpp`
  - This provides `TouchDetail` with `x`, `y`, `prev_x`, `prev_y`, `base_x`, `base_y`, `base_msec`, and a `state` bitfield compatible with M5Unified’s `touch_state_t`.
- Host event loop and existing gesture synthesis:
  - `/Users/mika/code/paperportal/portal/main/host/event_loop.cpp`
  - The function `process_touch(WasmController*, GestureState&, int32_t now)` polls touch, derives high-level gesture events, and calls `emit_gesture()`, which dispatches to WASM via `WasmController::CallOnGesture`.
- WASM “app contract” (exports from WASM, called by host):
  - `/Users/mika/code/paperportal/portal/main/wasm/app_contract.h`
  - The host calls `portalGesture` (optional) with the fixed signature and gesture kind constants.
- WASM native APIs (imports into WASM, implemented by host):
  - `/Users/mika/code/paperportal/portal/main/wasm/api/*.cpp`
  - Modules are registered in `/Users/mika/code/paperportal/portal/main/wasm/api/core.cpp` via `wasm_api_register_all()`.
- Zig SDK used by apps (this repo’s “WASM SDK”):
  - `/Users/mika/code/paperportal/zig-sdk/sdk/ffi.zig` declares `extern` imports.
  - `/Users/mika/code/paperportal/zig-sdk/sdk/*.zig` provide safe wrappers.

Important constraints:

- The host currently polls touch at ~50ms (`kTickIntervalMs`) so the recognizer must tolerate sparse move events.
- Memory allocation on each move event should be avoided; allocate at registration time and during Down only.
- The firmware is single-touch for gesture purposes today (the host uses the first touch detail entry); gesture APIs should keep a `pointerId` field but v0 will ignore non-zero pointer ids.

## Plan of Work

### 1) Add a gesture recognition spec aligned to this repo

Create a new spec file at:

    /Users/mika/code/paperportal/portal/docs/specs/spec-gesture-recognition.md

This spec should adapt the user-provided “Flexible Gesture Recognizer” proposal to the repository’s current architecture:

- Touch events originate from `TouchTracker` polling in `process_touch()`, not from an interrupt-driven stream.
- Recognition results are delivered through `portalGesture` (existing export) to keep the host→app event surface small and to reuse the existing dispatch plumbing.
- Gesture registration must be via a new native import module (host functions callable from WASM).

The spec must include:

- A definition of `TouchEvent` as used internally in firmware (Down/Move/Up/Cancel), and how each is derived from the current polling logic:
  - Down: transition from “not active” to “pressed”.
  - Move: while active and pressed, when `(x,y)` changes.
  - Up: transition from “active & pressed” to “not pressed”.
  - Cancel: when the host aborts tracking due to app switch/unload or display/touch becoming unavailable.
- Gesture definition fields (id, points, tolerance, fixed/relative, priority, maxDuration) and validation rules.
- The recognition rules (waypoint within tolerance, “approaching” constraint, optional “near segment” constraint, duration constraint, and K-consecutive-failure policy) with defaults.
- Winner selection on Up among all recognizers.
- The WASM-facing API:
  - New `portalGesture.kind` constant for custom polyline recognition (proposed name `kGestureCustomPolyline`).
  - Encoding: `flags = gesture_handle` (positive int32), `x,y` are Up coordinates, `dx,dy` are `(up - down)`, `duration_ms` is `(up_time - down_time)`, `now_ms` is current host time.
  - Behavior: the host emits this event only when a registered gesture wins on Up; it emits nothing if no registered gesture matches.
- The WASM import module and function signatures to register/remove gestures (see section 4).

### 2) Implement the gesture engine in C++ (firmware side)

Add new source files under `main/input/` (mirroring the existing `touch_tracker.*` placement). Suggested filenames:

- `/Users/mika/code/paperportal/portal/main/input/gesture_engine.h`
- `/Users/mika/code/paperportal/portal/main/input/gesture_engine.cpp`

Implement these concepts (names can vary, but the interfaces and responsibilities must exist):

Internal types:

- `enum class TouchType { Down, Move, Up, Cancel };`
- `struct TouchEvent { TouchType type; int pointer_id; float x; float y; uint64_t time_ms; };`
- `struct PointF { float x; float y; };`
- `struct GestureDef { int32_t handle; char id[48]; std::vector<PointF> points; float tolerance_px; bool fixed; int32_t priority; uint32_t max_duration_ms; bool segment_constraint_enabled; };`
- `struct TrackState { bool active; PointF anchor; uint64_t start_time_ms; size_t target_index; float last_dist_to_target; float max_progress; int consecutive_fail_approach; int consecutive_fail_segment; PointF down_pos; PointF last_pos; };`

Key behaviors:

- Registration:
  - Store gestures in a container keyed by `handle` (monotonic positive int32 handle assigned by the host).
  - Copy and validate gesture definitions on registration; reject invalid inputs with `kWasmErrInvalidArgument` and a descriptive last-error string.
- Per-touch tracking:
  - On Down: resolve absolute points (if relative) by capturing anchor; decide Active vs Inactive based on “fixed requires Down near first point; relative always active”.
  - On Move/Up: advance `target_index` when within tolerance of the next waypoint (skip-friendly).
  - Apply the “approaching” rule and (optionally) “near segment” rule; only fail after K consecutive violations (default K = 2).
  - Apply duration constraint continuously; if exceeded, fail and go Inactive.
- On Up:
  - Determine if eligible: reached all waypoints and Up within tolerance of last point, and duration satisfied.
  - Compute score: distance from Up to last point (lower is better).
- Winner selection:
  - Among eligible recognizers, pick the highest `priority`, then lowest `score`.
  - Return the winning handle (or 0 / -1 to represent “no winner” internally).
- Reset:
  - After Up or Cancel, reset all recognizers to inactive and clear per-touch tracking.

Implementation constraints for embedded robustness:

- Avoid allocations on Move events. Pre-store points; allocate per-gesture only at registration time.
- Use squared-distance comparisons to avoid `sqrt` where possible, but ensure the score uses consistent units (it can be squared distance to avoid sqrt, as long as the tie-break rule is documented and consistent).
- Keep tolerances in pixels and treat input coordinates as floats (even though the current touch coordinates are integers).

### 3) Integrate the gesture engine into the host event loop

Modify:

    /Users/mika/code/paperportal/portal/main/host/event_loop.cpp

Specifically, extend `process_touch(WasmController *wasm, GestureState &state, int32_t now)` so it:

1. Continues to call `touch_tracker().update(...)` once per loop as it does today.
2. Derives `TouchEvent` transitions (Down/Move/Up) from the existing `state.active` + `pressed` logic:
   - Use `det.x/det.y` for position and `now` for `time_ms` (converted to `uint64_t`).
   - Emit Move events only when the position changes (to limit noise).
3. Feeds those `TouchEvent`s to the gesture engine each loop.
4. On Up (before resetting `state.active`), asks the gesture engine for a winning handle.
5. If a handle wins, dispatch a custom `portalGesture` event:
   - `kind = kGestureCustomPolyline` (new constant).
   - `x,y` = Up position.
   - `dx,dy` = Up minus Down (store Down in the engine track state, or reuse existing `state.start_x/state.start_y`).
   - `duration_ms` = Up time minus Down time.
   - `flags` = winning handle.

App-switch/unload safety:

- When switching apps or exiting apps in `host_event_loop_run()`, call a new gesture-engine reset/clear method so a prior app’s registered gestures do not leak into the next app. The safe places are immediately after `wasm->UnloadModule()` in both the app-switch and app-exit code paths.

### 4) Define the WASM-facing API (native imports) and register it

Add a new WAMR native API module implementation:

- `/Users/mika/code/paperportal/portal/main/wasm/api/gesture.cpp`

Expose a small, v0-stable API surface under a new module name (proposed `m5_gesture`):

- `gestureClearAll() -> i32` clears all registered gestures for the current app.
- `gestureRegisterPolyline(id_z: const char*, points: u8*, points_len: size_t, fixed: i32, tolerance_px: f32, priority: i32, max_duration_ms: i32, options: i32) -> i32`
  - Returns a positive `handle` on success.
  - Returns `kWasmErr*` on failure and sets last-error message.
  - `points` is an array of `{ f32 x; f32 y; }` pairs in little-endian (8 bytes per point). `points_len` must be a multiple of 8 and represent at least 2 points.
  - `options` bit 0: enable/disable the “near segment” constraint (default enabled if bit not provided; document the exact default).
- `gestureRemove(handle: i32) -> i32` removes a single gesture by handle.

Then update registration plumbing:

- Add `bool wasm_api_register_gesture(void);` to `/Users/mika/code/paperportal/portal/main/wasm/api.h`.
- In `/Users/mika/code/paperportal/portal/main/wasm/api/core.cpp`, include `wasm_api_register_gesture()` in `wasm_api_register_all()`.
- In `/Users/mika/code/paperportal/portal/main/CMakeLists.txt`, add the new `wasm/api/gesture.cpp` and any new `input/gesture_*.cpp` sources to `SRCS`.

### 5) Extend contract constants

Update:

- `/Users/mika/code/paperportal/portal/main/wasm/app_contract.h`

Add a new gesture kind constant (choose a value not currently used; for example `100`):

- `kGestureCustomPolyline = 100`

Document in that header (and in the new spec) that for this kind:

- `flags` is the winning `gesture_handle`.
- `x,y` are the Up coordinates.
- `dx,dy` are Up minus Down.
- `duration_ms` is the touch duration.

### 6) Update Zig SDK (`../zig-sdk`) to expose the new API

Even though the prompt called it `../wasm-sdk`, this repo’s SDK is:

    /Users/mika/code/paperportal/zig-sdk

Implement:

1. FFI declarations in:

       /Users/mika/code/paperportal/zig-sdk/sdk/ffi.zig

   Add:

   - `pub extern "m5_gesture" fn gestureClearAll() i32;`
   - `pub extern "m5_gesture" fn gestureRegisterPolyline(id: [*:0]const u8, points_ptr: [*]const u8, points_len: i32, fixed: i32, tolerance_px: f32, priority: i32, max_duration_ms: i32, options: i32) i32;`
   - `pub extern "m5_gesture" fn gestureRemove(handle: i32) i32;`

2. A safe wrapper module:

       /Users/mika/code/paperportal/zig-sdk/sdk/gesture.zig

   This should:

   - Define `pub const PointF = struct { x: f32, y: f32 };`
   - Define `pub const Options = packed struct(u32) { segment_constraint_enabled: bool = true, _pad: u31 = 0 };` (or an equivalent explicit bitmask API).
   - Provide `registerPolyline(id: [:0]const u8, points: []const PointF, opts: ...) Error!i32` returning the host handle.
   - Provide `remove(handle: i32) Error!void` and `clearAll() Error!void`.
   - Include small helpers for interpreting `portalGesture`:
     - A `GestureKind` enum mirroring `pp_contract` constants (including the new `custom_polyline` kind).
     - A note that `flags` carries the handle for `custom_polyline`.

3. Export it from:

       /Users/mika/code/paperportal/zig-sdk/sdk.zig

   Add:

   - `pub const gesture = @import("sdk/gesture.zig");`

### 7) Add a demo app or devserver test app

Goal: make the new behavior obviously testable without needing new hardware tooling.

Option A (recommended): add a small WASM app under this firmware repo that can be run via devserver upload.

- Create a Zig app in `/Users/mika/code/paperportal/portal/apps/gesture-demo` that:
  - Calls `core.begin()`.
  - Registers 2–3 gestures (for example: a simple “L” shape and a simple “V” shape) using `sdk.gesture.registerPolyline`.
  - Implements `portalGesture` to:
    - On `kGestureCustomPolyline`, log the handle and (optionally) draw the recognized gesture name on screen.
    - Ignore tap events to avoid confusion.
  - Keep the UI minimal: clear screen and draw text.

Option B: use `zig-app-template` (outside this repo) to build a WASM and run it via the existing devserver “RunUploadedWasm” path; document the exact build and upload steps used in validation.

### 8) Documentation updates

Update or create:

- `/Users/mika/code/paperportal/portal/docs/specs/spec-gesture-recognition.md` (new)
- If needed, add a short section to `/Users/mika/code/paperportal/portal/docs/specs/spec-ui-toolkit.md` describing how apps should interpret `portalGesture` events and how custom gestures differ (only fire on Up; use `flags` for handle).

## Concrete Steps

All commands are run from `/Users/mika/code/paperportal/portal` unless stated otherwise.

Build built-in apps (if you added `apps/gesture-demo`, build it too):

    cd apps/launcher && zig build
    cd ../settings && zig build
    cd ../gesture-demo && zig build

Build firmware:

    idf.py build

If validating via devserver upload, ensure devserver is running (from Settings) and upload the demo WASM using the existing devserver workflow you use today. Record the exact steps and observed logs in `Artifacts and Notes`.

## Validation and Acceptance

Acceptance is met when all of the following are true:

1. Existing built-in apps (`launcher`, `settings`) still function normally (tap to activate buttons, etc.).
2. A demo WASM app can register multiple polyline gestures and receives **exactly one** `portalGesture` event per recognized touch sequence (only on Up), with:
   - `kind == kGestureCustomPolyline`
   - `flags` equal to the handle returned at registration
   - `x,y` equal to the Up position
3. Winner selection works:
   - When two gestures are both eligible, the one with higher `priority` wins.
   - When priorities tie, the one whose final waypoint is closer to the Up position wins.
4. Robustness properties are observable:
   - Gestures still recognize when move samples skip intermediate pixels (coarse movement).
   - Recognition fails when the touch path deviates far from the intended segment for more than K consecutive samples (when segment constraint is enabled).
   - `max_duration_ms` prevents recognition when exceeded (when non-zero).

## Idempotence and Recovery

- Registration API must be safe to call multiple times; the host should either return distinct handles per registration or explicitly reject duplicates (pick one behavior and document it in the spec and in the Zig wrapper).
- `gestureClearAll()` should return the system to a clean state (no registered gestures; no active tracking).
- On app switch or app exit, the host must clear all registered gestures to avoid cross-app leakage.

## Artifacts and Notes

As implementation proceeds, record:

- Any on-device logs demonstrating a gesture recognition event (including handle, Up coordinates, duration).
- Any debugging visualizations (if added) showing the recognized gesture id/handle.
- Any performance or memory observations (heap logs before/after registering multiple gestures).

At the bottom of this file, add a short dated note whenever you revise major decisions (what changed and why).

### Notes

- (2026-02-10) Implemented the v0 API end-to-end; `options` bit 0 disables the segment constraint; custom gestures are cleared on app unload (exit/switch/crash recovery) to prevent cross-app leakage.
