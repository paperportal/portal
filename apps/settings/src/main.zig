const std = @import("std");
const sdk = @import("paper_portal_sdk");
const core = sdk.core;
const display = sdk.display;
const devserver = sdk.devserver;
const Error = sdk.errors.Error;

const View = enum {
    Settings,
    DevServer,
};

const DevServerState = enum {
    Stopped,
    Starting,
    Running,
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

var g_view: View = .Settings;
var g_layout: UiLayout = undefined;
var g_status_buf: [96]u8 = [_]u8{0} ** 96;
var g_status_len: usize = 0;

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
    const msg = core.lastErrorMessage(msg_buf[0..]) catch "";

    var out: [96]u8 = undefined;
    const text = std.fmt.bufPrintZ(&out, "{s}: {s}", .{ prefix, msg }) catch return;
    setStatusZ(text);
}

fn readDevServerState() DevServerState {
    if (devserver.isRunning()) return .Running;
    if (devserver.isStarting()) return .Starting;
    return .Stopped;
}

fn setStatusFromDevServerError(prefix: []const u8) void {
    var msg_buf: [128]u8 = undefined;
    const msg = devserver.getLastError(msg_buf[0..]) catch "";
    if (msg.len == 0) {
        setStatusFromLastError(prefix);
        return;
    }

    var out: [96]u8 = undefined;
    const text = std.fmt.bufPrintZ(&out, "{s}: {s}", .{ prefix, msg }) catch return;
    setStatusZ(text);
}

fn drawSettings() Error!void {
    g_view = .Settings;

    const screen_w = display.width();
    const screen_h = display.height();
    if (screen_w <= 0 or screen_h <= 0) return Error.Internal;

    try display.vlw.useSystem(display.vlw.SystemFont.inter, 12);
    try display.text.setEncodingUtf8();
    try display.text.setWrap(false, false);
    try display.text.setColor(display.colors.BLACK, display.colors.WHITE);
    try display.text.setSize(0.8, 0.8);

    const margin: i32 = 16;
    const row_h: i32 = 64;
    const row_gap: i32 = 20;

    const title_y: i32 = margin;
    const rows_y: i32 = 80;

    g_layout.back_btn = .{ .x = margin, .y = screen_h - 56, .w = 120, .h = 40 };
    g_layout.devmode_row = .{ .x = margin, .y = rows_y, .w = screen_w - (2 * margin), .h = row_h };
    g_layout.devserver_row = .{ .x = margin, .y = rows_y + row_h + row_gap, .w = screen_w - (2 * margin), .h = row_h };
    g_layout.stop_btn = .{ .x = margin, .y = screen_h - 56, .w = 120, .h = 40 };

    const state = readDevServerState();
    const state_text = switch (state) {
        .Running => "ON",
        .Starting => "STARTING",
        .Stopped => "OFF",
    };

    try display.epd.setMode(display.epd.QUALITY);
    try display.startWrite();
    defer display.endWrite() catch {};

    try display.fillRect(0, 0, screen_w, screen_h, display.colors.WHITE);
    try display.text.draw("Settings", margin, title_y);

    try display.drawRect(g_layout.devmode_row.x, g_layout.devmode_row.y, g_layout.devmode_row.w, g_layout.devmode_row.h, display.colors.BLACK);
    try display.text.draw("Developer Mode", g_layout.devmode_row.x + 12, g_layout.devmode_row.y + 18);
    try display.text.draw(state_text, g_layout.devmode_row.x + g_layout.devmode_row.w - 140, g_layout.devmode_row.y + 18);

    if (state != .Stopped) {
        try display.drawRect(g_layout.devserver_row.x, g_layout.devserver_row.y, g_layout.devserver_row.w, g_layout.devserver_row.h, display.colors.BLACK);
        try display.text.draw("Dev Server", g_layout.devserver_row.x + 12, g_layout.devserver_row.y + 18);
        try display.text.draw(
            if (state == .Running) "Show" else "Status",
            g_layout.devserver_row.x + g_layout.devserver_row.w - 88,
            g_layout.devserver_row.y + 18,
        );
    }

    try display.drawRect(g_layout.back_btn.x, g_layout.back_btn.y, g_layout.back_btn.w, g_layout.back_btn.h, display.colors.BLACK);
    try display.text.draw("Back", g_layout.back_btn.x + 28, g_layout.back_btn.y + 10);

    if (g_status_len > 0) {
        try display.text.draw(g_status_buf[0..g_status_len], margin, screen_h - 90);
    }

    try display.updateRect(0, 0, screen_w, screen_h);
}

fn drawDevServer() Error!void {
    g_view = .DevServer;

    const screen_w = display.width();
    const screen_h = display.height();
    if (screen_w <= 0 or screen_h <= 0) return Error.Internal;

    try display.vlw.useSystem(display.vlw.SystemFont.inter, 12);
    try display.text.setEncodingUtf8();
    try display.text.setWrap(false, false);
    try display.text.setColor(display.colors.BLACK, display.colors.WHITE);
    try display.text.setSize(0.6, 0.6);

    const margin: i32 = 16;
    g_layout.back_btn = .{ .x = margin, .y = screen_h - 56, .w = 120, .h = 40 };
    g_layout.stop_btn = .{ .x = screen_w - margin - 160, .y = screen_h - 56, .w = 160, .h = 40 };
    const state = readDevServerState();
    const state_text = switch (state) {
        .Running => "Running",
        .Starting => "Starting",
        .Stopped => "Stopped",
    };

    var url_buf: [96]u8 = undefined;
    const url = devserver.getUrl(url_buf[0..]) catch "";

    var ssid_buf: [64]u8 = undefined;
    const ssid = devserver.getApSsid(ssid_buf[0..]) catch "";

    var pw_buf: [32]u8 = undefined;
    const pw = devserver.getApPassword(pw_buf[0..]) catch "";
    var err_buf: [128]u8 = undefined;
    const last_err = devserver.getLastError(err_buf[0..]) catch "";

    try display.epd.setMode(display.epd.QUALITY);
    try display.startWrite();
    defer display.endWrite() catch {};

    try display.fillRect(0, 0, screen_w, screen_h, display.colors.WHITE);
    try display.text.draw("Dev Server", margin, margin);

    try display.text.setSize(0.5, 0.5);
    try display.text.draw("Status:", margin, 70);
    try display.text.draw(state_text, margin + 90, 70);

    if (state == .Running) {
        try display.text.draw("Open this URL:", margin, 96);
        try display.text.draw(url, margin, 122);

        if (ssid.len > 0) {
            try display.text.draw("SoftAP:", margin, 160);
            try display.text.draw(ssid, margin, 184);
            if (pw.len > 0) {
                try display.text.draw("Password:", margin, 220);
                try display.text.draw(pw, margin, 244);
            }
        }
    } else if (state == .Starting) {
        try display.text.draw("Server is starting...", margin, 106);
    } else {
        try display.text.draw("Server is stopped.", margin, 106);
        if (last_err.len > 0) {
            try display.text.draw("Last error:", margin, 146);
            try display.text.draw(last_err, margin, 170);
        }
    }

    try display.drawRect(g_layout.back_btn.x, g_layout.back_btn.y, g_layout.back_btn.w, g_layout.back_btn.h, display.colors.BLACK);
    try display.text.setSize(0.6, 0.6);
    try display.text.draw("Back", g_layout.back_btn.x + 28, g_layout.back_btn.y + 10);

    if (state != .Stopped) {
        try display.drawRect(g_layout.stop_btn.x, g_layout.stop_btn.y, g_layout.stop_btn.w, g_layout.stop_btn.h, display.colors.BLACK);
        try display.text.draw("Stop", g_layout.stop_btn.x + 44, g_layout.stop_btn.y + 10);
    }

    try display.updateRect(0, 0, screen_w, screen_h);
}

pub fn main() !void {
    core.begin() catch {
        core.log.err("ppInit: core.begin failed");
        return;
    };

    drawSettings() catch {
        core.log.err("ppInit: drawSettings failed");
        return;
    };

    core.log.info("Settings app initialized.");
    return;
}

pub export fn ppOnGesture(kind: i32, x: i32, y: i32, dx: i32, dy: i32, duration_ms: i32, now_ms: i32, flags: i32) i32 {
    _ = dx;
    _ = dy;
    _ = duration_ms;
    _ = now_ms;
    _ = flags;
    if (kind == 1) {
        if (g_view == .Settings) {
            if (g_layout.back_btn.contains(x, y)) {
                clearStatus();
                core.exitApp() catch {
                    setStatusFromLastError("exit failed");
                    drawSettings() catch {
                        core.log.err("drawSettings failed");
                    };
                };
                return 0;
            }

            if (g_layout.devmode_row.contains(x, y)) {
                clearStatus();
                const state = readDevServerState();
                if (state != .Stopped) {
                    devserver.stop() catch {
                        setStatusFromLastError("stop failed");
                        drawSettings() catch {
                            core.log.err("drawSettings failed");
                        };
                        return 0;
                    };
                    setStatusZ("Dev server stopped");
                } else {
                    devserver.start() catch {
                        setStatusFromLastError("start failed");
                        drawSettings() catch {
                            core.log.err("drawSettings failed");
                        };
                        return 0;
                    };
                    setStatusZ("Dev server starting...");
                }

                drawSettings() catch {
                    core.log.err("drawSettings failed");
                };
                return 0;
            }

            if (readDevServerState() != .Stopped and g_layout.devserver_row.contains(x, y)) {
                drawDevServer() catch {
                    core.log.err("drawDevServer failed");
                };
                return 0;
            }
        } else if (g_view == .DevServer) {
            if (g_layout.back_btn.contains(x, y)) {
                drawSettings() catch {
                    core.log.err("drawSettings failed");
                };
                return 0;
            }
            if (g_layout.stop_btn.contains(x, y)) {
                clearStatus();
                devserver.stop() catch {
                    setStatusFromDevServerError("stop failed");
                    drawSettings() catch {
                        core.log.err("drawSettings failed");
                    };
                    return 0;
                };
                setStatusZ("Dev server stopped");
                drawSettings() catch {
                    core.log.err("drawSettings failed");
                };
                return 0;
            }
        }
    }
    return 0;
}

pub export fn ppShutdown() void {
    display.vlw.clearAll() catch {};
}
