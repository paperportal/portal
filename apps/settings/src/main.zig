const std = @import("std");
const sdk = @import("paper_portal_sdk");
const core = sdk.core;
const display = sdk.display;
const devserver = sdk.devserver;
const Error = sdk.errors.Error;

const font_bytes = @embedFile("assets/Inter-Medium-32.vlw");

const allocator = std.heap.wasm_allocator;

const View = enum {
    Home,
    Settings,
    DevServer,
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
    back_btn: Rect,
    devmode_row: Rect,
    devserver_row: Rect,
    stop_btn: Rect,
};

var g_view: View = .Home;
var g_layout: UiLayout = undefined;
var g_font_handle: i32 = 0;
var g_status_buf: [96]u8 = [_]u8{0} ** 96;
var g_status_len: usize = 0;
var g_initialized: bool = false;

fn clearStatus() void {
    g_status_len = 0;
    g_status_buf[0] = 0;
}

fn setStatusZ(msg: [:0]const u8) void {
    const max_copy = @min(msg.len, g_status_buf.len - 1);
    std.mem.copyForwards(u8, g_status_buf[0..max_copy], msg[0..max_copy]);
    g_status_buf[max_copy] = 0;
    g_status_len = max_copy;
}

fn setStatusFromLastError(prefix: []const u8) void {
    var msg_buf: [128]u8 = undefined;
    const msg = core.last_error_message(msg_buf[0..]) catch "";

    var out: [96]u8 = undefined;
    const text = std.fmt.bufPrintZ(&out, "{s}: {s}", .{ prefix, msg }) catch return;
    setStatusZ(text);
}

fn drawHome() Error!void {
    g_view = .Home;
    clearStatus();

    const screen_w = display.width();
    const screen_h = display.height();
    if (screen_w <= 0 or screen_h <= 0) return Error.Internal;

    if (g_font_handle != 0) {
        try display.vlw.use(g_font_handle);
    }
    try display.text.set_encoding_utf8();
    try display.text.set_wrap(false, false);
    try display.text.set_color(display.colors.BLACK, display.colors.WHITE);
    try display.text.set_size(3, 3);

    const margin: i32 = 16;

    try display.epd.set_mode(display.epd.QUALITY);
    try display.start_write();
    defer display.end_write() catch {};

    try display.fill_rect(0, 0, screen_w, screen_h, display.colors.WHITE);
    try display.text.draw("Settings", margin, margin);

    try display.update_rect(0, 0, screen_w, screen_h);
}

fn drawSettings() Error!void {
    g_view = .Settings;

    const screen_w = display.width();
    const screen_h = display.height();
    if (screen_w <= 0 or screen_h <= 0) return Error.Internal;

    if (g_font_handle != 0) {
        try display.vlw.use(g_font_handle);
    }
    try display.text.set_encoding_utf8();
    try display.text.set_wrap(false, false);
    try display.text.set_color(display.colors.BLACK, display.colors.WHITE);
    try display.text.set_size(3, 3);

    const margin: i32 = 16;
    const row_h: i32 = 64;
    const row_gap: i32 = 20;

    const title_y: i32 = margin;
    const rows_y: i32 = 80;

    g_layout.back_btn = .{ .x = margin, .y = screen_h - 56, .w = 120, .h = 40 };
    g_layout.devmode_row = .{ .x = margin, .y = rows_y, .w = screen_w - (2 * margin), .h = row_h };
    g_layout.devserver_row = .{ .x = margin, .y = rows_y + row_h + row_gap, .w = screen_w - (2 * margin), .h = row_h };
    g_layout.stop_btn = .{ .x = margin, .y = screen_h - 56, .w = 120, .h = 40 };

    const running = devserver.is_running();

    try display.epd.set_mode(display.epd.QUALITY);
    try display.start_write();
    defer display.end_write() catch {};

    try display.fill_rect(0, 0, screen_w, screen_h, display.colors.WHITE);
    try display.text.draw("Settings", margin, title_y);

    try display.draw_rect(g_layout.devmode_row.x, g_layout.devmode_row.y, g_layout.devmode_row.w, g_layout.devmode_row.h, display.colors.BLACK);
    try display.text.draw("Developer Mode", g_layout.devmode_row.x + 12, g_layout.devmode_row.y + 18);
    try display.text.draw(if (running) "ON" else "OFF", g_layout.devmode_row.x + g_layout.devmode_row.w - 64, g_layout.devmode_row.y + 18);

    if (running) {
        try display.draw_rect(g_layout.devserver_row.x, g_layout.devserver_row.y, g_layout.devserver_row.w, g_layout.devserver_row.h, display.colors.BLACK);
        try display.text.draw("Dev Server", g_layout.devserver_row.x + 12, g_layout.devserver_row.y + 18);
        try display.text.draw("Show", g_layout.devserver_row.x + g_layout.devserver_row.w - 72, g_layout.devserver_row.y + 18);
    }

    try display.draw_rect(g_layout.back_btn.x, g_layout.back_btn.y, g_layout.back_btn.w, g_layout.back_btn.h, display.colors.BLACK);
    try display.text.draw("Back", g_layout.back_btn.x + 28, g_layout.back_btn.y + 10);

    if (g_status_len > 0) {
        try display.text.draw(g_status_buf[0..g_status_len], margin, screen_h - 90);
    }

    try display.update_rect(0, 0, screen_w, screen_h);
}

fn drawDevServer() Error!void {
    g_view = .DevServer;

    const screen_w = display.width();
    const screen_h = display.height();
    if (screen_w <= 0 or screen_h <= 0) return Error.Internal;

    if (g_font_handle != 0) {
        try display.vlw.use(g_font_handle);
    }
    try display.text.set_size(2, 2);
    try display.text.set_encoding_utf8();
    try display.text.set_wrap(false, false);
    try display.text.set_color(display.colors.BLACK, display.colors.WHITE);
    try display.text.set_size(0.6, 0.6);

    const margin: i32 = 16;
    g_layout.back_btn = .{ .x = margin, .y = screen_h - 56, .w = 120, .h = 40 };
    g_layout.stop_btn = .{ .x = screen_w - margin - 160, .y = screen_h - 56, .w = 160, .h = 40 };

    var url_buf: [96]u8 = undefined;
    const url = devserver.get_url(url_buf[0..]) catch "";

    var ssid_buf: [64]u8 = undefined;
    const ssid = devserver.get_ap_ssid(ssid_buf[0..]) catch "";

    var pw_buf: [32]u8 = undefined;
    const pw = devserver.get_ap_password(pw_buf[0..]) catch "";

    try display.epd.set_mode(display.epd.QUALITY);
    try display.start_write();
    defer display.end_write() catch {};

    try display.fill_rect(0, 0, screen_w, screen_h, display.colors.WHITE);
    try display.text.draw("Dev Server", margin, margin);

    try display.text.set_size(0.5, 0.5);
    try display.text.draw("Open this URL:", margin, 70);
    try display.text.draw(url, margin, 96);

    if (ssid.len > 0) {
        try display.text.draw("SoftAP:", margin, 140);
        try display.text.draw(ssid, margin, 164);
        if (pw.len > 0) {
            try display.text.draw("Password:", margin, 200);
            try display.text.draw(pw, margin, 224);
        }
    }

    try display.draw_rect(g_layout.back_btn.x, g_layout.back_btn.y, g_layout.back_btn.w, g_layout.back_btn.h, display.colors.BLACK);
    try display.text.set_size(0.6, 0.6);
    try display.text.draw("Back", g_layout.back_btn.x + 28, g_layout.back_btn.y + 10);

    try display.draw_rect(g_layout.stop_btn.x, g_layout.stop_btn.y, g_layout.stop_btn.w, g_layout.stop_btn.h, display.colors.BLACK);
    try display.text.draw("Stop", g_layout.stop_btn.x + 44, g_layout.stop_btn.y + 10);

    try display.update_rect(0, 0, screen_w, screen_h);
}

pub fn main() void {}

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

    drawSettings() catch {
        core.log.err("pp_init: drawSettings failed\n");
        return -1;
    };

    core.log.info("Settings app initialized.\n");
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
            // Home view doesn't have any interactive elements
        } else if (g_view == .Settings) {
            if (g_layout.back_btn.contains(x, y)) {
                drawHome() catch {
                    core.log.err("drawHome failed\n");
                };
                return 0;
            }

            if (g_layout.devmode_row.contains(x, y)) {
                clearStatus();
                if (devserver.is_running()) {
                    devserver.stop() catch {
                        setStatusFromLastError("stop failed");
                    };
                } else {
                    devserver.start() catch {
                        setStatusFromLastError("start failed");
                    };
                }

                drawSettings() catch {
                    core.log.err("drawSettings failed\n");
                };
                return 0;
            }

            if (devserver.is_running() and g_layout.devserver_row.contains(x, y)) {
                drawDevServer() catch {
                    core.log.err("drawDevServer failed\n");
                };
                return 0;
            }
        } else if (g_view == .DevServer) {
            if (g_layout.back_btn.contains(x, y)) {
                drawSettings() catch {
                    core.log.err("drawSettings failed\n");
                };
                return 0;
            }
            if (g_layout.stop_btn.contains(x, y)) {
                clearStatus();
                devserver.stop() catch {
                    setStatusFromLastError("stop failed");
                };
                drawSettings() catch {
                    core.log.err("drawSettings failed\n");
                };
                return 0;
            }
        }
    }
    return 0;
}
