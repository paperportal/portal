# WebAssembly app packaging specification

This document specifies the on-disk packaging format for Paper Portal WebAssembly apps.

## Scope and terminology

- **Runner**: the native firmware that installs, catalogs, validates, and executes apps (the WASM runtime host).
- **Package**: a `.papp` file (ZIP container) containing `manifest.json`, `app.wasm`, and optional assets.
- **Install**: extracting a validated package to an app directory on the SD card and updating the app catalog.
- **App root**: the extracted directory for an installed app, at `/portal/apps/<id>/` (WASM-visible) or
  `/sdcard/portal/apps/<id>/` (native filesystem).

## File extension

App packages **must** use the file extension `.papp`.

## Container format

A `.papp` file is a ZIP archive with the following constraints (to keep parsing simple on embedded targets):

- No encryption.
- No ZIP64.
- Compression methods:
  - **store** (no compression)
  - **deflate** (recommended for `app.wasm`; `icon.png` is usually already compressed)

## ZIP entry rules

To avoid path traversal and simplify implementation:

- ZIP entry names must use `/` as the path separator.
- Entry names must be relative (no leading `/`).
- Entry names must not contain `..` segments.
- Entry names must not start with `./`.
- Runner must ignore (and should not extract) any ZIP entries not listed in “ZIP contents”.

## ZIP contents

Required:

- `manifest.json` (UTF-8 JSON; see schema below)
- `app.wasm` (the WebAssembly module)

Optional:

- `icon.png` (launcher icon; optional)
- `assets/**` (additional app files, nested paths allowed under the `assets/` directory)

Additional files may be present and must be ignored.

## Extracted on-disk layout

When a package is installed, Runner extracts it to:

- Native filesystem: `/sdcard/portal/apps/<id>/`
- WASM-visible paths: `/portal/apps/<id>/` (because the WASM FS maps `/...` to `/sdcard/...`)

Runner must use `manifest.json.id` as the directory name. `manifest.json.name` is display-only.

## Icon requirements

- Must be a square PNG with alpha.
- Recommended size: **128×128 px**. Other sizes are accepted and will be resized as needed.
- Should have rounded corners. Recommended corner radius: **24 px at 128 px** (scale proportionally with size).

If `icon.png` is missing, Paper Portal Runner must display a placeholder icon.

## `manifest.json` schema

`manifest.json` is a JSON object with these fields:

- `manifest_version` (int, required): Manifest schema version.
  - Currently supported value: `1`
- `sdk_version` (int, required): Paper Portal SDK/host-contract compatibility version.
  - Currently supported value: `1`
  - Runner must reject packages with unsupported `sdk_version`.
- `id` (string, required): Stable unique identifier used for install/update and paths.
  - Format: GUID/UUID canonical string (RFC 4122), lower-case
  - Must match: `^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$`
- `name` (string, required): Short app name displayed under the icon in the launcher.
- `checksum` (string, required): Checksum of `app.wasm`.
  - Algorithm: **SHA-256**
  - Input bytes: exact (uncompressed) `app.wasm` file contents
- Encoding: lower-case hex
  - Format: `sha256:<64-hex-chars>`
- `version` (string, required): App version string.
  - Format: `#.#.#` (exactly three dot-separated numbers; no prefixes/suffixes)
  - Must match: `^[0-9]+\\.[0-9]+\\.[0-9]+$`
- `description` (string, optional): Longer human-readable description (may be shown in app details UI).
- `author` (string, optional): App author / publisher name.
- `home_page` (string, optional): App homepage URL.
- `copyright` (string, optional): Copyright notice.
- `publisher_pubkey` (string, optional): Ed25519 public key (raw 32 bytes, Base64-encoded).
  - When present, `signature` must also be present.
- `signature` (string, optional): Ed25519 signature (raw 64 bytes, Base64-encoded).
  - When present, `publisher_pubkey` must also be present.

Unknown fields must be ignored to allow forward-compatible extensions.

## Validation rules (normative)

Runner must reject install/update if any of these checks fail:

- ZIP contents must include `manifest.json` and `app.wasm`.
- `manifest.json` must parse as UTF-8 JSON object.
- `manifest_version` must be `1`.
- `sdk_version` must be `1`.
- `id` must be valid per the schema constraints above.
- `version` must be valid per the schema constraints above.
- If `signature` is present:
  - `publisher_pubkey` must be present.
  - `publisher_pubkey` must decode to exactly 32 bytes.
  - `signature` must decode to exactly 64 bytes.

Runner must compute SHA-256 over the exact (uncompressed) `app.wasm` bytes and compare to
`manifest.json.checksum`. On mismatch, reject install/update.

Runner should also validate the module’s exported host contract at load time (e.g. via `portalContractVersion()`).

### Example

```json
{
  "manifest_version": 1,
  "sdk_version": 1,
  "id": "3fa85f64-5717-4562-b3fc-2c963f66afa6",
  "name": "Notes",
  "checksum": "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
}
```

## Integrity and authenticity (optional signing)

The checksum provides integrity for `app.wasm` (detects corruption/tampering) but does not provide publisher
authenticity. Authenticity can be layered on using an optional Ed25519 signature with TOFU (trust-on-first-use)
pinning by app `id`.

### Signing payload (normative)

If `publisher_pubkey` and `signature` are present, Runner must verify Ed25519 over this message:

- Message bytes: UTF-8 of:

  `paperportal.papp.v1\n` + `<id>` + `\n` + `<checksum>` + `\n`

Where:
- `<id>` is exactly `manifest.json.id`
- `<checksum>` is exactly `manifest.json.checksum` (e.g. `sha256:...`)

### TOFU pinning rules (normative)

Runner maintains a pinned publisher key per app `id` in the app catalog (`/portal/apps.json`):

1. If there is no pinned key for `id`:
   - If the package is signed and verifies: accept and pin `publisher_pubkey` for future updates.
   - If the package is unsigned: accept and keep `id` unpinned.
2. If there is a pinned key for `id`:
   - The package must be signed.
   - `publisher_pubkey` must match the pinned key exactly.
   - The signature must verify.
   - Otherwise reject the update as unauthorized.

Signature v1 covers `app.wasm` only (assets are not authenticated).

## Suggested future fields (non-normative)

These fields are not required by this spec today, but are commonly useful:

- `runner_min_version` (string/int): Minimum compatible Runner version.
- `sdk_min_version` (string/int): Minimum compatible SDK/contract version.
- `entrypoint` (string): Module entrypoint selector if multiple modules are ever supported.
