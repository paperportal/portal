# Implement Full VLW Font Support on FastEPD With Minimal Modularization

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and `Outcomes & Retrospective` must be kept up to date as work proceeds.

This repository does not currently include a `PLANS.md` file. Follow the `exec-plans` skill requirements directly when updating this document.

## Purpose / Big Picture

After this change, Paper Portal apps must use the `display.vlw.*` workflow for proportional font rendering on the `fastepd` display driver, just as they already do on `lgfx`. The old BBF-based approximation path is no longer a compatibility target. In practical terms, a Zig/WASM app will be able to register a VLW font from bytes, switch between registered and system VLW fonts, unload fonts, clear registered font state, and draw or measure text with the same visible behavior on both drivers.

The observable success case is simple. Running the launcher or settings app with `display.driver = "fastepd"` will render through real VLW glyphs instead of the current “closest BBF size” approximation, and the public `vlwRegister`, `vlwUse`, `vlwUnload`, and `vlwClearAll` lifecycle will work on FastEPD exactly as it does on LGFX from the WASM app’s point of view.

## Progress

- [x] (2026-03-11 15:20Z) Survey the existing font paths in `main/wasm/api/display_lgfx.cpp`, `main/wasm/api/display_fastepd.cpp`, `components/LovyanGFX`, and `components/FastEPD`, and confirm that FastEPD currently implements only a partial `vlwUseSystem` shim backed by BBF fonts.
- [x] (2026-03-11 15:20Z) Confirm the VLW file format reference already exists at `docs/specs/VLW_Font_Format_Specification.md` and can be used as the parser contract for the FastEPD implementation.
- [x] (2026-03-11 15:20Z) Author this ExecPlan at `docs/plans/execplan-fastepd-vlw-parity.md`.
- [x] (2026-03-11 16:10Z) Implement a small, reusable VLW parser/index module for firmware use without changing LGFX internals.
- [x] (2026-03-11 16:22Z) Implement a FastEPD VLW text renderer and text-state bridge so `drawString`, `textWidth`, and `fontHeight` behave like the LGFX path when a VLW font is active.
- [x] (2026-03-11 16:27Z) Replace the current BBF-backed `vlwUseSystem` approximation in FastEPD with real VLW system font loading for the existing Inter and Montserrat assets, and remove the old compatibility fallback from the VLW path.
- [x] (2026-03-11 16:33Z) Implement FastEPD support for `vlwRegister`, `vlwUse`, `vlwUnload`, and `vlwClearAll`, including ownership and cleanup on app unload.
- [x] (2026-03-11 16:49Z) Add validation coverage: parity doc updates, `zig build` for `apps/launcher`, and a successful firmware build with `python3 $IDF_PATH/tools/idf.py build`.

## Surprises & Discoveries

- Observation: FastEPD already exposes the `portal_display` VLW entry points, but only `vlwUseSystem` does anything, and it does not load VLW at all. It selects a nearest-size BBF blob and leaves `vlwRegister`, `vlwUse`, `vlwUnload`, and `vlwClearAll` as no-op success paths.
  Evidence: `main/wasm/api/display_fastepd.cpp` around `DisplayFastEpd::vlwRegister` through `DisplayFastEpd::vlwClearAll`.

- Observation: LGFX already implements the complete public VLW lifecycle, including host-owned copies of registered font blobs and explicit unload/clear logic.
  Evidence: `main/wasm/api/display_lgfx.cpp` around `DisplayLgfx::vlwRegister`, `DisplayLgfx::vlwUse`, `DisplayLgfx::vlwUnload`, and `DisplayLgfx::vlwClearAll`.

- Observation: The shipped VLW assets are larger than BBF but already exist in firmware assets, so the work does not require inventing a new asset pipeline for system VLW fonts.
  Evidence: `main/CMakeLists.txt` embeds `assets/inter_medium_32.vlw` and `assets/montserrat_light_20.vlw`; `ls -l` shows `inter_medium_32.vlw` is about 39 KB while `inter_medium_32.bbf` is about 11.5 KB.

- Observation: FastEPD custom fonts are already proportional and already support measurement through `getStringBox`, but they are tied to the BBF binary format and CP1252 conversion path.
  Evidence: `components/FastEPD/src/FastEPD.cpp` and `components/FastEPD/src/bb_ep_gfx.inl`.

- Observation: FastEPD’s direct framebuffer writes use the `pfnSetPixelFast` layout, and the nibble packing for rotated 2bpp/4bpp modes must be mirrored exactly when blending VLW alpha over existing pixels.
  Evidence: `components/FastEPD/src/bb_ep_gfx.inl` `bbepSetPixelFast4Clr*` and `bbepSetPixelFast16Clr*`.

## Decision Log

- Decision: Scope “full VLW support” to the public `portal_display` behavior that apps can observe on LGFX, not to a literal port of LovyanGFX internals.
  Rationale: The goal is driver parity at the WASM API boundary. Reproducing private LGFX implementation details would add complexity without improving app-visible behavior.
  Date/Author: 2026-03-11 / Codex

- Decision: Keep LGFX unchanged and add a small FastEPD-specific VLW implementation instead of trying to extract a shared cross-driver text engine.
  Rationale: LGFX already works. The lowest-risk path is to add the missing FastEPD behavior behind the existing `DisplayFastEpd` interface rather than refactoring both drivers at once.
  Date/Author: 2026-03-11 / Codex

- Decision: Split the implementation into exactly three new responsibilities: an immutable VLW parser/index, a FastEPD VLW renderer, and a thin registration/selection bridge owned by FastEPD.
  Rationale: This is the smallest modular split that keeps parsing, rendering, and lifecycle management separate without creating a generic abstraction layer that the repository does not need.
  Date/Author: 2026-03-11 / Codex

- Decision: Backward compatibility with the old BBF-backed VLW approximation is not required. Apps should migrate to real VLW fonts, and the FastEPD VLW path should stop depending on BBF assets or BBF selection logic.
  Rationale: The user explicitly wants a unified font system. Keeping the old compatibility layer would add code paths and testing surface that work against that goal.
  Date/Author: 2026-03-11 / Codex

- Decision: Make FastEPD `vlwUseSystem` load the same VLW assets that LGFX uses and ignore `font_size`, matching current LGFX semantics exactly.
  Rationale: The current API already accepts `font_size`, but LGFX ignores it. Preserving that behavior is necessary for parity and avoids another asset explosion.
  Date/Author: 2026-03-11 / Codex

## Outcomes & Retrospective

- (2026-03-11 16:49Z) Completed the FastEPD VLW parity work described here. `main/fonts/` now contains the parser, registry, and renderer modules; `DisplayFastEpd` owns the public VLW lifecycle and text state; and app-runtime cleanup clears registered VLW state on unload.
- (2026-03-11 16:49Z) Validation reached the intended proof level for this repository: `zig build` succeeded for `apps/launcher`, and the firmware build succeeded via `python3 $IDF_PATH/tools/idf.py build` after sourcing the local ESP-IDF environment.

## Context and Orientation

Paper Portal exposes one display API to WASM apps through the virtual `Display` interface in `main/wasm/api/display.h`. Two backends implement that interface:

- `DisplayLgfx` in `main/wasm/api/display_lgfx.cpp`
- `DisplayFastEpd` in `main/wasm/api/display_fastepd.cpp`

The Zig SDK already treats VLW as part of the public display contract. Apps call:

- `display.vlw.register(font_bytes)`
- `display.vlw.use(handle)`
- `display.vlw.useSystem(font, size)`
- `display.vlw.unload()`
- `display.vlw.clearAll()`

and then use the ordinary text APIs:

- `display.text.draw(...)`
- `display.text.textWidth(...)`
- `display.text.fontHeight()`
- `display.text.setSize(...)`
- `display.text.setDatum(...)`
- `display.text.setEncodingUtf8()` / `setEncodingCp437()`

Today, that contract is fully implemented on LGFX and only partially simulated on FastEPD. This plan fixes FastEPD so the public behavior matches LGFX when a VLW font is active.

The term “VLW” in this repository means the Processing-style bitmap font format documented in `docs/specs/VLW_Font_Format_Specification.md`. A VLW file contains a 24-byte header, a 28-byte record for each glyph, and then raw 8-bit grayscale bitmap data for every glyph. LGFX parses these files directly. FastEPD does not.

The term “BBF” means FastEPD’s native compressed proportional font format. BBF is smaller and already renders efficiently on FastEPD, but it is not the public font format the Zig SDK exposes. The old FastEPD VLW path was not real VLW support; it was an approximation layered on top of BBF.

### Relevant existing files

- `main/wasm/api/display.h`: defines the public virtual methods that both display drivers must implement.
- `main/wasm/api/display_lgfx.cpp`: the reference implementation for public VLW behavior.
- `main/wasm/api/display_fastepd.cpp`: the backend that needs the new VLW implementation.
- `components/LovyanGFX/src/lgfx/v1/lgfx_fonts.cpp`: reference parser and renderer behavior for VLW metrics and glyph loading.
- `components/FastEPD/src/FastEPD.cpp` and `components/FastEPD/src/bb_ep_gfx.inl`: current custom-font rendering behavior on FastEPD.
- `docs/specs/VLW_Font_Format_Specification.md`: repository-local reference for the VLW binary layout.
- `docs/specs/spec-display-driver-parity.md`: current parity document that should be updated after implementation.
- `main/CMakeLists.txt`: embeds the existing `inter_medium_32.vlw` and `montserrat_light_20.vlw` system font assets.

### Constraints that must shape the implementation

The implementation must be modularized, but it must not introduce a large new framework. The safe target is a small set of helper modules used only by FastEPD. Do not create a generic “font engine” abstraction for both drivers, and do not rewrite LGFX to consume the new code.

Backward compatibility with the previous BBF-based VLW approximation is not required. Apps that currently rely on the old FastEPD-specific font behavior must be updated to use real VLW fonts through `display.vlw.*`.

This plan does not require preserving previous BBF font-file compatibility for app-level proportional font rendering. The old BBF assets may remain in the tree temporarily if other non-VLW internals still need them during the migration, but the public VLW path must no longer depend on them.

The implementation must own registered font bytes on the host side, because the WAMR caller can free or overwrite its own memory after `vlwRegister()` returns. This is how LGFX already behaves, and FastEPD must do the same.

The implementation must clean up state when an app unloads or the current display driver is released. A stale registered font from one app must not survive into the next app.

## Plan of Work

### Milestone 1: Add a reusable VLW parser and registry with no rendering yet

The first milestone introduces an immutable representation of a VLW font and a small registry that mirrors the public LGFX lifecycle. At the end of this milestone, FastEPD can register and select VLW blobs safely, but text drawing may still be routed through the old path.

Create a new firmware-internal directory or use `main/wasm/api/` if that is materially simpler. The preferred layout is:

- `main/fonts/vlw_font.h`
- `main/fonts/vlw_font.cpp`
- `main/fonts/vlw_registry.h`
- `main/fonts/vlw_registry.cpp`

Do not place parsing code directly into `display_fastepd.cpp`. The file is already large, and the parser needs to be testable in isolation.

`vlw_font.*` must define a small immutable type, proposed as `VlwFont`, plus per-glyph metadata:

    struct VlwGlyph {
        uint16_t codepoint;
        uint16_t width;
        uint16_t height;
        uint16_t x_advance;
        int16_t y_delta;
        int8_t x_delta;
        uint32_t bitmap_offset;
    };

    struct VlwMetrics {
        uint16_t glyph_count;
        int16_t ascent;
        int16_t descent;
        int16_t max_ascent;
        int16_t max_descent;
        int16_t line_height;
        uint16_t space_width;
    };

    class VlwFont {
    public:
        static std::shared_ptr<VlwFont> CreateCopy(const uint8_t* ptr, size_t len, const char* debug_name);
        const VlwMetrics& metrics() const;
        const VlwGlyph* FindGlyph(uint16_t codepoint) const;
        const uint8_t* GlyphBitmap(const VlwGlyph& glyph) const;
        bool IsValid() const;
        const char* debug_name() const;
    };

The parser must validate the VLW header, confirm that every glyph’s bitmap stays within the provided byte range, compute `space_width`, `max_ascent`, `max_descent`, and `line_height` using the same meaning as the existing LGFX path, and reject malformed inputs with a clear error string.

The parser does not need to support every Unicode plane that the VLW file format can theoretically encode. It only needs to match what LGFX currently consumes in this repository, which is effectively 16-bit code points for lookup and metrics.

`vlw_registry.*` must manage handle-based ownership for FastEPD:

    class VlwRegistry {
    public:
        int32_t RegisterCopy(const uint8_t* ptr, size_t len, const char* debug_name, std::string* out_error);
        std::shared_ptr<VlwFont> Get(int32_t handle) const;
        bool Remove(int32_t handle);
        void Clear();
    };

This registry is for FastEPD only. It must not replace LGFX’s existing registry. The registry should use a monotonic positive `int32_t` handle counter and `std::unordered_map<int32_t, std::shared_ptr<VlwFont>>`.

At the same time, add a small helper for system VLW assets. This can live in `vlw_registry.cpp` or a tiny `vlw_system_fonts.cpp` if that keeps the code simpler. It must expose exactly the two currently shipped assets:

- `inter_medium_32.vlw`
- `montserrat_light_20.vlw`

This helper must return a `std::shared_ptr<VlwFont>` loaded from the binary asset bytes and cached once per process. Do not create one parser instance per `vlwUseSystem()` call.

Validation for Milestone 1 is build-only plus targeted unit-style assertions in the new module. If this repo does not have an existing firmware unit-test target for `main/`, add a small parser self-check function that can be called from a temporary debug path during development and removed or converted to permanent tests before the milestone is considered complete.

### Milestone 2: Add a FastEPD VLW renderer and text-state bridge

The second milestone makes FastEPD actually draw and measure VLW text. At the end of this milestone, when a VLW font is active, `drawString`, `textWidth`, and `fontHeight` on FastEPD behave like LGFX from the app’s point of view.

Create:

- `main/fonts/vlw_renderer_fastepd.h`
- `main/fonts/vlw_renderer_fastepd.cpp`

This module is responsible only for rendering and measurement against a `FASTEPD` instance plus a current text state. It must not own font handles and must not know about WAMR.

Define a small state struct that mirrors the public text attributes FastEPD needs for VLW:

    struct FastEpdVlwTextState {
        float size_x;
        float size_y;
        int32_t datum;
        bool wrap_x;
        bool wrap_y;
        bool utf8_enabled;
        bool cp437_enabled;
        bool scroll_enabled;
        int32_t fg_rgb888;
        int32_t bg_rgb888;
        bool use_bg;
    };

Keep this struct local to the renderer module or declared in its header. Do not invent a general display-wide state object for every text mode.

The renderer must provide:

    int32_t MeasureTextWidth(const VlwFont& font, const FastEpdVlwTextState& state, const char* text, int32_t* out_width);
    int32_t CurrentFontHeight(const VlwFont& font, const FastEpdVlwTextState& state, int32_t* out_height);
    int32_t DrawString(FASTEPD& epd, const VlwFont& font, const FastEpdVlwTextState& state, const char* text, int32_t x, int32_t y, int32_t* out_width);

Behavior rules:

1. Decode text using the same public switches the display API already exposes. When UTF-8 is enabled, decode UTF-8 into code points. When CP437 is enabled, map single-byte characters the same way LGFX does for its text path. If neither is enabled, treat input as plain bytes for the code-point range that VLW files in this repository actually include.

2. Use the VLW font’s baseline metrics so that `fontHeight()` and the vertical placement of `drawString()` match LGFX’s behavior closely enough that the built-in apps do not change layout between drivers.

3. Honor `setTextSize(sx, sy)` for VLW fonts by scaling glyph placement and glyph bitmaps. Because VLW glyphs are 8-bit grayscale bitmaps, use nearest-neighbor scaling first. This is sufficient for parity and much simpler than introducing a resampling pipeline. Record in the code comments that scaling intentionally matches the “bitmap font scaled by text size” model, not vector-font rasterization.

4. Honor `setTextDatum(datum)` for VLW fonts by measuring the string before drawing and then offsetting the origin the same way LGFX does for the supported datum values already accepted by `display_lgfx.cpp`.

5. Honor text color and optional background through FastEPD’s current grayscale quantization path. The renderer must translate VLW alpha into the current EPD mode without changing non-text rendering semantics. In 4bpp mode, preserve anti-aliased glyph edges; in 1bpp and 2bpp modes, quantize predictably.

6. The public proportional-font path on FastEPD must now be VLW-only. The renderer may leave FastEPD’s small built-in bitmap fonts in place for explicitly non-VLW text operations, but it must not route any `display.vlw.*` behavior through BBF compatibility logic.

Do not add a generic `Renderer` base class. `vlw_renderer_fastepd.*` is the only renderer this plan needs.

### Milestone 3: Wire the FastEPD display backend to the new modules and complete the public lifecycle

The third milestone connects the parser, registry, and renderer to the existing `DisplayFastEpd` implementation. At the end of this milestone, the public `portal_display` VLW API is complete on FastEPD.

Modify `main/wasm/api/display_fastepd.cpp` and, if needed, `main/wasm/api/display_fastepd.h` to add a small internal state bundle:

    struct FastEpdVlwRuntime {
        VlwRegistry registry;
        std::shared_ptr<VlwFont> active_font;
        bool active_font_is_system;
        FastEpdVlwTextState text_state;
    };

Store this runtime as a private file-static object in `display_fastepd.cpp`. This matches the existing global FastEPD style and avoids adding a new long-lived class hierarchy.

Then implement the missing public methods:

- `vlwRegister`: copy bytes into the registry, validate, and return a positive handle or a real error.
- `vlwUse`: resolve a registered handle, set `active_font`, and switch FastEPD text drawing into VLW mode.
- `vlwUseSystem`: load the real VLW asset for Inter or Montserrat, ignore `font_size`, set `active_font`, and stop using the old BBF selection helper for this API.
- `vlwUnload`: clear the active VLW selection but keep registered handles intact.
- `vlwClearAll`: unload the active VLW font and delete all registered handles.

Update these existing text methods so they dispatch to the VLW renderer when `active_font` is not null:

- `setTextSize`
- `setTextDatum`
- `setTextWrap`
- `setTextScroll`
- `setTextEncoding`
- `setTextColor`
- `drawString`
- `textWidth`
- `fontHeight`

For each method, preserve current FastEPD behavior only for explicitly non-VLW text operations. The old BBF-backed VLW compatibility path must be removed. This must be an explicit branch, not an implicit side effect.

The text positioning semantics must follow the current public contract, not FastEPD’s older BBF conventions. In particular:

- `drawString()` must return the drawn width like LGFX does.
- `textWidth()` must return the measured width for the active VLW string and state.
- `fontHeight()` must reflect the active VLW line height and current `size_y`.

Do not use `setTextFont()` as part of the VLW compatibility story. The preferred behavior is:

- `vlwUse*()` activates VLW mode.
- `setTextFont()` deactivates VLW mode only if the caller explicitly wants one of FastEPD’s small built-in bitmap fonts.

Apps that want proportional fonts must use VLW. This keeps the migration rule simple and removes ambiguity about which proportional font system is active.

Finally, add cleanup hooks so FastEPD VLW state cannot leak across app lifetimes. The exact cleanup points must include:

- display release path in `DisplayFastEpd::release`
- app unload or runtime teardown path that already clears per-app resources

If there is a single helper for app runtime cleanup, call a new `fastepd_vlw_reset_all()` helper there rather than duplicating cleanup logic.

### Milestone 4: Documentation, validation app, and parity proof

The final milestone makes the behavior demonstrable. At the end of this milestone, another engineer can build the firmware, run a small app, and verify that VLW behavior is real on FastEPD.

Update these documents:

- `docs/specs/spec-display-driver-parity.md`
- `docs/specs/VLW_Font_Format_Specification.md`

The parity spec must move FastEPD VLW entries from “Different” to “Full parity” or “Partial parity” with precise notes. Do not leave stale statements that describe the old BBF shim.

Add a dedicated validation app under `apps/`, proposed as:

- `apps/font-lab`

This app should:

1. Draw one line using `display.vlw.useSystem(display.vlw.SystemFont.inter, 12)`.
2. Draw one line using `display.vlw.useSystem(display.vlw.SystemFont.montserrat, 20)`.
3. Register a VLW byte blob embedded into the app with `@embedFile`, call `vlwRegister`, then `vlwUse`, then draw text and report width and font height on screen.
4. Call `vlwUnload` and show that built-in text drawing still works.
5. Call `vlwClearAll` and prove that using the old handle now fails with an error shown on screen or logged.

This app exists to prove public behavior, not to become product UI. Keep it visually simple and deterministic.

If embedding a VLW file directly into the WASM app makes the app unreasonably large, embed a small purpose-built VLW test asset instead of a full production font. The important thing is that the app exercises the dynamic registration API, not that it ships a large font.

## Concrete Steps

Run all commands from the repository root:

    cd /Users/mika/code/paperportal/portal

1. Implement the parser and registry modules, then verify the firmware still builds:

    idf.py build

   Expected outcome: the build completes successfully, and there are no unresolved references from the new `vlw_*` modules.

2. Implement the renderer and FastEPD wiring, then rebuild:

    idf.py build

   Expected outcome: the build still completes successfully, and there are no duplicate symbol or missing asset errors for the VLW system fonts.

3. Build the validation app and ensure its WASM artifact is embedded by the main firmware build. Use the project’s normal app build path if an app-specific build command already exists in the repo; otherwise rely on the regular firmware build that packages built-in apps.

4. Flash and monitor on hardware:

    idf.py -p /dev/tty.usbserial-XXXX flash monitor

   Replace `/dev/tty.usbserial-XXXX` with the actual device path.

5. In the device settings or config, force:

    { "display": { "driver": "fastepd" } }

   Then boot into the validation app and capture observations:

   - Inter and Montserrat VLW system fonts both render.
   - Text width and font height numbers match the expected layout and remain stable across redraws.
   - A dynamically registered VLW font renders successfully.
   - After `vlwUnload`, built-in text rendering still works.
   - After `vlwClearAll`, reusing the old handle fails with a real error instead of silent success.

6. Repeat the same validation with:

    { "display": { "driver": "lgfx" } }

   The layouts do not need to be pixel-identical, but they must be close enough that the same app logic produces the same visible structure and measurements within the documented tolerance below.

## Validation and Acceptance

Acceptance is based on app-visible behavior, not merely on successful parsing.

The implementation is complete only when all of the following are true:

1. `DisplayFastEpd::vlwRegister`, `vlwUse`, `vlwUseSystem`, `vlwUnload`, and `vlwClearAll` all return real success and failure states rather than no-op success placeholders.

2. `display.vlw.useSystem(display.vlw.SystemFont.inter, 12)` on FastEPD loads the real VLW system asset, not a BBF substitute.

3. `display.vlw.useSystem(display.vlw.SystemFont.montserrat, 20)` works on FastEPD, matching the existing LGFX system-font set.

4. When a VLW font is active on FastEPD, `drawString`, `textWidth`, and `fontHeight` produce the same semantic behavior as LGFX for the validation app. “Same semantic behavior” means the same datum anchor, line spacing, and width/height results within a tolerance of one pixel per glyph accumulation and two pixels on vertical placement after scaling.

5. `setTextSize`, `setTextDatum`, `setTextEncoding`, `setTextWrap`, and `setTextColor` all affect VLW rendering on FastEPD and no longer log `[unimplemented]` for the VLW path.

6. The public `display.vlw.*` path on FastEPD no longer depends on BBF compatibility. Apps that want proportional fonts must use VLW, and any old app assumptions about BBF-backed VLW behavior are considered intentionally broken by this change.

7. App unload, display release, and driver switching leave no stale VLW handles or active font pointers behind.

8. `docs/specs/spec-display-driver-parity.md` accurately reflects the new state.

## Idempotence and Recovery

The parser and registry changes are additive and safe to apply repeatedly. Rebuilding with `idf.py build` is idempotent.

If the renderer integration breaks text drawing during development, the safest recovery path is:

1. Keep the new parser and registry modules.
2. Temporarily branch `drawString`, `textWidth`, and `fontHeight` back to the old FastEPD path when no validated VLW font is active.
3. Re-run the validation app and only re-enable full VLW dispatch once width and baseline placement are correct.

If hardware validation fails after flashing, keep the firmware logging statements that identify:

- whether a system VLW asset or registered VLW handle was selected,
- how many glyphs were parsed,
- the resolved line height,
- the measured width for the string under test,
- whether cleanup ran on app unload.

Do not leave noisy per-glyph logs in the final version.

## Artifacts and Notes

Useful evidence snippets to capture while implementing:

    I (....) display_fastepd: vlwUseSystem loaded font 'inter_medium_32' glyphs=...
    I (....) display_fastepd: vlwUse handle=3 font='font_lab_test'
    I (....) display_fastepd: textWidth('Settings') => 54
    I (....) display_fastepd: vlwClearAll cleared 1 registered fonts

Expected firmware build proof:

    $ idf.py build
    ...
    Project build complete.

Expected validation proof on device:

    FastEPD VLW test
    system inter ok
    system montserrat ok
    registered handle=1 ok
    unload ok
    clearAll ok; old handle rejected

## Interfaces and Dependencies

This work depends on the existing VLW system assets already embedded by `main/CMakeLists.txt`, the public `Display` interface in `main/wasm/api/display.h`, the FastEPD framebuffer API from `components/FastEPD`, and the repository-local VLW format reference in `docs/specs/VLW_Font_Format_Specification.md`. It does not require compatibility with the previous BBF-backed VLW approximation.

At the end of the work, the following internal interfaces must exist:

In `main/fonts/vlw_font.h`:

    class VlwFont;
    struct VlwGlyph;
    struct VlwMetrics;

In `main/fonts/vlw_registry.h`:

    class VlwRegistry {
    public:
        int32_t RegisterCopy(const uint8_t* ptr, size_t len, const char* debug_name, std::string* out_error);
        std::shared_ptr<VlwFont> Get(int32_t handle) const;
        bool Remove(int32_t handle);
        void Clear();
    };

In `main/fonts/vlw_renderer_fastepd.h`:

    struct FastEpdVlwTextState;
    int32_t MeasureTextWidth(const VlwFont& font, const FastEpdVlwTextState& state, const char* text, int32_t* out_width);
    int32_t CurrentFontHeight(const VlwFont& font, const FastEpdVlwTextState& state, int32_t* out_height);
    int32_t DrawString(FASTEPD& epd, const VlwFont& font, const FastEpdVlwTextState& state, const char* text, int32_t x, int32_t y, int32_t* out_width);

In `main/wasm/api/display_fastepd.cpp`:

    static void fastepd_vlw_reset_all();

and the public methods:

    int32_t DisplayFastEpd::vlwRegister(...);
    int32_t DisplayFastEpd::vlwUse(...);
    int32_t DisplayFastEpd::vlwUseSystem(...);
    int32_t DisplayFastEpd::vlwUnload(...);
    int32_t DisplayFastEpd::vlwClearAll(...);

must be fully implemented.

Revision note: Created on 2026-03-11 to provide a decision-complete implementation plan for replacing FastEPD’s current BBF-based VLW shim with real VLW parsing, rendering, and lifecycle parity while keeping the module split intentionally small.

Revision note: Updated on 2026-03-11 to make the migration explicit: backward compatibility with the old BBF/BPP approximation is not required, and apps must switch to VLW fonts for proportional text on FastEPD.

Revision note: Updated on 2026-03-11 after implementation to record the shipped module split, the FastEPD cleanup hook, and the build commands that succeeded in this working tree.
