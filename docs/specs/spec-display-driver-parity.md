# Display API driver parity (LGFX vs FastEPD)

Paper Portal exposes a single WASM display API surface (`/Users/mika/code/paperportal/portal/main/wasm/api/display.h`) with two backend implementations:

- **LGFX**: `DisplayLgfx` (`/Users/mika/code/paperportal/portal/main/wasm/api/display_lgfx.cpp`)
- **FastEPD**: `DisplayFastEpd` (`/Users/mika/code/paperportal/portal/main/wasm/api/display_fastepd.cpp`)

This document lists every `Display` virtual function and summarizes behavioral parity between the two drivers.

## Parity categories

- **A) Full parity**: same intent + broadly equivalent behavior and return value semantics across both drivers.
- **B) Partial parity**: implemented by both, but differs in defaults, validation/clipping behavior, update quality/perf, or other observable semantics.
- **C) Different**: missing/unimplemented in one driver, or materially different behavior/return value semantics.

Notes:
- FastEPD has several functions that log `[unimplemented] ... called` but still return `kWasmOk` (success). Those are categorized as **Different** (or **Partial** when the no-op is effectively equivalent).

## A) Full parity

- `driver()`: Returns the active `PaperDisplayDriver`.
- `clear()`: Clears the framebuffer to “white” (driver’s notion of a blank screen).
- `setCursor(x, y)`: Sets the text cursor position.
- `textWidth(s)`: Returns the pixel width of `s` for the currently selected font settings.
- `fontHeight()`: Returns the current font height in pixels.

## B) Partial parity

### Lifecycle, geometry, rotation

- `release()`: Both attempt to free driver resources, but LGFX currently has a `// TODO panel->deinit()` and may not fully deinitialize hardware resources the same way FastEPD does.
- `width()`, `height()`: Both report current logical dimensions, but the **default rotation differs** (`FastEPD` initializes to 90°; LGFX touch/display baseline is rotation 0), so initial `width/height` can be swapped between drivers until `setRotation()` is called.
- `getRotation()`, `setRotation(rot)`: Both use `rot` in `0..3`, but FastEPD maps from rotation degrees and also applies an offset when configuring touch rotation (to keep LGFX touch coordinates aligned with FastEPD’s baseline).

### Update / write batching

- `fillScreen(rgb888)`: Both accept RGB888 input, but FastEPD immediately **quantizes to the current EPD mode** (1bpp or 4bpp) while LGFX writes to an 8-bit grayscale framebuffer and defers panel quantization to the panel/update path.
- `display()`: Both trigger a full-screen refresh, but FastEPD uses a `fullUpdate(CLEAR_SLOW, ...)` path (slow clear waveform) while LGFX delegates to `display->display()` (update behavior depends on LGFX/epd mode).
- `displayRect(x, y, w, h)`: Both update a rectangle, but FastEPD uses `fullUpdate(CLEAR_NONE, ... , &rect)` while LGFX uses `display->display(x, y, w, h)` (different waveform/ghosting tradeoffs).
- `fullUpdateSlow()`: LGFX uses the base default (`fullUpdateSlow() == display()`), while FastEPD explicitly runs a full update with a slow clear waveform.
- `waitDisplay()`: LGFX blocks until the async display task finishes; FastEPD updates are currently synchronous so `waitDisplay()` is effectively a no-op (and logs `[unimplemented]`).
- `startWrite()`, `endWrite()`: LGFX wraps batched drawing with start/end write calls; FastEPD currently treats these as no-ops (and logs `[unimplemented]`), so batching semantics differ.

### Brightness

- `setBrightness(v)`, `getBrightness()`: Both expose a 0..255 value, but LGFX requires the display to be initialized and returns the underlying driver’s value; FastEPD stores the value in a global and applies it to the FastEPD backend (without requiring display readiness).

### Text (partially supported)

- `setTextWrap(wrap_x, wrap_y)`: LGFX supports X and Y wrap independently; FastEPD collapses both into a single boolean (`wrap_x || wrap_y`), so wrap behavior can differ.
- `setTextColor(fg_rgb888, bg_rgb888, use_bg)`: Both support foreground and optional background, but FastEPD converts RGB888 to grayscale and then quantizes per EPD mode (1bpp/4bpp), while LGFX uses its color conversion + panel pipeline.

### Images (supported subset / semantic differences)

- `pushImageGray8(x, y, w, h, ptr, len)`: Both draw an 8-bit grayscale image, but LGFX requires the rect to be fully in-bounds and `len == w*h`; FastEPD allows `len >= w*h` and clips pixels that fall outside the physical display.
- `drawPng(ptr, len, x, y)`: Both decode and draw PNG, but FastEPD uses its own decode+dither pipeline (including alpha handling blended against white), while LGFX uses its built-in decoder and conversion pipeline (rendering/quantization can differ).
- `drawJpgFit(ptr, len, x, y, max_w, max_h)`, `drawJpgFile(path, x, y, max_w, max_h)`: Both “fit” decode and draw JPEGs, but scaling/quality tradeoffs differ (FastEPD uses power-of-two scale options and a dithered grayscale path; LGFX uses its own decoder).

### Primitives

These all draw the same shape primitives, but differ in **argument validation and clipping**:

- `drawPixel(x, y, rgb888)`: FastEPD explicitly errors on out-of-bounds coordinates; LGFX forwards to the backend (which may clip silently).
- `drawFastVline(x, y, h, rgb888)`, `drawFastHline(x, y, w, rgb888)`: LGFX returns `kWasmErrInvalidArgument` for negative `h/w`; FastEPD treats `h <= 0` / `w <= 0` as a no-op success.
- `drawCircle(x, y, r, rgb888)`, `fillCircle(x, y, r, rgb888)`: LGFX validates `r >= 0`; FastEPD does not explicitly validate `r`.
- `drawRoundRect(x, y, w, h, r, rgb888)`, `fillRoundRect(x, y, w, h, r, rgb888)`: LGFX validates `r >= 0`; FastEPD does not explicitly validate `r`.

All remaining primitive functions are otherwise broadly equivalent (subject to the validation differences above):

- `drawLine(x0, y0, x1, y1, rgb888)`
- `drawRect(x, y, w, h, rgb888)`
- `fillRect(x, y, w, h, rgb888)`
- `fillArc(x, y, r0, r1, angle0, angle1, rgb888)`
- `drawEllipse(x, y, rx, ry, rgb888)`
- `fillEllipse(x, y, rx, ry, rgb888)`
- `drawTriangle(x0, y0, x1, y1, x2, y2, rgb888)`
- `fillTriangle(x0, y0, x1, y1, x2, y2, rgb888)`

## C) Different

### Lifecycle / mode selection

- `init()`: Both initialize the display, but FastEPD performs FastEPD-specific panel init, sets mode to 4bpp, sets rotation to 90°, performs an initial clear/full update, and sets up a backup plane; LGFX uses `paper_display().init()` and does not mirror FastEPD’s defaults.
- `setDisplayMode(mode)`: Selects framebuffer grayscale precision (0..3 => 1/2/4/8bpp). LGFX supports all modes (maps to `setColorDepth(grayscale_*)`); FastEPD supports 0..2 (maps to `setMode(BB_MODE_*BPP)`) and returns `kWasmErrInvalidArgument` for mode 3 (8bpp). Both implementations cache the last successfully applied mode and treat setting the same mode twice as a fast no-op (without forcing initialization). This does not clear the backbuffer and does not trigger a display refresh; callers should redraw after switching modes.
- `setEpdMode(mode)`: Supported by LGFX (`mode` validated `1..4`); FastEPD logs `[unimplemented]` and does not change mode.
- `getEpdMode()`: LGFX returns its `epd_mode_t` (typically `1..4`); FastEPD maps internal mode to only `{1 (1bpp), 2 (4bpp)}`.

### Text (missing features and mismatched semantics)

- `setTextSize(sx, sy)`: Implemented by LGFX; FastEPD logs `[unimplemented]` and does not scale text.
- `setTextDatum(datum)`: Implemented by LGFX (validated subset); FastEPD logs `[unimplemented]` and does not support datum alignment.
- `setTextScroll(scroll)`: Implemented by LGFX; FastEPD logs `[unimplemented]`.
- `setTextFont(font_id)`: Both accept `font_id`, but the **font mapping differs** (LGFX maps ids to LGFX fonts; FastEPD forwards ids to FastEPD’s font selection), so the same `font_id` is not guaranteed to render the same font/metrics.
- `setTextEncoding(utf8_enable, cp437_enable)`: Implemented by LGFX; FastEPD logs `[unimplemented]`.
- `drawString(s, x, y)`: **Return value semantics differ**: LGFX returns the drawn string width; FastEPD returns `kWasmOk` and also applies a y-offset adjustment based on `getStringBox()`, so positioning/measurements can differ.

### VLW font APIs

- `vlwRegister(ptr, len)`: LGFX copies the bytes and returns a handle; FastEPD logs `[unimplemented]`.
- `vlwUse(handle)`: LGFX loads the registered font blob; FastEPD logs `[unimplemented]`.
- `vlwUseSystem(font_id, font_size)`: LGFX supports Inter + Montserrat system VLW fonts and **ignores** `font_size`; FastEPD supports only Inter and picks a closest-size **BBF** font based on `font_size`.
- `vlwUnload()`: LGFX unloads current font; FastEPD logs `[unimplemented]`.
- `vlwClearAll()`: LGFX frees all registered fonts; FastEPD logs `[unimplemented]`.

### Image APIs

- `pushImageRgb565(...)`: Implemented by LGFX (strict alignment + exact length); FastEPD logs `[unimplemented]`.
- `pushImage(...)`: Implemented by LGFX (multiple depths + palettes); FastEPD logs `[unimplemented]`.
- `readRectRgb565(...)`: Implemented by LGFX; FastEPD returns `kWasmErrInternal` (“not supported by FastEPD”).
- `drawXth(ptr, len)`, `drawXtg(ptr, len)`: LGFX draws into the framebuffer only; FastEPD draws **and immediately triggers a full update** (`fullUpdate(CLEAR_SLOW, ...)`), which changes expected “draw vs refresh” control flow.
- `drawPngFit(ptr, len, x, y, max_w, max_h)`, `drawPngFile(path, x, y, max_w, max_h)`: LGFX uses its PNG “fit” path (expected to scale/fit); FastEPD’s “fit” logic is **crop/clip only** (limits drawn width/height to `max_w/max_h` and available space, without scaling).
