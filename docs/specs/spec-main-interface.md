# Main launcher interface specification

This file specifies how the launcher main interface works.

**Goal**: This specification should be clear and provide an accurate description of the interface so that implementation can be done simply by following the spec, without requiring additional clarification or decision-making during development.

## Performance
- Displaying of launcher interface must be as fast as possible.

## Startup
- Header of the launcher with paper portal logo shown immediately after launch to be very fast (see launcher interface section). After header is visible start setting up the environment and loading apps.
- Dev server autostart must not block launcher startup: start launcher/event loop first, then enqueue dev server startup in background.
- Maybe optimize startup further by saving image of the full launch page with apps and display that while loading -- must test how it works in practice.
- Setup environment, SD, display, touch -- show clear error message if there are problems.
- App packages (`.papp`) can be found from SD card folder `/portal/apps` (WASM-visible; maps to `/sdcard/portal/apps`).
- Find all modified app packages from there.
- Install/unpack modified app packages to `/portal/apps/<id>/` (where `<id>` comes from `manifest.json.id`).
- Load installed apps from `/portal/apps/<id>/` subfolders.

## App catalog file
- Keep a catalog of all installed apps in /portal/apps.json so it can be loaded quickly.
- Format (schema_version 1):
  - JSON object with:
    - `schema_version`: integer (`1`)
    - `apps`: array of objects, each:
      - `id`: lower-case UUID string (RFC 4122 canonical form)
      - `manifest`: JSON object (the app’s `manifest.json`, preserved as-is for forward compatibility)
      - `pinned_publisher_pubkey` (optional): Base64 string of the pinned Ed25519 publisher public key (raw 32 bytes) for TOFU pinning
- The Settings app is not stored in this file; it is injected by the launcher UI as the first app tile.
- If an app package is removed then automatically update the catalog by removing it from catalog.
- If an app package is updated then automatically update new information to the catalog.

## Launcher interface

### Header
- Position: Top of screen
- Content: Image from `src/assets/main-header.png`
- Dimensions: Full screen width, height determined by image file
- The image file is embedded in the application binary
- View background: White

### Settings
- Settings is an app (not a button in the header).
- The Settings app is installed by default and is always the first app in the app grid.
- Implementation: `src/assets/paperportal-settings.wasm`

### App grid

#### Layout
- Screen size: 960×540px (M5Paper S3)
- Fixed 3 columns
- As many rows as can fit on screen below header
- Side margins: 16px
- Bottom margin: 16px
- Gap between columns: 24px
- Gap between rows: 24px
- Each column: (960 - 32) / 3 = 269px width
- Icon centered within column

#### App item
- Icon: 128×128px
- Title displayed below icon
- Gap between icon and title: 8px
- Title font: Inter-Medium-32 VLW, scaled to 0.6x, black color
- Icon and title centered within column

#### Mock data (initial implementation)
- 6 mock apps:
  - Settings (first app in the grid)
  - "App 1", "App 2", "App 3", "App 4", "App 5"
- Mock icons: Black 128×128px rectangles

#### Pagination
- Buttons at bottom to navigate to next/previous page if apps don't fit on one page
- TODO: Specify pagination button appearance and behavior

# Implementation plan
1. Implement fast display of launcher header
2. Display mock apps in launcher.
3. Implement Settings app launching (see `src/assets/paperportal-settings.wasm` and spec-settings.md)
4. TODO…
