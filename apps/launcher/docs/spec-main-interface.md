# Main launcher interface specification

This file specifies how the launcher main interface works.

**Goal**: This specification should be clear and provide an accurate description of the interface so that implementation can be done simply by following the spec, without requiring additional clarification or decision-making during development.

## Performance
- Displaying of launcher interface must be as fast as possible.

## Startup
- Header of the launcher with paper portal logo shown immediately after launch to be very fast (see launcher interface section). After header is visible start setting up the environment and loading apps.
- Maybe optimize startup further by saving image of the full launch page with apps and display that while loading -- must test how it works in practice.
- Setup environment, SD, display, touch -- show clear error message if there are problems.
- App packages can be found from SD card folder /pp/apps.
- Find all modified app packages from there.
- Uncompress modified app packages to /pp/apps/<app-name-or-identifier>
- Load uncompressed apps from subfolders

## App catalog file
- Keep a catalog of all installed apps in /pp/apps.json so it can be loaded quickly.
- TODO: specify format of the file
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
