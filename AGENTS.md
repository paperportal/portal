# Repository Guidelines

## Project Overview
`portal` is an ESP-IDF (C/C++) firmware project that enables installing and running WASM applications on low-power e-ink devices.

## Project Structure & Module Organization
- `main/`: application code and the ESP-IDF component registration (`main/CMakeLists.txt`).
- `main/wasm/`: WASM controller + host API surface (`main/wasm/api/*.cpp`).
- `main/assets/`: binary assets embedded into the firmware (e.g., `entrypoint.wasm`, `settings.wasm`).
- `apps/`: Zig/WASM app sources – `apps/launcher` becomes `entrypoint.wasm`, and `apps/settings` becomes `settings.wasm`.
- `docs/`: project documentation and schemas.
  - `docs/specs/`: technical specifications for features (keep these aligned with the implementation).
  - `docs/plans/`: execution plans for planned/ongoing work (create/update as the plan changes).
  - `docs/*.schema.json`: JSON schemas used by firmware tooling/config (e.g., `config.schema.json`, `app-manifest.schema.json`).
- `components/`: vendored dependencies cloned by `fetch-deps.sh` (ignored by git).
- `managed_components/`: ESP-IDF component-manager dependencies fetched by ESP-IDF tooling.
- `patches/`: local patch files applied to third-party components.
- `sdkconfig`, `sdkconfig.defaults`, `partitions.csv`: ESP-IDF configuration and partition layout.
  - If changes are needed to configs, make them to `sdkconfig.defaults` and regenerate `sdkconfig`.
- `build/`: generated build output (ignored by git).

## Documentation Guidelines
- `docs/specs/` and `docs/plans/` are part of the project’s “source of truth” alongside the code: changes to behavior, configuration, interfaces, or build/deploy workflow must come with corresponding doc updates.
- Keep specs and plans current. Stale docs cause wasted time, incorrect assumptions during debugging, and regressions when features evolve.

## Build, Test, and Development Commands
Prereq: ESP-IDF installed and exported (so `IDF_PATH` and `idf.py` are available).
- `./fetch-deps.sh`: clones pinned component versions into `components/` (requires network).
- `idf.py build`: compiles firmware into `build/`.
- `./conf.sh [profile]`: reconfigure using `sdkconfig.defaults` plus optional `sdkconfig.<profile>` (e.g., `release`, `heap`).
- `idf.py -p /dev/tty.usbserial-XXXX flash`: flashes to device.
- `idf.py -p /dev/tty.usbserial-XXXX monitor`: serial monitor (use after flash).

## Coding Style & Naming Conventions
- Indentation: 4 spaces; avoid tabs.
- C++ style: keep braces and spacing consistent with nearby code (K&R-like).
- Naming: `PascalCase` for types, `snake_case` for C-style/ABI functions, `kConstant` for constants, `g_` for globals (follow existing WASM API patterns).
- Zig naming (for `apps/`): `lowerCamelCase` for all functions, except functions that return a type (Zig), which use `PascalCase`.
- When changing WASM APIs, keep signatures stable and update native symbol registration in the relevant `main/wasm/api/*.cpp`.

## Paper Portal SDK
- Public Paper Portal interfaces are defined in `main/wasm/api.h` and `main/wasm/api/*`.
- When modifying API ensure that Zig SDK at `../zig-sdk` is kept up to date.
