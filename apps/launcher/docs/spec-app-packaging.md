# WebAssembly app packaging specification

This document specifies the on-disk packaging format for Paper Portal WebAssembly apps.

## File extension

App packages **must** use the file extension `.ppapp`.

## Container format

A `.ppapp` file is a ZIP archive with the following constraints (to keep parsing simple on embedded targets):

- Only files at the ZIP root (no directories).
- No encryption.
- No ZIP64.
- Compression methods:
  - **store** (no compression)
  - **deflate** (recommended for `app.wasm`; `icon.png` is usually already compressed)

## ZIP contents

Required:

- `manifest.json` (UTF-8 JSON; see schema below)
- `app.wasm` (the WebAssembly module)

Optional:

- `icon.png` (launcher icon; required when `manifest.json` does not contain `icon`)

Additional files may be present in the ZIP root and must be ignored.

## Icon requirements

- Must be a square PNG with alpha.
- Recommended size: **128×128 px**. Other sizes are accepted and will be resized as needed.
- Should have rounded corners. Recommended corner radius: **24 px at 128 px** (scale proportionally with size).

If both `manifest.json.icon` and `icon.png` are missing, Paper Portal Runner must display a placeholder icon.

## `manifest.json` schema

`manifest.json` is a JSON object with these fields:

- `sdk_version` (int, required): Paper Portal SDK compatibility version.
  - Currently supported value: `1`
  - This value is incremented when there is an incompatible SDK update.
- `name` (string, required): Short app name displayed under the icon in the launcher.
- `checksum` (string, required): Checksum of `app.wasm`.
  - Algorithm: **SHA-256**
  - Input bytes: exact (uncompressed) `app.wasm` file contents
  - Encoding: lower-case hex
  - Format: `sha256:<64-hex-chars>`
- `icon` (string, optional): Base64-encoded PNG file bytes.
  - Encoding: Base64 (RFC 4648, standard alphabet, no newlines)
  - When present, this overrides `icon.png`.
  - When absent, `icon.png` should be present (otherwise a placeholder is shown).
- `version` (string, optional): Human-readable app version (e.g. `1.2.3`).
- `description` (string, optional): Longer human-readable description (may be shown in app details UI).
- `author` (string, optional): App author / publisher name.
- `home_page` (string, optional): App homepage URL.
- `copyright` (string, optional): Copyright notice.

Unknown fields must be ignored to allow forward-compatible extensions.

### Example

```json
{
  "sdk_version": 1,
  "name": "Notes",
  "checksum": "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "icon": "iVBORw0KGgoAAAANSUhEUgAAAI..."
}
```

## Suggested future fields (non-normative)

These fields are not required by this spec today, but are commonly useful:

- `manifest_version` (int): Explicit schema version (e.g. `1`) for forward-compatible evolution.
- `id` (string): Stable unique identifier (used for updates and per-app settings).
- `version` (string): Human-readable version (e.g. `1.2.3`).
- `runner_min_version` (string/int): Minimum compatible Runner version.
- `sdk_min_version` (string/int): Minimum compatible SDK/contract version.
- `signature` (string): Optional package or `app.wasm` signature (e.g. Ed25519) for authenticity, beyond checksum integrity.
