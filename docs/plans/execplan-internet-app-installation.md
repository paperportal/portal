# Enable Internet App Installation (Catalogs + Installer App)

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and `Outcomes & Retrospective` must be kept up to date as work proceeds.

There is no `PLANS.md` file in this repository at the time of writing.

## Purpose / Big Picture

After this change, a device can install apps from the internet directly on-device, without manually copying a `.papp` file onto the SD card. In the launcher grid there is a new tile, “Install Apps”, which opens a built-in installer app. The installer app loads one or more app catalogs (JSON) from the internet, shows a list of apps that are not already installed, and allows the user to view details and install an app. During installation, the UI shows progress (download and install).

This also introduces a new `platforms` field in each app’s `manifest.json` to declare which device platforms the app can run on (initially only `m5paper_s3` is supported), and a new `app_catalog_urls` field in `/sdcard/portal/config.json` to configure where the device loads catalogs from (with a default catalog URL used when unset).

## Progress

- [ ] (2026-02-09) Confirm remote “app catalog” JSON format and add a spec + schema.
- [ ] Update app manifest: add `platforms` to schema, docs spec, and manifest parsing/validation (backward compatible).
- [ ] Update portal SD config schema/example: add `app_catalog_urls` with default behavior when missing/empty.
- [ ] Make Wi-Fi connect usable from WASM apps by updating `net_connect` to join using `/sdcard/portal/config.json` credentials when present.
- [ ] Add streaming HTTP WASM API to support large downloads and progress (open/read/close).
- [ ] Update Zig SDK (`../zig-sdk`) to expose the HTTP streaming API (FFI + safe wrapper).
- [ ] Add built-in `app-installer` WASM app (`apps/app-installer`) with UI states: Loading → List → Detail → Installing.
- [ ] Add “Install Apps” tile to launcher grid after installed apps; wire to open built-in `app-installer`.
- [ ] Validate on hardware: configure Wi-Fi + catalog URL(s), install an app, and verify it appears in the launcher after returning.

## Surprises & Discoveries

- Observation: The current WASM HTTP API (`portal_http.http_get`) performs only a single `esp_http_client_read` call and returns the bytes read; there is no guest-visible loop or streaming interface. This makes downloading `.papp` packages reliably (and showing progress) impractical without new APIs.
  Evidence: `/Users/mika/code/paperportal/portal/main/wasm/api/http.cpp` implements `http_get` with one `esp_http_client_read`.
- Observation: `portal_net.net_connect` currently calls `wifi::sta_connect()` without loading credentials from `/sdcard/portal/config.json`. On a fresh device, this may fail unless credentials were previously saved by some other path.
  Evidence: `/Users/mika/code/paperportal/portal/main/wasm/api/net.cpp` calls `wifi::sta_connect()`; `devserver_service` is currently the only code path that loads SD Wi-Fi credentials and calls `wifi::sta_join()`.

## Decision Log

- Decision: Use built-in app id `app-installer` and show a launcher tile labeled “Install Apps”.
  Rationale: Built-in ids already exist (`launcher`, `settings`) and are allowed explicitly by `host_event_loop_request_app_switch`; adding one more keeps the host switch logic simple.
  Date/Author: 2026-02-09 / GPT-5.2
- Decision: Treat `manifest.platforms` as optional for backward compatibility; when missing, assume the app supports `m5paper_s3`.
  Rationale: Existing `.papp` packages and catalog entries should remain installable without repackaging; the platform filter only applies when the field is present.
  Date/Author: 2026-02-09 / GPT-5.2
- Decision: Default catalog URL when `app_catalog_urls` is missing/empty: `https://paperportal.org/catalog.json`.
  Rationale: The schemas already use `paperportal.org` as the canonical domain and it can be backed by GitHub Pages via a custom domain. The default remains configurable via `app_catalog_urls`.
  Date/Author: 2026-02-09 / GPT-5.2
- Decision: Define the remote catalog format as a JSON object with `schema_version: 1` and an `apps` array of entries containing `id`, `manifest` (inline `manifest.json` object), and `package_url` (HTTPS URL to a `.papp`).
  Rationale: This mirrors the existing installed-apps catalog (`/sdcard/portal/apps.json`) while adding the one missing piece needed for installation: where to download the package.
  Date/Author: 2026-02-09 / GPT-5.2

## Outcomes & Retrospective

This section starts empty and must be filled in as milestones complete (what worked, what didn’t, and what is left).

## Context and Orientation

This repository (`/Users/mika/code/paperportal/portal`) contains firmware (ESP-IDF C/C++) in `main/`, including the WASM host APIs under `main/wasm/api/`. It also contains built-in WASM apps compiled from Zig sources in `apps/` and embedded in firmware as `main/assets/*.wasm`; in particular, `apps/launcher` installs to `main/assets/entrypoint.wasm` and `apps/settings` installs to `main/assets/settings.wasm`.

Installed user apps live on the SD card at `/sdcard/portal/apps/<uuid>/` with `app.wasm`, `manifest.json`, and optional assets. The launcher maintains an installed-apps catalog at `/sdcard/portal/apps.json` (JSON), loaded by `apps/launcher/src/apps/catalog.zig`. On startup the launcher supports “local installation” by scanning `/sdcard/portal/apps/` for `.papp` files and installing them via `apps/launcher/src/apps/installer.zig`.

The portal SD card configuration file is `/sdcard/portal/config.json`, currently used for Wi-Fi credentials by `main/services/settings_service.cpp` (schema: `docs/config.schema.json`). The `.papp` packaging specification and `manifest.json` field definitions are documented in `docs/specs/spec-app-packaging.md` and `docs/app-manifest.schema.json`.

Terminology used in this plan:

- **Remote app catalog**: a JSON document fetched via HTTPS that lists installable apps and provides a download URL for each `.papp`.
- **Installed-apps catalog**: `/sdcard/portal/apps.json`, a local index of what is installed (used for fast launcher startup and TOFU pinning metadata).
- **Platform**: a device identifier string. For now, only `m5paper_s3` is recognized.

## Plan of Work

### 1) Specify remote catalogs (docs + schema)

Add a new spec and (recommended) a JSON schema for remote catalogs. Create `docs/specs/spec-app-catalogs.md` describing a JSON object with `schema_version: 1` and an `apps` array. Each entry contains `id` (canonical lower-case UUID string), `manifest` (an inline `manifest.json` object; unknown fields ignored for forward compatibility), and `package_url` (an HTTPS URL to a `.papp` file). Specify normative behavior: invalid entries are skipped with warnings, and duplicates are resolved deterministically by “first wins” based on the configured URL order (later entries with the same `id` are ignored and produce a warning log). Specify platform filtering: when `manifest.platforms` is present and does not include `m5paper_s3`, the app must not be offered for install on this device.

Add `docs/app-catalog.schema.json` (Draft-07) matching the spec so catalog publishers can validate their output similarly to `docs/app-manifest.schema.json` and `docs/config.schema.json`.

Keep the spec aligned with what the installer app actually implements.

### 2) Add `platforms` to app manifests (schema + spec + code)

Docs:

- Update `/Users/mika/code/paperportal/portal/docs/app-manifest.schema.json`:
  - Add optional property `platforms`: array of strings.
  - Constrain items to currently-supported values: `m5paper_s3`.
  - Keep `additionalProperties: true` and do not add `platforms` to the required list (backward compatibility).
- Update `/Users/mika/code/paperportal/portal/docs/app-manifest.example.json` to include:
  - `"platforms": ["m5paper_s3"]`
- Update `/Users/mika/code/paperportal/portal/docs/specs/spec-app-packaging.md`:
  - Describe `platforms` and the install-time behavior (reject install when present and incompatible).

Code:

- Update manifest parsing in `/Users/mika/code/paperportal/portal/apps/launcher/src/apps/manifest.zig`:
  - Parse optional `platforms` from JSON (if present).
  - Validate it is a JSON array of strings.
  - Enforce “only `m5paper_s3` allowed” for now.
  - Decide how to represent it in-memory (recommended: `?[]const []const u8` or a single `bool supports_m5paper_s3` computed during parse to keep memory small).
- Update install validation in `/Users/mika/code/paperportal/portal/apps/launcher/src/apps/installer.zig`:
  - After manifest parse, check platform compatibility and refuse install if incompatible.

Tooling (monorepo sibling, but important to keep builders aligned):

- Update `/Users/mika/code/paperportal/zig-sdk/build_support/manifest.zig` to optionally write `platforms`:
  - Add field to `ManifestFields` (optional; default to `["m5paper_s3"]` when not provided by the caller).
  - Emit JSON key in a stable order.

### 3) Add `app_catalog_urls` to SD config (schema + runtime behavior)

Docs:

- Update `/Users/mika/code/paperportal/portal/docs/config.schema.json`:
  - Add optional root property `app_catalog_urls`:
    - type: array
    - items: string with `format: uri`
  - Keep `additionalProperties: false` at the root (so new keys must be declared).
- Update `/Users/mika/code/paperportal/portal/docs/config.example.json` to include an example:
  - `"app_catalog_urls": ["https://paperportal.org/catalog.json"]`

Runtime behavior (implemented in the installer app, not in native firmware):

The installer app reads `/sdcard/portal/config.json`. If `app_catalog_urls` is missing or empty, it uses the default URL `https://paperportal.org/catalog.json`. If non-empty, it uses exactly those URLs in order (the first URL has highest precedence for duplicate ids).

### 4) Make `net_connect` usable with SD card Wi-Fi credentials

Goal: a WASM app can call `net.connect()` and have the device attempt to join Wi-Fi using `/sdcard/portal/config.json` (same file used for developer mode devserver autostart).

Implementation: in `/Users/mika/code/paperportal/portal/main/wasm/api/net.cpp`, change `net_connect` so it still initializes Wi-Fi and ensures STA mode is started, but then loads credentials via `settings_service::get_wifi_settings(&WifiSettings)`. If credentials are present, call `wifi::sta_join(creds, opts)` in async mode (set `opts.timeout_ms == 0`) so the WASM app can keep rendering its own UI while waiting for `net_is_ready()` to become true. If credentials are missing, set a clear last error message so the installer app can tell the user to configure Wi‑Fi (either by editing `/sdcard/portal/config.json` or via the Settings UI if/when it supports it).

This keeps the guest API unchanged (no new WASM net functions) while making it practical to rely on SD-provided credentials.

### 5) Add streaming HTTP WASM API (host side)

Goal: allow the installer app to download catalogs and `.papp` files incrementally, write to SD card incrementally, and show progress.

Add a streaming interface under module `"portal_http"` in `/Users/mika/code/paperportal/portal/main/wasm/api/http.cpp`, implemented with a small fixed-size handle table (similar in spirit to `/Users/mika/code/paperportal/portal/main/wasm/api/fs.cpp`). The interface must support opening an HTTP GET request, reading response body bytes incrementally, and closing the stream while exposing status code and content length for progress UI.

Concretely, add these new natives:

    int32_t http_stream_open(wasm_exec_env_t, const char *url, int32_t timeout_ms);
    int32_t http_stream_status(wasm_exec_env_t, int32_t handle);
    int32_t http_stream_content_length(wasm_exec_env_t, int32_t handle);
    int32_t http_stream_read(wasm_exec_env_t, int32_t handle, uint8_t *out_ptr, int32_t out_len);
    int32_t http_stream_close(wasm_exec_env_t, int32_t handle);

`http_stream_open` returns a non-negative handle on success (index into the context table) and a WASM error code on failure; all other functions validate handles and set `wasm_api_set_last_error` consistently with existing APIs. `http_stream_read` returns the number of bytes read, `0` on EOF, or a WASM error code on failure.

### 6) Expose HTTP streaming in the Zig SDK

Add Zig SDK bindings so Zig apps can use the new API without manual FFI. In `/Users/mika/code/paperportal/zig-sdk/sdk/ffi.zig`, add the corresponding `extern "portal_http"` declarations. Then add a new wrapper module at `/Users/mika/code/paperportal/zig-sdk/sdk/http.zig` that exposes a small `Stream` type (handle-owning, with `open/read/status/contentLength/close`). Finally, export it from `/Users/mika/code/paperportal/zig-sdk/sdk.zig` as `pub const http = @import("sdk/http.zig");`.

### 7) Add built-in installer app and launcher tile

#### 7.1 Built-in `app-installer` app wiring (native host)

Firmware changes are analogous to the existing Settings app. First, embed `main/assets/app_installer.wasm` by adding `target_add_binary_data(${COMPONENT_LIB} "assets/app_installer.wasm" BINARY)` in `/Users/mika/code/paperportal/portal/main/CMakeLists.txt`. Next, add a loader to `WasmController` by declaring the `_binary_app_installer_wasm_start/_end` symbols and implementing `LoadEmbeddedAppInstaller()` in `/Users/mika/code/paperportal/portal/main/wasm/wasm_controller_load.cpp` (with the corresponding declaration in `/Users/mika/code/paperportal/portal/main/wasm/wasm_controller.h`). Finally, update `/Users/mika/code/paperportal/portal/main/host/event_loop.cpp` so `host_event_loop_request_app_switch` allows the id `app-installer`, and the pending-switch handler loads it via `wasm->LoadEmbeddedAppInstaller()`.

#### 7.2 Launcher grid tile and icon

In the launcher app, add a tile after the installed apps list by appending `{ id_z = "app-installer", title = "Install Apps" }` in `/Users/mika/code/paperportal/portal/apps/launcher/src/apps/controller.zig` after iterating the installed-apps catalog. In the same file, update `Controller.onTap` to treat `"app-installer"` as a built-in app id and call `core.openApp("app-installer", null)`.

For the icon, add a PNG asset under `/Users/mika/code/paperportal/portal/apps/launcher/src/assets/` (for example `icon-install-apps.png`) and update `/Users/mika/code/paperportal/portal/apps/launcher/src/ui/grid.zig` to `@embedFile` it and draw it when the tapped cell id is `"app-installer"`.

Update docs:

Update `/Users/mika/code/paperportal/portal/docs/specs/spec-main-interface.md` to describe the “Install Apps” tile placement (after installed apps) and behavior (opens the built-in installer app).

### 8) Implement the installer app (Zig/WASM)

Create `/Users/mika/code/paperportal/portal/apps/app-installer/` in the same style as `apps/settings` and `apps/launcher`. In `apps/app-installer/build.zig`, use `sdk.addPortalApp` and add an install step that writes the emitted WASM binary to `../../../main/assets/app_installer.wasm` so the firmware can embed it.

Implement `/Users/mika/code/paperportal/portal/apps/app-installer/src/main.zig` as a small state machine. The UI must start by drawing a “Loading…” screen immediately. It then mounts the SD card (`fs.mount()`), initiates Wi‑Fi connection (`net.connect()`), and waits for `net.is_ready()` with an on-screen status and a hard timeout (for example 20–30 seconds). Once network is ready, it reads `/sdcard/portal/config.json` to obtain `app_catalog_urls`, falling back to the default URL when missing/empty, then fetches each catalog URL and merges entries (logging warnings for invalid entries and for duplicate `id`s; duplicates keep the first entry based on URL order). After merging, it filters the list so it does not show apps already installed (based on `/sdcard/portal/apps.json`) and does not show apps incompatible with `m5paper_s3` when `manifest.platforms` is present.

After loading completes, the UI presents a list view showing available apps (at minimum name; version is recommended) with a simple pagination approach if needed. Tapping an app opens a detail view that renders all fields available from the catalog entry (`manifest` fields plus `package_url`). The detail view includes an Install button.

When Install is tapped, the app transitions to an installing view. It downloads the `.papp` from `package_url` using the streaming HTTP API and writes it to a temporary file such as `/sdcard/portal/apps/.download-<id>.papp`, updating the screen with progress (percent and bytes when content length is known; bytes otherwise). After download completes, it installs using the same staging, validation, checksum, and TOFU pinning logic as the launcher installer, updates `/sdcard/portal/apps.json`, and then shows either a success confirmation (with a button to return to launcher) or a failure screen (with a clear message and a retry option).

Code reuse recommendation (to avoid duplicating complex installer logic):

Extract the current launcher installer modules into a shared Zig library folder, then import it from both launcher and app-installer. Concretely, move (or copy first, then move once stable) the modules currently under `/Users/mika/code/paperportal/portal/apps/launcher/src/apps/` into a new shared location such as `/Users/mika/code/paperportal/portal/apps/shared/src/apps/` (at minimum: `catalog.zig`, `installer.zig`, `manifest.zig`, `paths.zig`, `signing.zig`, and `zip_reader.zig`), and update imports in both apps to use the shared modules. Keep launcher-only state/UI code in `/Users/mika/code/paperportal/portal/apps/launcher/src/apps/controller.zig`.

This reuse is important because install correctness (checksum, TOFU pinning, staging/backup recovery) is already implemented and should not diverge.

## Concrete Steps

All commands are run from `/Users/mika/code/paperportal/portal` unless stated otherwise.

Build built-in apps (copies `.wasm` into `main/assets/` via Zig install steps):

    cd apps/launcher && zig build
    cd ../settings && zig build
    cd ../app-installer && zig build

Build firmware:

    idf.py build

Prepare SD card:

- Create `/sdcard/portal/config.json` with Wi-Fi credentials and (optionally) `app_catalog_urls`.
- Ensure SD has `/sdcard/portal/` directory structure expected by the firmware.

Run and observe:

- Boot device into launcher.
- Tap “Install Apps”.
- Observe “Loading…” then the list of installable apps.
- Install an app and verify it appears in launcher after returning.

## Validation and Acceptance

The feature is accepted when the following are all true on an `m5paper_s3` device:

- With `app_catalog_urls` missing or empty in `/sdcard/portal/config.json`, the installer app uses the default catalog URL and loads successfully.
- With `app_catalog_urls` set to multiple URLs, the installer app loads and merges them, logs a warning for duplicate app ids, and uses the first entry.
- The installer list does not show apps already installed (based on `/sdcard/portal/apps.json`).
- The installer list does not show apps whose `manifest.platforms` excludes `m5paper_s3`.
- Tapping an app shows a detail view with all available fields and an Install button.
- Installing downloads the `.papp` with visible progress and results in a new `/sdcard/portal/apps/<id>/` directory and an updated `/sdcard/portal/apps.json`.
- After returning to the launcher, the newly installed app appears in the grid and can be opened.

## Idempotence and Recovery

- Re-running catalog load is safe; duplicates are handled deterministically (first wins).
- If a download is interrupted, the installer must clean up its temporary `.download-<id>.papp` file on the next attempt, or overwrite it safely.
- Installation should continue using the existing staging/backup recovery logic (see `recoverIncompleteInstalls` in the installer code) so power loss during install does not brick the app catalog.

## Artifacts and Notes

Example `/sdcard/portal/config.json` (minimal):

    {
      "wifi": { "ssid": "MyWiFi", "password": "secret" },
      "app_catalog_urls": ["https://paperportal.org/catalog.json"]
    }

Example remote catalog JSON (`schema_version: 1`):

    {
      "schema_version": 1,
      "apps": [
        {
          "id": "3fa85f64-5717-4562-b3fc-2c963f66afa6",
          "package_url": "https://example.com/notes-1.2.3.papp",
          "manifest": {
            "manifest_version": 1,
            "sdk_version": 1,
            "id": "3fa85f64-5717-4562-b3fc-2c963f66afa6",
            "name": "Notes",
            "version": "1.2.3",
            "checksum": "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
            "platforms": ["m5paper_s3"],
            "description": "A tiny notes app."
          }
        }
      ]
    }

## Interfaces and Dependencies

New/changed interfaces introduced by this plan:

- App manifest (`manifest.json` inside `.papp`):
  - New optional field `platforms: string[]`.
  - Allowed values (for now): only `"m5paper_s3"`.
- Portal SD configuration (`/sdcard/portal/config.json`):
  - New optional field `app_catalog_urls: string[]` (URLs).
  - Behavior: default URL is used when missing/empty; otherwise use provided URLs in order.
- New built-in app id:
  - `app-installer` (host must allow switching to it and load embedded WASM from `main/assets/app_installer.wasm`).
- New WASM host API additions (module `"portal_http"`):
  - `http_stream_open`, `http_stream_read`, `http_stream_close`, plus status/length query functions.
