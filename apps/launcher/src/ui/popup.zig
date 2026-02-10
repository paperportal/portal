const std = @import("std");

const sdk = @import("paper_portal_sdk");
const display = sdk.display;
const Error = sdk.errors.Error;

var g_visible: bool = false;
var g_msg_buf: [128]u8 = [_]u8{0} ** 128;
var g_msg_len: usize = 0;

pub fn isVisible() bool {
    return g_visible;
}

pub fn show(message: []const u8) void {
    const max_copy = @min(message.len, g_msg_buf.len - 1);
    std.mem.copyForwards(u8, g_msg_buf[0..max_copy], message[0..max_copy]);
    g_msg_buf[max_copy] = 0;
    g_msg_len = max_copy;
    g_visible = true;
}

pub fn hide() void {
    g_visible = false;
}

pub fn draw() Error!void {
    if (!g_visible) return;

    const screen_w = display.width();
    const screen_h = display.height();
    if (screen_w <= 0 or screen_h <= 0) return Error.Internal;

    const popup_w: i32 = @min(700, screen_w - 40);
    const popup_h: i32 = 140;
    const x: i32 = @divTrunc(screen_w - popup_w, 2);
    const y: i32 = @divTrunc(screen_h - popup_h, 2);

    try display.text.setEncodingUtf8();
    try display.text.setWrap(false, false);
    try display.text.setColor(display.colors.BLACK, display.colors.WHITE);
    try display.text.setSize(0.8, 0.8);

    try display.epd.setMode(display.epd.TEXT);
    try display.startWrite();
    defer display.endWrite() catch {};

    try display.fillRect(x, y, popup_w, popup_h, display.colors.WHITE);
    try display.drawRect(x, y, popup_w, popup_h, display.colors.BLACK);

    if (g_msg_len > 0) {
        try display.text.draw(g_msg_buf[0..g_msg_len], x + 20, y + 50);
    }

    try display.updateRect(x, y, popup_w, popup_h);
}

