# Add Wake-From-Sleep Unlock Gate (Wake Image + Swipe)

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and `Outcomes & Retrospective` must be kept up to date as work proceeds.

There is no `PLANS.md` file in this repository at the time of writing.

## Purpose / Big Picture

After this change, when the device boots as the result of waking from deep sleep, it checks how long it was asleep. If the sleep duration is more than 30 seconds, the firmware immediately shows the embedded `wakeimage.jpg` as a “wake lock” screen, and then requires a left-to-right swipe within 10 seconds to continue booting into the normal launcher experience. If no qualifying swipe occurs within the 10 second window, the firmware shows the embedded `sleepimage.jpg` and powers off back into deep sleep.

User-visible outcome: the device no longer “pocket wakes” into a fully running system after long sleeps; it only stays awake when the user explicitly unlocks it with a swipe.

## Progress

- [ ] (2026-02-11) Read current sleep/power-off path and confirm how deep-sleep wake is detected (wake cause and/or reset reason).
- [ ] (2026-02-11) Add a persistent (RTC-retained) sleep-entry timestamp and a “valid marker” written just before deep sleep starts.
- [ ] (2026-02-11) Implement the wake gate: if slept > 30s, show `wakeimage.jpg`, then wait up to 10s for a left→right swipe.
- [ ] (2026-02-11) If no swipe, show `sleepimage.jpg` and power off again; if swipe, continue normal boot without extra prompts.
- [ ] (2026-02-11) Update specs/docs describing the wake gate behavior and constants.
- [ ] (2026-02-11) Validate on hardware (sleep > 30s and < 30s cases; swipe success/failure; repeatability).

## Surprises & Discoveries

- Observation: The firmware already embeds both `sleepimage.jpg` and `wakeimage.jpg` into the binary.
  Evidence: `/Users/mika/code/paperportal/portal/main/CMakeLists.txt` (binary data for `assets/sleepimage.jpg` and `assets/wakeimage.jpg`).
- Observation: The current “sleep” action used by the system gesture calls `power_service::power_off(true)`, which draws the sleep image and then enters deep sleep.
  Evidence: `/Users/mika/code/paperportal/portal/main/host/event_loop.cpp` (system sleep gesture handler) and `/Users/mika/code/paperportal/portal/main/services/power_service.cpp` (`power_off` calls `esp_deep_sleep_start()` after drawing the image).
- Observation: Touch is already polled at ~50ms in the host event loop using `TouchTracker`, and swipe-like gestures can be detected from touch start/end deltas.
  Evidence: `/Users/mika/code/paperportal/portal/main/host/event_loop.cpp` (`kTickIntervalMs = 50` and `process_touch`) and `/Users/mika/code/paperportal/portal/main/input/touch_tracker.*`.

## Decision Log

- Decision: Measure sleep duration using `esp_rtc_get_time_us()` captured immediately before entering deep sleep and stored in RTC-retained memory via `RTC_DATA_ATTR`.
  Rationale: `esp_rtc_get_time_us()` is backed by the RTC timer and continues across deep sleep, so `now_rtc_us - sleep_entry_rtc_us` yields the time spent asleep without needing wall-clock time or NVS.
  Date/Author: 2026-02-11 / GPT-5.2
- Decision: Implement the unlock swipe detection as a small, local state machine (touch down → touch up) that checks for a sufficiently large positive `dx` (left-to-right) and limited `dy`, rather than registering a global “system gesture” in `GestureEngine`.
  Rationale: Global system gestures are always active and would interfere with app UX by consuming or emitting events for normal left-to-right swipes; the wake gate should be isolated to the boot/unlock screen only.
  Date/Author: 2026-02-11 / GPT-5.2
- Decision: Run the wake gate as early as possible during boot (in `app_main` before starting the WASM runtime / event loop).
  Rationale: The requirement says to display `wakeimage.jpg` “immediately” after wake. Running before launcher initialization minimizes delays and avoids showing launcher UI briefly before the lock screen.
  Date/Author: 2026-02-11 / GPT-5.2

## Outcomes & Retrospective

- (TBD) At completion, summarize the observed wake-gate behavior on hardware and any tuning needed for swipe thresholds or display timing.

## Context and Orientation

This firmware project lives at `/Users/mika/code/paperportal/portal` and runs a WAMR-based WASM runtime on the device.

Key concepts used in this plan:

- “Deep sleep”: the ESP-IDF low-power mode entered via `esp_deep_sleep_start()`; on wake, the device cold-boots and runs `app_main()` again.
- “RTC-retained memory”: a special memory region that keeps its contents across deep sleep. In ESP-IDF, globals tagged with `RTC_DATA_ATTR` live there. This is not guaranteed to persist across full power loss.
- “Wake gate”: the short lock-screen flow that runs on boot-after-sleep to decide whether to continue startup or to power off again.
- “Swipe left-to-right”: a single-finger touch that begins at some `(x0,y0)` and ends at `(x1,y1)` where `x1 - x0` is positive and exceeds a minimum distance. We also bound vertical drift (`abs(y1 - y0)`) to avoid treating a mostly-vertical swipe as an unlock.

Relevant code and assets:

- Power off and sleep image drawing:
  - `/Users/mika/code/paperportal/portal/main/services/power_service.h`
  - `/Users/mika/code/paperportal/portal/main/services/power_service.cpp`
  - `power_service::power_off(true)` draws embedded `sleepimage.jpg` (from `_binary_sleepimage_jpg_*`) via `Display::current()` and the `main/wasm/api/display.h` API surface, then deep-sleeps.
- Wake/sleep images embedded into firmware:
  - `/Users/mika/code/paperportal/portal/main/assets/sleepimage.jpg`
  - `/Users/mika/code/paperportal/portal/main/assets/wakeimage.jpg`
  - `/Users/mika/code/paperportal/portal/main/CMakeLists.txt` (`target_add_binary_data` for both JPGs).
- Startup sequence:
  - `/Users/mika/code/paperportal/portal/main/main.cpp` (`app_main()` mounts SD, sets up WASM controller, starts host event loop).
- Touch sampling utilities:
  - `/Users/mika/code/paperportal/portal/main/input/touch_tracker.h`
  - `/Users/mika/code/paperportal/portal/main/input/touch_tracker.cpp`
  - `touch_tracker().update(&paper_display(), now_ms)` yields `TouchDetail` with coordinates and pressed state.
- Display initialization and drawing:
  - `/Users/mika/code/paperportal/portal/main/m5papers3_display.h`
  - `/Users/mika/code/paperportal/portal/main/m5papers3_display.cpp`
  - `paper_display_ensure_init()` selects/initializes the active display driver; `power_service` draws via `Display::current()` (e.g. `drawJpgFit`, `display`/`fullUpdateSlow`).

## Plan of Work

Implement a boot-time wake gate that is triggered only when both of the following are true:

1) The current boot is the result of waking from deep sleep (based on `esp_sleep_get_wakeup_cause()`).
2) The firmware previously recorded a sleep-entry timestamp immediately before entering deep sleep, and the computed duration exceeds 30 seconds.

The wake gate flow is:

1) Draw embedded `wakeimage.jpg` full-screen and flush to the e-ink display.
2) For up to 10 seconds, poll touch input and detect a left-to-right swipe (single-finger down→up with sufficient `dx`).
3) If swipe detected before timeout: continue normal boot (launcher/WASM runtime starts as usual).
4) If timeout expires without swipe: call `power_service::power_off(true)` to show `sleepimage.jpg` and deep-sleep again.

To support accurate sleep duration measurement, add RTC-retained state written at sleep entry and consumed at wake:

- A “magic” marker indicating that the timestamp is valid and was written by this firmware.
- The sleep-entry RTC timestamp in microseconds, captured by `esp_rtc_get_time_us()` immediately before deep sleep.
- On boot, compute `slept_us = now_rtc_us - sleep_entry_rtc_us`, then clear the magic marker so future warm resets do not re-trigger the gate.

### Files to change / add

1) Extend `power_service` to record sleep entry and to draw `wakeimage.jpg`:

- Update `/Users/mika/code/paperportal/portal/main/services/power_service.h`:
  - Add `void record_sleep_entry();`
  - Add `bool consume_last_sleep_duration_ms(int32_t *out_ms);` (or `uint32_t`, but keep the API explicit about ranges)
  - Add `void show_wake_image_best_effort();` (mirrors existing sleep-image logic)
- Update `/Users/mika/code/paperportal/portal/main/services/power_service.cpp`:
  - Add `RTC_DATA_ATTR` globals:
        static uint32_t g_sleep_marker_magic;
        static uint64_t g_sleep_entry_rtc_us;
  - Define `constexpr uint32_t kSleepMarkerMagic = 0x5050534C;` (ASCII-ish “PPSL” or any fixed constant) and compare against it.
  - Implement `record_sleep_entry()`:
    - Set `g_sleep_entry_rtc_us = esp_rtc_get_time_us();`
    - Set `g_sleep_marker_magic = kSleepMarkerMagic;`
  - Implement `consume_last_sleep_duration_ms(...)`:
    - If `g_sleep_marker_magic != kSleepMarkerMagic`, return false.
    - Read `const uint64_t now_rtc_us = esp_rtc_get_time_us();`
    - Clear `g_sleep_marker_magic = 0;` (consume semantics).
    - If `now_rtc_us <= g_sleep_entry_rtc_us`, return false (clock anomaly / wrap).
    - Compute `slept_ms = (now_rtc_us - g_sleep_entry_rtc_us) / 1000` and return it via `out_ms`.
  - Modify `power_off(bool show_sleep_image)` to call `record_sleep_entry()` immediately before entering deep sleep (before drawing/sleeping the display is fine; the key is “just before deep sleep starts”).
  - Add embedded wake image accessors:
    - Declare `extern const uint8_t _binary_wakeimage_jpg_start[];` and `_end[]` similarly to the sleep image.
    - Implement `show_wake_image_best_effort()` by copying the structure of `show_sleep_image_best_effort()` but reading the wake image symbols instead.
    - Ensure it does `display.clearDisplay()`, `drawJpg(...)`, then `display.display()` and `display.waitDisplay()`.

2) Add a small wake-gate module that runs in `app_main`:

- Add new files:
  - `/Users/mika/code/paperportal/portal/main/host/wake_gate.h`
  - `/Users/mika/code/paperportal/portal/main/host/wake_gate.cpp`
- Update `/Users/mika/code/paperportal/portal/main/CMakeLists.txt` to compile the new `.cpp`.

In `/Users/mika/code/paperportal/portal/main/host/wake_gate.h`, define:

    // Returns true to continue boot; returns false if it powered off.
    bool host_wake_gate_run();

In `/Users/mika/code/paperportal/portal/main/host/wake_gate.cpp`, implement:

- `host_wake_gate_run()`:
  - Read wake cause via `esp_sleep_get_wakeup_cause()`.
  - If wake cause is `ESP_SLEEP_WAKEUP_UNDEFINED`, return true (normal power-on boot; no gate).
  - Call `power_service::consume_last_sleep_duration_ms(&slept_ms)`. If it returns false, return true (we cannot prove a prior sleep entry; do not block boot).
  - If `slept_ms <= 30000`, return true (short sleep; no gate).
  - Otherwise:
    - Call `power_service::show_wake_image_best_effort()` to draw `wakeimage.jpg`.
    - Call a helper `wait_for_unlock_swipe(timeout_ms=10000)`:
      - Ensure `paper_display_ensure_init()` succeeds before polling touch; if it fails, fall back to powering off (best-effort safety).
      - Poll in a loop with a small delay (e.g. 20–50ms): `touch_tracker().update(&paper_display(), now_ms)`.
      - Track a single active touch:
        - On press begin: record `(x0,y0,t0)` and set active.
        - While pressed: update last `(x1,y1)`.
        - On release: compute `dx = x1 - x0`, `dy = y1 - y0`, `dt = t1 - t0`.
      - Accept as “unlock swipe” when:
        - `dx >= kSwipeMinDxPx` (recommend: `screen_w / 3` with a minimum clamp such as 120px),
        - `abs(dy) <= kSwipeMaxDyPx` (recommend: `screen_h / 6` or a fixed value like 120px),
        - Optionally, `dt <= kSwipeMaxDurationMs` (recommend: 4000ms) to avoid extremely slow drags counting as unlock.
      - If accepted, return true; otherwise keep looping until timeout.
    - If `wait_for_unlock_swipe(...)` returns true: return true (continue boot).
    - Else: call `power_service::power_off(true)` and return false.

3) Call the wake gate early in boot:

- Update `/Users/mika/code/paperportal/portal/main/main.cpp`:
  - Include the new header (`host/wake_gate.h`).
  - Call `if (!host_wake_gate_run()) { return; }` immediately after `mem_utils::init();` and before NVS init / SD mount / starting the host event loop.

4) Optional but recommended: record sleep entry for other deep-sleep entry points.

If WASM apps can enter deep sleep via the WASM power API, also record the sleep entry timestamp for those paths:

- Update `/Users/mika/code/paperportal/portal/main/wasm/api/power.cpp`:
  - In `powerDeepSleepUs(...)`, call `power_service::record_sleep_entry();` just before `esp_deep_sleep_start();`.
  - Do not record for light sleep (light sleep does not reboot and does not need this gate).

5) Documentation updates

- Add a spec describing this behavior, or update an existing one:
  - Create `/Users/mika/code/paperportal/portal/docs/specs/spec-wake-gate.md` (recommended) describing:
    - When the wake gate triggers (deep-sleep wake + slept > 30s).
    - The exact assets used (`wakeimage.jpg`, `sleepimage.jpg`).
    - The 10 second timeout and the swipe direction requirement.
    - The rationale (avoid accidental wake battery drain).
  - Optionally add a short note in `/Users/mika/code/paperportal/portal/docs/specs/spec-main-interface.md` referencing the wake gate and where it appears in the user flow.

## Concrete Steps

All commands are run from `/Users/mika/code/paperportal/portal` unless stated otherwise.

Build firmware:

    idf.py build

Flash and monitor (adjust serial port):

    idf.py -p /dev/tty.usbserial-XXXX flash
    idf.py -p /dev/tty.usbserial-XXXX monitor

When adding debug logs for the gate, prefer a short, searchable prefix, for example:

    ESP_LOGI("wake_gate", "slept_ms=%" PRId32 " cause=%d (gate=%d)", slept_ms, (int)cause, gate_enabled ? 1 : 0);

Expected behavior transcript in logs (example; exact text is not important):

    I (.. ) wake_gate: slept_ms=45000 cause=... (gate=1)
    I (.. ) wake_gate: waiting for unlock swipe (timeout=10000ms)
    I (.. ) wake_gate: unlock swipe detected; continuing boot

Or, if no swipe:

    I (.. ) wake_gate: unlock timeout; powering off

## Validation and Acceptance

Acceptance criteria (human-verifiable on hardware):

1) Long-sleep path (gate triggers):
   - Put the device into sleep using the existing system sleep gesture (it should show `sleepimage.jpg` and power off).
   - Wait at least 31 seconds.
   - Wake the device.
   - The first screen shown is `wakeimage.jpg` (not the launcher).
   - If you perform a left-to-right swipe within 10 seconds, the device continues booting into the normal launcher.
   - If you do nothing (or swipe wrong direction), after 10 seconds the device shows `sleepimage.jpg` and powers off again.

2) Short-sleep path (gate does not trigger):
   - Put the device into sleep.
   - Wake it again within 30 seconds.
   - The device boots normally (no wake image lock screen).

3) Repeatability:
   - Perform the long-sleep test twice in a row; ensure the gate does not “stick” across warm resets due to stale RTC marker data.

## Idempotence and Recovery

- The RTC marker must be “consumed” on boot (cleared after reading). This prevents repeated gating after unrelated resets.
- If the wake gate accidentally triggers on every boot due to incorrect marker handling, recovery should be possible via a full power cycle (RTC memory clears on full power loss) or by flashing firmware that clears the marker on boot.
- If touch or display initialization fails during the wake gate, the safe fallback is to power off (best-effort battery protection).

## Artifacts and Notes

- The embedded JPG assets are already present:
  - `/Users/mika/code/paperportal/portal/main/assets/sleepimage.jpg`
  - `/Users/mika/code/paperportal/portal/main/assets/wakeimage.jpg`
- The wake gate should not depend on SD card contents or WASM app startup; it must be able to run on a minimal boot.
- Keep the unlock thresholds conservative (avoid false negatives) but directional (avoid accepting mostly vertical swipes). Tune on real hardware if needed.

## Interfaces and Dependencies

Required new/updated interfaces (names may be adjusted, but the behavior must match):

- In `/Users/mika/code/paperportal/portal/main/services/power_service.h`:
  - `void record_sleep_entry();`
  - `bool consume_last_sleep_duration_ms(int32_t *out_ms);`
  - `void show_wake_image_best_effort();`
- In `/Users/mika/code/paperportal/portal/main/host/wake_gate.h`:
  - `bool host_wake_gate_run();`

Key ESP-IDF dependencies used:

- `esp_sleep_get_wakeup_cause()` from `esp_sleep.h` to distinguish normal boots from deep-sleep wake boots.
- `esp_rtc_get_time_us()` (declared in `soc/rtc.h`) to measure elapsed time across deep sleep.
- `RTC_DATA_ATTR` (from `esp_attr.h`) to retain marker/timestamp across deep sleep.
