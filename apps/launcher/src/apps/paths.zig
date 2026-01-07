const std = @import("std");

pub const apps_dir_z: [:0]const u8 = "/sdcard/portal/apps";
pub const catalog_path_z: [:0]const u8 = "/sdcard/portal/apps.json";

pub fn appRootZ(buf: []u8, id: []const u8) [:0]const u8 {
    return std.fmt.bufPrintZ(buf, "/sdcard/portal/apps/{s}", .{id}) catch @panic("app_root_z buf too small");
}

pub fn appWasmPathZ(buf: []u8, id: []const u8) [:0]const u8 {
    return std.fmt.bufPrintZ(buf, "/sdcard/portal/apps/{s}/app.wasm", .{id}) catch @panic("app_wasm_path_z buf too small");
}

pub fn appManifestPathZ(buf: []u8, id: []const u8) [:0]const u8 {
    return std.fmt.bufPrintZ(buf, "/sdcard/portal/apps/{s}/manifest.json", .{id}) catch @panic("app_manifest_path_z buf too small");
}

pub fn appIconPathZ(buf: []u8, id: []const u8) [:0]const u8 {
    return std.fmt.bufPrintZ(buf, "/sdcard/portal/apps/{s}/icon.png", .{id}) catch @panic("app_icon_path_z buf too small");
}

pub fn stagingRootZ(buf: []u8, id: []const u8) [:0]const u8 {
    return std.fmt.bufPrintZ(buf, "/sdcard/portal/apps/.staging-{s}", .{id}) catch @panic("staging_root_z buf too small");
}

pub fn backupRootZ(buf: []u8, id: []const u8) [:0]const u8 {
    return std.fmt.bufPrintZ(buf, "/sdcard/portal/apps/.backup-{s}", .{id}) catch @panic("backup_root_z buf too small");
}
