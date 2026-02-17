# Power Management

This document describes Paper Portal power/sleep behavior implemented in firmware.

## Auto-sleep (inactivity)

The firmware automatically powers off after **3 minutes (180,000ms)** of no touch input.

- **Activity definition:** any touch down, movement while pressed, touch-up, or continued press (hold) counts as activity and resets the timer.
- **Devserver exemption:** while the devserver is starting or running, auto-sleep is disabled (the timer is kept alive).

Implementation:

- Timeout constant: `kIdleSleepTimeoutMs` in `main/host/event_loop.cpp`
- Sleep action: `power_service::power_off(true)` (renders embedded `sleepimage.jpg` then enters deep sleep)

## Manual sleep

The system sleep gesture triggers `power_service::power_off(true)` immediately, independent of the inactivity timer.
