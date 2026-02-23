const std = @import("std");
const Error = sdk.errors.Error;

const sdk = @import("paper_portal_sdk");
const core = sdk.core;
const display = sdk.display;
const net = sdk.net;

const header_png_bytes = @embedFile("../assets/main-header.png");

const PngDimensions = struct {
    w: u32,
    h: u32,
};

pub fn draw() Error!i32 {
    const dims = try pngDimensions(header_png_bytes);
    const screen_w = display.width();
    const header_h: i32 = std.math.cast(i32, dims.h) orelse return Error.InvalidArgument;

    try display.epd.setMode(display.epd.FAST);
    try display.startWrite();
    defer display.endWrite() catch {};

    try display.fillRect(0, 0, screen_w, header_h, display.colors.WHITE);
    try display.image.drawPng(0, 0, header_png_bytes);

    drawBatteryStatus(screen_w) catch {};

    const wifi_icon = blk: {
        const mode = net.wifiGetMode() catch break :blk display.image.icon.wifi;
        break :blk switch (mode) {
            .ap, .apSta => display.image.icon.softap,
            .off, .sta => display.image.icon.wifi,
        };
    };
    try display.image.drawIcon(screen_w - 100, 10, wifi_icon);

    if (sdk.devserver.isRunning()) {
        try display.image.drawIcon(screen_w - 100, 38, display.image.icon.devserver);
    }

    try display.updateRect(0, 0, screen_w, header_h);
    return header_h;
}

fn drawBatteryStatus(screen_w: i32) !void {
    try sdk.power.begin();

    const percentage: ?i32 = sdk.power.batteryLevelPercent() catch |err| blk: {
        core.log.fwarn("power.batteryLevelPercent failed: {s}", .{@errorName(err)});
        break :blk null;
    };
    const volt_mv: ?i32 = sdk.power.batteryVoltageMv() catch |err| blk: {
        core.log.fwarn("power.batteryVoltageMv failed: {s}", .{@errorName(err)});
        break :blk null;
    };

    const bar_length: i32 = if (percentage) |p| blk: {
        const clamped = std.math.clamp(p, 0, 100);
        break :blk @divTrunc(clamped * 34, 100);
    } else 0;

    const battery_x: i32 = screen_w - 64;
    const battery_y: i32 = 16;
    try display.image.drawIcon(battery_x, battery_y, display.image.Icon.battery);
    try display.fillRect(battery_x + 5, battery_y + 4, bar_length, 14, display.gray4(3));

    if (volt_mv) |mv| {
        var volt_buf: [16]u8 = undefined;
        const whole: i32 = @divTrunc(mv, 1000);
        const frac: i32 = @divTrunc(@mod(mv, 1000), 10);
        const volt_str = try std.fmt.bufPrintZ(&volt_buf, "{d}.{d:0>2}V", .{ whole, @as(u32, @intCast(frac)) });

        display.vlw.useSystem(display.vlw.SystemFont.inter, 9) catch {};
        try display.text.setEncodingUtf8();
        try display.text.setWrap(false, false);
        try display.text.setColor(display.colors.BLACK, display.colors.WHITE);
        try display.text.setSize(0.6, 0.6);

        const width = display.text.textWidth(volt_str) catch 50;

        try display.text.draw(volt_str, battery_x + 25 - @divTrunc(width, 2), battery_y + 28);
    }
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

fn readBeU32(bytes: *const [4]u8) u32 {
    return (@as(u32, bytes.*[0]) << 24) |
        (@as(u32, bytes.*[1]) << 16) |
        (@as(u32, bytes.*[2]) << 8) |
        @as(u32, bytes.*[3]);
}
