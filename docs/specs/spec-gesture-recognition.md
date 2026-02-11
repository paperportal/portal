# Custom polyline gesture recognition specification

This document specifies the firmware-side recognition of app-registered single-touch “polyline” gestures and the corresponding WASM API.

## Goals

- Allow a WASM app to register 1+ polyline gestures (a sequence of waypoints).
- Recognize gestures using low-rate, noisy touch polling (currently ~50 ms host loop tick).
- Notify the app **only on touch Up** when a registered gesture is recognized.
- Allow multiple registered gestures to track the same touch simultaneously and select one winner on Up.

## Touch event model

The host polls touch state in the main event loop and derives a single-pointer event stream:

- `Down`: first observed press
- `Move`: position changes while pressed (emitted only when coordinates change)
- `Up`: first observed release after an active press
- `Cancel`: synthesized reset (e.g. app unload); clears tracking without recognition

All custom gesture recognition is driven by this derived event stream.

## Gesture definition

Each registered gesture consists of:

- `id` (string, <= 47 bytes)
- `points` (>= 2 waypoints), each `{ f32 x; f32 y; }`
- `tolerance_px` (pixels, `> 0`)
- `fixed`:
  - `fixed = 1`: `points` are absolute screen coordinates; the gesture only becomes active if `Down` is within tolerance of the first waypoint
  - `fixed = 0`: `points` are offsets relative to the `Down` position (anchor); the gesture is always active on `Down`
- `priority` (higher wins)
- `max_duration_ms` (0 disables the duration constraint)
- `segment_constraint_enabled` (see “Constraints”)

## Recognition algorithm

### Waypoint progression

Each gesture tracks a `target_index` (the next waypoint to reach).

- On `Down`, `target_index` starts at 0.
- On each `Move` or `Up`, if the touch position is within `tolerance_px` of the current target waypoint, `target_index` advances.
- A gesture is eligible only if all waypoints have been reached in order and the `Up` position is within tolerance of the final waypoint.

### Constraints

Constraints are enforced with “K consecutive failures” to tolerate noisy sampling:

- `K = 2` consecutive violations deactivate the gesture for the current touch.

Constraints:

- **Approaching rule**: while tracking a target waypoint, the squared distance to that waypoint must not increase for `K` consecutive samples.
  - The host “arms” this rule only after it observes initial movement toward the new target waypoint (and while still within tolerance of the previously-reached waypoint, it does not count approach failures). This reduces false failures at corners.
- **Near-segment rule** (optional): while tracking, the touch must remain within `tolerance_px` of the segment connecting the previous waypoint and the current target waypoint.

### Duration constraint

If `max_duration_ms != 0`, the touch must end (`Up`) within that duration from `Down`, otherwise the gesture is not eligible.

## Winner selection (on Up)

On `Up`, all eligible gestures are scored by squared distance from the `Up` position to the final waypoint (lower is better).

The host selects:

1. highest `priority`
2. then lowest score
3. then lowest handle (stable tie-break)

If no gesture is eligible, no custom gesture event is emitted.

## App notification (ppOnGesture)

When a winner exists, the host emits a single `ppOnGesture` call:

- `kind = kGestureCustomPolyline` (100)
- `x,y` = Up coordinates
- `dx,dy` = Up minus Down
- `duration_ms` = touch duration
- `flags` = winning `gesture_handle`

## WASM import API (`m5_gesture`)

### `gestureClearAll() -> i32`

Clears all **app-registered** gestures and resets tracking state.

Notes:

- The firmware may register additional **system gestures** that are always active; these are not removed by `gestureClearAll()`.

### `gestureRegisterPolyline(...) -> i32`

Registers a new gesture and returns a positive handle on success.

Arguments:

- `id`: NUL-terminated string (1..47 bytes)
- `points_ptr`: bytes of `PointF` array (`{ f32 x; f32 y; }`, little-endian)
- `points_len`: length in bytes (must be divisible by 8; at least 16 bytes / 2 points)
- `fixed`: `0` or `1`
- `tolerance_px`: `f32`, `> 0`
- `priority`: `i32`
- `max_duration_ms`: `i32`, `>= 0` (0 disables)
- `options`: bitfield
  - bit 0: **disable** segment constraint when set (default is enabled)

Notes:

- Each successful registration returns a distinct handle (even if `id` is the same).
- The `id` is informational (for debugging); recognition results are reported by handle.

### `gestureRemove(handle: i32) -> i32`

Removes a single gesture by handle.

## App lifecycle safety

On app unload (exit/switch/crash recovery), the host clears all custom gestures to prevent cross-app leakage.

System gestures (registered by the firmware) remain active across app unloads.
