const std = @import("std");

const sdk = @import("paper_portal_sdk");
const core = sdk.core;
const display = sdk.display;
const fs = sdk.fs;
const Error = sdk.errors.Error;

const paths = @import("../paths.zig");

const settings_icon_png_bytes = @embedFile("../assets/icon-settings.png");

fn logWarn(comptime fmt: []const u8, args: anytype) void {
    var buf: [384]u8 = undefined;
    const msg = std.fmt.bufPrintZ(&buf, fmt, args) catch return;
    core.log.warn(msg);
}

fn lastHostErrorMessage(buf: []u8) []const u8 {
    return core.lastErrorMessage(buf) catch "";
}

pub const Rect = struct {
    x: i32,
    y: i32,
    w: i32,
    h: i32,

    pub fn contains(self: Rect, px: i32, py: i32) bool {
        return px >= self.x and py >= self.y and px < (self.x + self.w) and py < (self.y + self.h);
    }
};

pub const UiApp = struct {
    id_z: [:0]const u8,
    title: []const u8,
};

pub const Cell = struct {
    id_z: [:0]const u8,
    rect: Rect,
    title: []const u8,
};

pub const GridState = struct {
    cells: []Cell,
    header_h: i32,
};

const GridLayout = struct {
    screen_w: i32,
    screen_h: i32,
    header_h: i32,

    content_y: i32,
    content_h: i32,

    side_margin: i32,
    bottom_margin: i32,
    top_margin: i32,
    gap_x: i32,
    gap_y: i32,
    columns: i32,

    col_w: i32,

    icon_size: i32,
    icon_title_gap: i32,
    title_h: i32,
    cell_h: i32,

    start_y: i32,
    draw_count: usize,
};

const CellFrame = struct {
    cell_x: i32,
    cell_y: i32,
    icon_rect: Rect,
};

fn initTitleTextStyle(title_scale: f32) !i32 {
    try display.vlw.useSystem(display.vlw.SystemFont.inter, 12);
    try display.text.setEncodingUtf8();
    try display.text.setSize(title_scale, title_scale);
    try display.text.setWrap(false, false);
    try display.text.setColor(display.colors.BLACK, display.colors.WHITE);
    return display.text.fontHeight();
}

fn computeGridLayout(screen_w: i32, screen_h: i32, header_h: i32, apps_len: usize, title_h: i32) !GridLayout {
    const side_margin: i32 = 16;
    const bottom_margin: i32 = 16;
    const top_margin: i32 = 24;
    const gap_x: i32 = 24;
    const gap_y: i32 = 24;
    const columns: i32 = 3;

    const icon_size: i32 = 128;
    const icon_title_gap: i32 = 8;

    const content_y: i32 = header_h;
    const content_h: i32 = screen_h - header_h;

    const total_gap_x = gap_x * (columns - 1);
    const col_w: i32 = @divTrunc(screen_w - (2 * side_margin) - total_gap_x, columns);

    const cell_h: i32 = icon_size + icon_title_gap + title_h;
    const avail_h = content_h - top_margin - bottom_margin;
    const draw_count: usize = if (avail_h < cell_h) blk: {
        break :blk 0;
    } else blk: {
        const rows_fit: i32 = @divTrunc(avail_h + gap_y, cell_h + gap_y);
        const max_cells_i32: i32 = rows_fit * columns;
        const max_cells: usize = @intCast(@max(0, max_cells_i32));
        break :blk @min(apps_len, max_cells);
    };

    return .{
        .screen_w = screen_w,
        .screen_h = screen_h,
        .header_h = header_h,
        .content_y = content_y,
        .content_h = content_h,
        .side_margin = side_margin,
        .bottom_margin = bottom_margin,
        .top_margin = top_margin,
        .gap_x = gap_x,
        .gap_y = gap_y,
        .columns = columns,
        .col_w = col_w,
        .icon_size = icon_size,
        .icon_title_gap = icon_title_gap,
        .title_h = title_h,
        .cell_h = cell_h,
        .start_y = content_y + top_margin,
        .draw_count = draw_count,
    };
}

fn cellFrame(layout: *const GridLayout, idx: i32) CellFrame {
    const col: i32 = @mod(idx, layout.columns);
    const row: i32 = @divTrunc(idx, layout.columns);

    const cell_x: i32 = layout.side_margin + col * (layout.col_w + layout.gap_x);
    const cell_y: i32 = layout.start_y + row * (layout.cell_h + layout.gap_y);

    const icon_x: i32 = cell_x + @divTrunc(layout.col_w - layout.icon_size, 2);
    const icon_y: i32 = cell_y;

    return .{
        .cell_x = cell_x,
        .cell_y = cell_y,
        .icon_rect = .{ .x = icon_x, .y = icon_y, .w = layout.icon_size, .h = layout.icon_size },
    };
}

fn drawPlaceholderIcon(icon_rect: Rect) void {
    display.fillRect(icon_rect.x, icon_rect.y, icon_rect.w, icon_rect.h, display.colors.LIGHT_GRAY) catch {};
    display.drawRect(icon_rect.x, icon_rect.y, icon_rect.w, icon_rect.h, display.colors.BLACK) catch {};
}

fn drawAppIcon(app: UiApp, icon_rect: Rect) !void {
    if (std.mem.eql(u8, app.id_z, "settings")) {
        try display.image.drawPng(icon_rect.x, icon_rect.y, settings_icon_png_bytes);
        return;
    }

    var icon_path_buf: [256]u8 = undefined;
    const icon_path_z = paths.appIconPathZ(&icon_path_buf, app.id_z);

    const meta = fs.metadata(icon_path_z) catch |err| blk: {
        var host_buf: [192]u8 = undefined;
        const host_msg = lastHostErrorMessage(host_buf[0..]);
        logWarn(
            "launcher: icon stat failed id={s} path={s} err={s} host={s}\n",
            .{ app.id_z, icon_path_z, @errorName(err), host_msg },
        );
        break :blk null;
    };

    const ok = if (meta) |m| (!m.is_dir and m.size != 0) else false;
    if (!ok) {
        drawPlaceholderIcon(icon_rect);
        return;
    }

    display.fillRect(icon_rect.x, icon_rect.y, icon_rect.w, icon_rect.h, display.colors.WHITE) catch {};
    display.image.drawPngFile(icon_path_z, icon_rect.x, icon_rect.y, icon_rect.w, icon_rect.h) catch |err| {
        var host_buf: [192]u8 = undefined;
        const host_msg = lastHostErrorMessage(host_buf[0..]);
        logWarn(
            "launcher: icon draw failed id={s} path={s} size={d} err={s} host={s}\n",
            .{ app.id_z, icon_path_z, meta.?.size, @errorName(err), host_msg },
        );
        drawPlaceholderIcon(icon_rect);
    };
}

fn titleTextWidth(title: []const u8) !i32 {
    var buf: [64]u8 = undefined;
    if (buf.len == 0 or title.len == 0) return 0;

    const max_copy = @min(title.len, buf.len - 1);
    std.mem.copyForwards(u8, buf[0..max_copy], title[0..max_copy]);
    buf[max_copy] = 0;
    return display.text.textWidth(buf[0..max_copy :0]);
}

pub fn draw(allocator: std.mem.Allocator, apps: []const UiApp, header_h: i32) !GridState {
    const screen_w = display.width();
    const screen_h = display.height();
    if (header_h < 0 or header_h > screen_h) return Error.InvalidArgument;

    const title_scale: f32 = 0.6;
    const title_h = try initTitleTextStyle(title_scale);
    const layout = try computeGridLayout(screen_w, screen_h, header_h, apps.len, title_h);
    if (layout.draw_count == 0) return .{ .cells = try allocator.alloc(Cell, 0), .header_h = header_h };

    //    try display.epd.setMode(display.epd.QUALITY);
    try display.startWrite();
    defer display.endWrite() catch {};

    try display.fillRect(0, layout.content_y, layout.screen_w, layout.content_h, display.colors.WHITE);

    var cells = try allocator.alloc(Cell, layout.draw_count);
    errdefer allocator.free(cells);

    for (apps[0..layout.draw_count], 0..) |app, idx_usize| {
        const idx: i32 = std.math.cast(i32, idx_usize) orelse return Error.Internal;
        const frame = cellFrame(&layout, idx);

        try drawAppIcon(app, frame.icon_rect);

        const text_w: i32 = try titleTextWidth(app.title);
        const title_x: i32 = frame.cell_x + @divTrunc(layout.col_w - text_w, 2);
        const title_y: i32 = frame.icon_rect.y + layout.icon_size + layout.icon_title_gap;
        try display.text.draw(app.title, title_x, title_y);

        cells[idx_usize] = .{ .id_z = app.id_z, .rect = frame.icon_rect, .title = app.title };
    }

    //try display.updateRect(0, layout.content_y, layout.screen_w, layout.content_h);
    try display.update();

    return .{ .cells = cells, .header_h = header_h };
}

pub fn hitTest(grid: *const GridState, x: i32, y: i32) ?[:0]const u8 {
    for (grid.cells) |cell| {
        if (cell.rect.contains(x, y)) {
            return cell.id_z;
        }
    }
    return null;
}
