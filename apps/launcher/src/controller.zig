const std = @import("std");

const sdk = @import("paper_portal_sdk");
const core = sdk.core;
const fs = sdk.fs;
const microtask = sdk.microtask;
const ui = sdk.ui;
const Error = sdk.errors.Error;

const catalog_mod = @import("catalog.zig");
const installer = @import("installer.zig");
const paths = @import("paths.zig");
const grid = @import("ui/grid.zig");
const popup = @import("ui/popup.zig");

pub const Controller = struct {
    allocator: std.mem.Allocator,
    header_h: i32,

    catalog: catalog_mod.Catalog = .{},
    ui_apps: std.ArrayListUnmanaged(grid.UiApp) = .{},
    grid_state: ?grid.GridState = null,

    state: State = .BootDrawn,
    sd_ready: bool = false,
    pending_install_files: ?[]PappFile = null,

    pub const PappFile = struct {
        path_z: [:0]const u8,
    };

    pub const State = union(enum) {
        BootDrawn,
        PruneMissingFolders: struct { index: usize, dirty: bool },
        ScanForPapps,
        ShowInstallPopup: struct { until_ms: u32 },
        InstallQueue: struct { files: []PappFile, index: usize, dirty: bool },
        Done,
    };

    fn setState(self: *Controller, new_state: State) void {
        const old_tag = @tagName(self.state);
        const new_tag = @tagName(new_state);
        core.log.finfo("State: {s} -> {s}", .{ old_tag, new_tag });
        self.state = new_state;
    }

    pub fn init(allocator: std.mem.Allocator, header_h: i32) !Controller {
        var self: Controller = .{
            .allocator = allocator,
            .header_h = header_h,
        };

        fs.mount() catch |err| switch (err) {
            Error.NotReady => {
                core.log.warn("SD not mounted; launcher running without SD apps");
                self.sd_ready = false;
            },
            else => return err,
        };

        if (fs.isMounted()) {
            self.sd_ready = true;
            catalog_mod.ensureBaseDirs() catch {};
            installer.recoverIncompleteInstalls(allocator);
        }

        if (self.sd_ready) {
            const loaded = try catalog_mod.load(allocator, paths.catalog_path_z);
            self.catalog = loaded.catalog;
            const initial_dirty = loaded.was_missing;
            try self.rebuildUiApps();
            try self.redrawGrid();
            self.setState(.{ .PruneMissingFolders = .{ .index = 0, .dirty = initial_dirty } });
        } else {
            try self.rebuildUiApps();
            try self.redrawGrid();
            self.setState(.Done);
        }

        return self;
    }

    pub fn deinit(self: *Controller) void {
        if (self.grid_state) |gs| {
            self.allocator.free(gs.cells);
        }
        for (self.ui_apps.items) |a| {
            if (!std.mem.eql(u8, a.id_z, "settings")) {
                self.allocator.free(@constCast(a.id_z));
            }
        }
        self.ui_apps.deinit(self.allocator);
        self.catalog.deinit(self.allocator);
        self.* = undefined;
    }

    pub fn step(self: *Controller, now_ms: u32) anyerror!microtask.Action {
        const budget_ms: i32 = 5;
        const start_ms = core.time.millis();
        while (core.time.millis() - start_ms <= budget_ms) {
            switch (self.state) {
                .BootDrawn => self.setState(if (self.sd_ready) .{ .PruneMissingFolders = .{ .index = 0, .dirty = false } } else .Done),
                .PruneMissingFolders => |*s| {
                    if (s.index >= self.catalog.apps.items.len) {
                        if (s.dirty) {
                            catalog_mod.saveAtomic(&self.catalog, self.allocator, paths.catalog_path_z) catch |err| {
                                var buf: [192]u8 = undefined;
                                const msg = std.fmt.bufPrintZ(
                                    &buf,
                                    "apps.json: save failed ({s}) path={s}",
                                    .{ @errorName(err), paths.catalog_path_z },
                                ) catch "apps.json: save failed";
                                core.log.warn(msg);
                            };
                        }
                        self.setState(.ScanForPapps);
                        continue;
                    }

                    var root_buf: [96]u8 = undefined;
                    const root_z = paths.appRootZ(&root_buf, self.catalog.apps.items[s.index].id);
                    const meta = fs.metadata(root_z) catch |err| switch (err) {
                        Error.NotFound => null,
                        else => null,
                    };
                    if (meta == null or meta.?.is_dir == false) {
                        core.log.info("Removed missing app from catalog");
                        self.catalog.removeAt(self.allocator, s.index);
                        s.dirty = true;
                        continue;
                    }
                    s.index += 1;
                },
                .ScanForPapps => {
                    const files = self.scanForPapps() catch {
                        self.setState(.Done);
                        continue;
                    };

                    if (files.len == 0) {
                        self.allocator.free(files);
                        self.setState(.Done);
                        continue;
                    }

                    var msg_buf: [64]u8 = undefined;
                    const msg = std.fmt.bufPrint(&msg_buf, "Installing {d} app package(s)…", .{files.len}) catch "Installing…";
                    popup.show(msg);
                    popup.draw() catch {};

                    self.setState(.{ .ShowInstallPopup = .{ .until_ms = now_ms + 1500 } });
                    self.pending_install_files = files;
                },
                .ShowInstallPopup => |s| {
                    if (now_ms < s.until_ms) {
                        const delta = s.until_ms - now_ms;
                        return microtask.Action.sleepMs(delta);
                    }

                    if (self.pending_install_files) |files| {
                        self.pending_install_files = null;
                        self.setState(.{ .InstallQueue = .{ .files = files, .index = 0, .dirty = false } });
                    } else {
                        popup.hide();
                        self.redrawGrid() catch {};
                        self.setState(.Done);
                    }
                },
                .InstallQueue => |*s| {
                    if (s.index >= s.files.len) {
                        if (s.dirty) {
                            catalog_mod.saveAtomic(&self.catalog, self.allocator, paths.catalog_path_z) catch |err| {
                                var buf: [192]u8 = undefined;
                                const msg = std.fmt.bufPrintZ(
                                    &buf,
                                    "apps.json: save failed ({s}) path={s}",
                                    .{ @errorName(err), paths.catalog_path_z },
                                ) catch "apps.json: save failed";
                                core.log.warn(msg);
                            };
                            self.rebuildUiApps() catch {};
                        }

                        popup.hide();
                        self.redrawGrid() catch {};
                        self.freePappFiles(s.files);
                        self.setState(.Done);
                        continue;
                    }

                    const file = s.files[s.index];
                    const install_ok = installer.installOne(self.allocator, &self.catalog, file.path_z) catch blk: {
                        break :blk false;
                    };
                    if (install_ok) {
                        s.dirty = true;
                    }

                    fs.remove(file.path_z) catch {};
                    s.index += 1;
                },
                .Done => return microtask.Action.doneNow(),
            }

            break;
        }

        return microtask.Action.yieldSoon();
    }

    pub fn draw(self: *Controller, ctx: *ui.Context) anyerror!void {
        _ = ctx;
        try self.redrawGrid();
    }

    pub fn onGesture(self: *Controller, ctx: *ui.Context, nav: *ui.Navigator, ev: ui.GestureEvent) anyerror!void {
        _ = ctx;
        _ = nav;
        if (ev.kind == .tap) {
            self.onTap(ev.x, ev.y);
        }
    }

    pub fn onTap(self: *Controller, x: i32, y: i32) void {
        if (popup.isVisible()) return;
        const gs = self.grid_state orelse return;
        const id_z = grid.hitTest(&gs, x, y) orelse return;
        if (std.mem.eql(u8, id_z, "settings")) {
            core.openApp("settings", null) catch {
                core.log.err("Failed to open settings app");
            };
            return;
        }

        core.openApp(id_z, null) catch {
            core.log.err("Failed to open app");
        };
    }

    fn rebuildUiApps(self: *Controller) !void {
        for (self.ui_apps.items) |a| {
            if (!std.mem.eql(u8, a.id_z, "settings")) {
                self.allocator.free(@constCast(a.id_z));
            }
        }
        self.ui_apps.clearRetainingCapacity();

        try self.ui_apps.append(self.allocator, .{ .id_z = "settings", .title = "Settings" });
        for (self.catalog.apps.items) |app| {
            const id_z = try self.allocator.dupeZ(u8, app.id);
            try self.ui_apps.append(self.allocator, .{ .id_z = id_z, .title = app.name });
        }
    }

    fn redrawGrid(self: *Controller) !void {
        if (self.grid_state) |gs| {
            self.allocator.free(gs.cells);
        }
        self.grid_state = try grid.draw(self.allocator, self.ui_apps.items, self.header_h);
    }

    fn scanForPapps(self: *Controller) ![]PappFile {
        var list: std.ArrayList(PappFile) = .empty;
        errdefer {
            for (list.items) |pf| self.allocator.free(@constCast(pf.path_z));
            list.deinit(self.allocator);
        }

        var d = fs.Dir.open(paths.apps_dir_z) catch |err| switch (err) {
            Error.NotFound => return list.toOwnedSlice(self.allocator),
            else => return err,
        };
        defer d.close() catch {};

        var name_buf: [96]u8 = undefined;
        while (true) {
            const n_opt = d.readName(name_buf[0..]) catch break;
            if (n_opt == null) break;
            const n = n_opt.?;
            const name = name_buf[0..n];
            if (name.len != 0 and name[0] == '.') continue;
            if (!std.mem.endsWith(u8, name, ".papp")) continue;

            var path_buf: [160]u8 = undefined;
            const path_z = std.fmt.bufPrintZ(&path_buf, "{s}/{s}", .{ paths.apps_dir_z, name }) catch continue;
            try list.append(self.allocator, .{ .path_z = try self.allocator.dupeZ(u8, path_z) });
        }

        return list.toOwnedSlice(self.allocator);
    }

    fn freePappFiles(self: *Controller, files: []PappFile) void {
        for (files) |pf| {
            self.allocator.free(@constCast(pf.path_z));
        }
        self.allocator.free(files);
    }
};
