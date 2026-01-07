const std = @import("std");
const sdk = @import("paper_portal_sdk");
const core = sdk.core;
const display = sdk.display;
const Error = sdk.errors.Error;

const font_bytes = @embedFile("assets/Inter-Medium-32.vlw");
const header_png_bytes = @embedFile("assets/main-header.png");
const settings_icon_png_bytes = @embedFile("assets/icon-settings.png");

const allocator = std.heap.wasm_allocator;

const App = struct {
    title: []const u8,
};

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

    try display.epd.set_mode(display.epd.FAST);
    try display.start_write();
    defer display.end_write() catch {};

    try display.fill_rect(0, 0, screen_w, header_h, display.colors.WHITE);
    try display.image.draw_png(0, 0, header_png_bytes);
    try display.update_rect(0, 0, screen_w, header_h);
    return header_h;
}

const View = enum {
    Home,
};

const Rect = struct {
    x: i32,
    y: i32,
    w: i32,
    h: i32,

    pub fn contains(self: Rect, px: i32, py: i32) bool {
        return px >= self.x and py >= self.y and px < (self.x + self.w) and py < (self.y + self.h);
    }
};

const UiLayout = struct {
    header_h: i32,
    settings_cell: Rect,
};

var g_view: View = .Home;
var g_layout: UiLayout = undefined;
var g_font_handle: i32 = 0;

fn drawMockApps(header_h: i32) Error!void {
    const screen_w = display.width();
    const screen_h = display.height();

    const side_margin: i32 = 16;
    const top_margin: i32 = 24;
    const gap_x: i32 = 24;
    const gap_y: i32 = 40;
    const columns: i32 = 3;

    const icon_size: i32 = 128;
    const icon_title_gap: i32 = 8;
    const title_scale: f32 = 2.0;

    const apps = [_]App{
        .{ .title = "Settings" },
        .{ .title = "App 1" },
        .{ .title = "App 2" },
        .{ .title = "App 3" },
        .{ .title = "App 4" },
        .{ .title = "App 5" },
        .{ .title = "App 6" },
        .{ .title = "App 7" },
        .{ .title = "App 8" },
        .{ .title = "App 9" },
        .{ .title = "App 10" },
        .{ .title = "App 11" },
        .{ .title = "App 12" },
    };

    if (screen_w <= 0 or screen_h <= 0) return Error.Internal;
    if (header_h < 0 or header_h > screen_h) return Error.InvalidArgument;

    const content_y: i32 = header_h;
    const content_h: i32 = screen_h - header_h;

    const total_gap_x = gap_x * (columns - 1);
    const col_w: i32 = @divTrunc(screen_w - (2 * side_margin) - total_gap_x, columns);

    if (g_font_handle != 0) {
        try display.vlw.use(g_font_handle);
    }
    try display.text.set_encoding_utf8();
    try display.text.set_size(title_scale, title_scale);
    try display.text.set_wrap(false, false);
    try display.text.set_color(display.colors.BLACK, display.colors.WHITE);

    const title_h: i32 = display.text.font_height();
    const cell_h: i32 = icon_size + icon_title_gap + title_h;

    try display.epd.set_mode(display.epd.QUALITY);
    try display.start_write();
    defer display.end_write() catch {};

    try display.fill_rect(0, content_y, screen_w, content_h, display.colors.WHITE);

    const start_y: i32 = content_y + top_margin;
    for (apps, 0..) |app, idx_usize| {
        const idx: i32 = std.math.cast(i32, idx_usize) orelse return Error.Internal;
        const col: i32 = @mod(idx, columns);
        const row: i32 = @divTrunc(idx, columns);

        const cell_x: i32 = side_margin + col * (col_w + gap_x);
        const cell_y: i32 = start_y + row * (cell_h + gap_y);

        const icon_x: i32 = cell_x + @divTrunc(col_w - icon_size, 2);
        const icon_y: i32 = cell_y;

        if (idx == 0) {
            try display.image.draw_png(icon_x, icon_y, settings_icon_png_bytes);
            g_layout.settings_cell = .{ .x = icon_x, .y = icon_y, .w = icon_size, .h = icon_size };
        } else {
            try display.fill_rect(icon_x, icon_y, icon_size, icon_size, display.colors.BLACK);
        }

        const text_w: i32 = blk: {
            var buf: [64]u8 = undefined;
            if (buf.len == 0) break :blk 0;
            const max_copy = @min(app.title.len, buf.len - 1);
            std.mem.copyForwards(u8, buf[0..max_copy], app.title[0..max_copy]);
            buf[max_copy] = 0;
            break :blk try display.text.text_width(buf[0..max_copy :0]);
        };

        const title_x: i32 = cell_x + @divTrunc(col_w - text_w, 2);
        const title_y: i32 = icon_y + icon_size + icon_title_gap;
        try display.text.draw(app.title, title_x, title_y);
    }
}

var g_initialized: bool = false;

fn drawHome() Error!void {
    g_view = .Home;

    const header_h = try drawHeader();
    g_layout.header_h = header_h;
    try drawMockApps(header_h);
}

pub export fn pp_contract_version() i32 {
    return 1;
}

pub export fn pp_alloc(len: i32) i32 {
    if (len <= 0) return 0;
    const size: usize = @intCast(len);
    const buf = allocator.alloc(u8, size) catch return 0;
    return @intCast(@intFromPtr(buf.ptr));
}

pub export fn pp_free(ptr: i32, len: i32) void {
    if (ptr == 0 or len <= 0) return;
    const size: usize = @intCast(len);
    const addr: usize = @intCast(ptr);
    const buf = @as([*]u8, @ptrFromInt(addr))[0..size];
    allocator.free(buf);
}

pub export fn pp_init(api_version: i32, api_features: i64, screen_w: i32, screen_h: i32, args_ptr: i32, args_len: i32) i32 {
    _ = api_version;
    _ = api_features;
    _ = screen_w;
    _ = screen_h;
    _ = args_ptr;
    _ = args_len;

    if (g_initialized) return 0;
    g_initialized = true;

    core.begin() catch {
        core.log.err("pp_init: core.begin failed\n");
        return -1;
    };

    g_font_handle = display.vlw.register(font_bytes) catch {
        core.log.err("pp_init: font register failed\n");
        return -1;
    };

    drawHome() catch {
        core.log.err("pp_init: drawHome failed\n");
        return -1;
    };

    core.log.info("Launcher header drawn.\n");
    return 0;
}

pub export fn pp_on_gesture(kind: i32, x: i32, y: i32, dx: i32, dy: i32, duration_ms: i32, now_ms: i32, flags: i32) i32 {
    _ = dx;
    _ = dy;
    _ = duration_ms;
    _ = now_ms;
    _ = flags;
    if (kind == 1) {
        if (g_view == .Home) {
            if (g_layout.settings_cell.contains(x, y)) {
                core.open_app("settings", null) catch {
                    core.log.err("Failed to open settings app\n");
                };
                return 0;
            }
        }
    }
    return 0;
}

pub fn main() void {}
