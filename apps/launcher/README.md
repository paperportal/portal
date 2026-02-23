# Paper Portal Launcher App

This Zig project implements the Paper Portal launcher UI. It compiles to WebAssembly and is embedded into the firmware as the main entrypoint app (`main/assets/entrypoint.wasm`).

## Features

- Fast startup: draws the header immediately (embedded PNG) and then loads apps in the background.
- Status indicators in the header: battery, Wi‑Fi/AP mode, and devserver indicator (when running).
- App grid UI: fixed 3‑column grid of tiles below the header, with icon + title and tap hit‑testing.
- Built‑in Settings tile: always shown as the first tile; opens the `settings` app via `core.openApp`.
- SD card app catalog: loads/saves `/sdcard/portal/apps.json` (schema v1) for fast startup.
- `.papp` auto‑installation: scans `/sdcard/portal/apps/` for `.papp` files, installs them, then deletes the package files.
- Package validation:
  - Validates `manifest.json` (supported versions, UUID `id`, `#.#.#` version string, `sha256:<hex>` checksum).
  - Computes SHA‑256 over extracted `app.wasm` and compares to `manifest.json.checksum`.
- Optional authenticity via Ed25519 + TOFU pinning: verifies signatures when present and pins the publisher public key per app `id` in the catalog; once pinned, updates must be signed by the same key.
- Crash‑safe installs: uses `.staging-*` and `.backup-*` directories and performs recovery/cleanup on startup.

## Code structure

- `src/main.zig`: WebAssembly entrypoint (`main`) and exports (`ppOnGesture`, `ppShutdown`) and initial header draw.
- `src/controller.zig`: launcher state machine (mount FS, load catalog, prune missing apps, scan/install `.papp`, rebuild/redraw grid, handle taps).
- `src/ui/title_bar.zig`: header rendering and status icons.
- `src/ui/grid.zig`: grid layout, tile drawing, icon loading (embedded settings icon vs `icon.png` from installed apps), hit‑testing.
- `src/ui/popup.zig`: simple modal popup (used during installs).
- `src/catalog.zig`: `/sdcard/portal/apps.json` load/save + pinned publisher key persistence.
- `src/installer.zig`: `.papp` install pipeline (manifest read/parse, signature verification + pinning, ZIP extraction, checksum check, staging/activate, catalog upsert).
- `src/zip_reader.zig`: minimal ZIP reader/extractor (store/deflate) with path-safety checks.
- `src/manifest.zig`: `manifest.json` parsing and validation.
- `src/signing.zig`: Ed25519 verification and TOFU pinning helper.
- `src/paths.zig`: SD card path constants/helpers.
- `src/assets/`: embedded UI assets (header image, settings icon).

## Build

From `apps/launcher/`: `zig build`

This writes the compiled WASM binary to `main/assets/entrypoint.wasm`.
