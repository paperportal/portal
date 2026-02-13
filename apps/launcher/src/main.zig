const std = @import("std");
const sdk = @import("paper_portal_sdk");
const core = sdk.core;
const display = sdk.display;
const Error = sdk.errors.Error;

const header_png_bytes = @embedFile("assets/main-header.png");

const allocator = std.heap.wasm_allocator;

const Controller = @import("apps/controller.zig").Controller;

const PngDimensions = struct {
    w: u32,
    h: u32,
};

fn readBeU32(bytes: *const [4]u8) u32 {
    return (@as(u32, bytes.*[0]) << 24) |
        (@as(u32, bytes.*[1]) << 16) |
        (@as(u32, bytes.*[2]) << 8) |
        @as(u32, bytes.*[3]);
}

fn pngDimensions(png: []const u8) Error!PngDimensions {
    const signature = "\x89PNG\r\n\x1a\n";
    if (png.len < 33) return Error.InvalidArgument;
    if (!std.mem.eql(u8, png[0..8], signature)) return Error.InvalidArgument;

    const chunk_len = readBeU32(@ptrCast(png[8..12].ptr));
    if (chunk_len != 13) return Error.InvalidArgument;
    if (!std.mem.eql(u8, png[12..16], "IHDR")) return Error.InvalidArgument;

    const w = readBeU32(@ptrCast(png[16..20].ptr));
    const h = readBeU32(@ptrCast(png[20..24].ptr));
    if (w == 0 or h == 0) return Error.InvalidArgument;
    return .{ .w = w, .h = h };
}

fn drawHeader() Error!i32 {
    const dims = try pngDimensions(header_png_bytes);
    const screen_w = display.width();
    const header_h: i32 = std.math.cast(i32, dims.h) orelse return Error.InvalidArgument;

    try display.epd.setMode(display.epd.FAST);
    try display.startWrite();
    defer display.endWrite() catch {};

    try display.fillRect(0, 0, screen_w, header_h, display.colors.WHITE);
    try display.image.drawPng(0, 0, header_png_bytes);
    try display.updateRect(0, 0, screen_w, header_h);
    return header_h;
}

var g_font_handle: ?i32 = null;

var g_initialized: bool = false;
var g_controller: ?Controller = null;

pub export fn ppInit(api_version: i32, args_ptr: i32, args_len: i32) i32 {
    _ = api_version;
    _ = args_ptr;
    _ = args_len;

    if (g_initialized) return 0;
    g_initialized = true;

    core.begin(.fastepd) catch {
        core.log.err("ppInit: core.begin failed");
        return -1;
    };

    display.vlw.useSystem(display.vlw.SystemFont.inter) catch {};

    const header_h = drawHeader() catch {
        core.log.err("ppInit: drawHeader failed");
        return -1;
    };

    g_controller = Controller.init(allocator, header_h) catch {
        core.log.err("ppInit: controller init failed");
        return -1;
    };

    core.log.info("Launcher header drawn.");
    return 0;
}

pub export fn ppTick(now_ms: i32) i32 {
    if (g_controller) |*c| {
        c.tick(now_ms);
    }
    return 0;
}

pub export fn ppOnGesture(kind: i32, x: i32, y: i32, dx: i32, dy: i32, duration_ms: i32, now_ms: i32, flags: i32) i32 {
    _ = dx;
    _ = dy;
    _ = duration_ms;
    _ = now_ms;
    _ = flags;
    if (kind == 1) {
        if (g_controller) |*c| {
            c.onTap(x, y);
        }
    }
    return 0;
}

pub export fn ppShutdown() void {
    if (g_controller) |*c| {
        c.deinit();
        g_controller = null;
    }
    display.vlw.clearAll() catch {};
}
