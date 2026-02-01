# Repository Guidelines

## Project Overview
`portal` is an ESP-IDF (C/C++) firmware project targeting `esp32s3` (see `dependencies.lock`). It embeds a WASM entrypoint (`main/assets/entrypoint.wasm`) and integrates third-party components (LovyanGFX, FastEPD, WAMR) fetched locally.

## Project Structure & Module Organization
- `main/`: application code and the ESP-IDF component registration (`main/CMakeLists.txt`).
- `main/wasm/`: WASM controller + host API surface (`main/wasm/api/*.cpp`).
- `main/assets/`: binary assets embedded into the firmware (e.g., `entrypoint.wasm`, `settings.wasm`).
- `apps/`: Zig/WASM app sources (e.g., `apps/launcher`, `apps/settings`).
- `docs/`: project documentation and schemas.
  - `docs/specs/`: technical specifications for features (keep these aligned with the implementation).
  - `docs/plans/`: execution plans for planned/ongoing work (create/update as the plan changes).
  - `docs/*.schema.json`: JSON schemas used by firmware tooling/config (e.g., `config.schema.json`, `app-manifest.schema.json`).
- `components/`: vendored dependencies cloned by `fetch-deps.sh` (often ignored by git; treated as generated/vendor code).
- `managed_components/`: ESP-IDF component-manager dependencies fetched by ESP-IDF tooling.
- `patches/`: local patch files applied to third-party components.
- `sdkconfig`, `sdkconfig.defaults`, `partitions.csv`: ESP-IDF configuration and partition layout.
- `build/`: generated build output (ignored).

## Documentation Guidelines
- `docs/specs/` and `docs/plans/` are part of the project’s “source of truth” alongside the code: changes to behavior, configuration, interfaces, or build/deploy workflow must come with corresponding doc updates.
- Keep specs and plans current. Stale docs cause wasted time, incorrect assumptions during debugging, and regressions when features evolve.

## Build, Test, and Development Commands
Prereq: ESP-IDF installed and exported (so `IDF_PATH` and `idf.py` are available).
- `./fetch-deps.sh`: clones pinned component versions into `components/` (requires network).
- `idf.py set-target esp32s3`: sets the target (usually one-time per workspace).
- `idf.py build`: compiles firmware into `build/`.
- `./conf.sh [profile]`: reconfigure using `sdkconfig.defaults` plus optional `sdkconfig.<profile>` (e.g., `release`, `heap`).
- `idf.py -p /dev/tty.usbserial-XXXX flash`: flashes to device.
- `idf.py -p /dev/tty.usbserial-XXXX monitor`: serial monitor (use after flash).
- `idf.py menuconfig`: adjust `sdkconfig` options.

## Coding Style & Naming Conventions
- Indentation: 4 spaces; avoid tabs.
- C++ style: keep braces and spacing consistent with nearby code (K&R-like).
- Naming: `PascalCase` for types, `snake_case` for C-style/ABI functions, `kConstant` for constants, `g_` for globals (follow existing WASM API patterns).
- When changing WASM APIs, keep signatures stable and update native symbol registration in the relevant `main/wasm/api/*.cpp`.

## Testing Guidelines
No dedicated unit-test harness in this repo. Treat these as required checks:
- `idf.py build` for compile/link validation.
- Device smoke test for changes affecting display/touch/network/WASM: flash + monitor and verify the affected feature end-to-end.

## Commit & Pull Request Guidelines
- Commit messages in history are short, sentence-case summaries (no required prefixes). Prefer “Touch: fix …” / “Display: …” for clarity.
- PRs should include: what changed, how it was tested (`idf.py build`, device steps), target hardware (e.g., M5PaperS3), and screenshots/photos for UI/display changes when practical.
