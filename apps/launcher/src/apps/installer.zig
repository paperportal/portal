const std = @import("std");

const sdk = @import("paper_portal_sdk");
const core = sdk.core;
const fs = sdk.fs;
const Error = sdk.errors.Error;

const catalog_mod = @import("catalog.zig");
const manifest_mod = @import("manifest.zig");
const paths = @import("paths.zig");
const signing = @import("signing.zig");
const zip_reader = @import("zip_reader.zig");

fn logInfo(comptime fmt: []const u8, args: anytype) void {
    var buf: [384]u8 = undefined;
    const msg = std.fmt.bufPrintZ(&buf, fmt, args) catch return;
    core.log.info(msg);
}

fn rmRf(allocator: std.mem.Allocator, path_z: [:0]const u8) Error!void {
    const meta = fs.metadata(path_z) catch |err| switch (err) {
        Error.NotFound => return,
        else => return err,
    };
    if (!meta.is_dir) {
        fs.remove(path_z) catch {};
        return;
    }

    var d = fs.Dir.open(path_z) catch |err| switch (err) {
        Error.NotFound => return,
        else => return err,
    };
    defer d.close() catch {};

    var name_buf: [96]u8 = undefined;
    while (true) {
        const n_opt = d.readName(name_buf[0..]) catch break;
        if (n_opt == null) break;
        const n = n_opt.?;
        const name = name_buf[0..n];
        if (std.mem.eql(u8, name, ".") or std.mem.eql(u8, name, "..")) continue;

        var child_buf: [256]u8 = undefined;
        const child_z = std.fmt.bufPrintZ(&child_buf, "{s}/{s}", .{ path_z, name }) catch continue;
        try rmRf(allocator, child_z);
    }

    fs.Dir.rmdir(path_z) catch {};
}

fn findCatalogEntry(c: *catalog_mod.Catalog, id: []const u8) ?*catalog_mod.AppEntry {
    for (c.apps.items) |*e| {
        if (std.mem.eql(u8, e.id, id)) return e;
    }
    return null;
}

fn checksumMatches(manifest_checksum: []const u8, digest: [32]u8) bool {
    if (!std.mem.startsWith(u8, manifest_checksum, "sha256:")) return false;
    if (manifest_checksum.len != 7 + 64) return false;
    const hex = std.fmt.bytesToHex(digest, .lower);
    return std.mem.eql(u8, manifest_checksum[7..], hex[0..]);
}

/// Cleans up and recovers state from incomplete or interrupted app installations.
///
/// Scans the apps directory for:
/// - `.staging-*` directories: Leftover temporary staging directories from failed
///   installations. These are deleted.
/// - `.backup-*` directories: Backups created during app updates. If the corresponding
///   app directory does not exist, the backup is restored. If the app exists, the
///   backup is removed as it's no longer needed.
pub fn recoverIncompleteInstalls(allocator: std.mem.Allocator) void {
    if (!fs.isMounted()) return;

    logInfo("install: recover incomplete installs dir={s}", .{paths.apps_dir_z});

    var d = fs.Dir.open(paths.apps_dir_z) catch return;
    defer d.close() catch {};

    var name_buf: [96]u8 = undefined;
    while (true) {
        const n_opt = d.readName(name_buf[0..]) catch break;
        if (n_opt == null) break;
        const n = n_opt.?;
        const name = name_buf[0..n];

        if (std.mem.startsWith(u8, name, ".staging-")) {
            var full_buf: [160]u8 = undefined;
            const full_z = std.fmt.bufPrintZ(&full_buf, "{s}/{s}", .{ paths.apps_dir_z, name }) catch continue;
            logInfo("install: removing leftover staging dir path={s}", .{full_z});
            rmRf(allocator, full_z) catch {};
            continue;
        }

        if (std.mem.startsWith(u8, name, ".backup-")) {
            const id = name[".backup-".len..];
            manifest_mod.validateId(id) catch {
                var full_buf: [160]u8 = undefined;
                const full_z = std.fmt.bufPrintZ(&full_buf, "{s}/{s}", .{ paths.apps_dir_z, name }) catch continue;
                logInfo("install: removing invalid backup dir name={s} path={s}", .{ name, full_z });
                rmRf(allocator, full_z) catch {};
                continue;
            };

            var app_buf: [96]u8 = undefined;
            var backup_buf: [96]u8 = undefined;
            const appRootZ = paths.appRootZ(&app_buf, id);
            const backupRootZ = paths.backupRootZ(&backup_buf, id);

            const app_exists = fs.metadata(appRootZ) catch |err| switch (err) {
                Error.NotFound => null,
                else => null,
            };
            if (app_exists == null) {
                logInfo("install: restoring backup id={s} from={s} to={s}", .{ id, backupRootZ, appRootZ });
                fs.rename(backupRootZ, appRootZ) catch {};
            } else {
                logInfo("install: removing leftover backup (app exists) id={s} path={s}", .{ id, backupRootZ });
                rmRf(allocator, backupRootZ) catch {};
            }
        }
    }
}

pub fn installOne(allocator: std.mem.Allocator, catalog: *catalog_mod.Catalog, papp_path_z: [:0]const u8) !bool {
    logInfo("install: path={s}", .{papp_path_z});
    const path: []const u8 = papp_path_z;
    const base = std.fs.path.basename(path);
    if (base.len != 0 and base[0] == '.') {
        logInfo("install: ignoring dotfile path={s}", .{papp_path_z});
        return false;
    }
    if (!fs.isMounted()) {
        logInfo("install: fs not mounted; skipping path={s}", .{papp_path_z});
        return false;
    }

    const papp_meta = fs.metadata(papp_path_z) catch null;
    if (papp_meta) |m| {
        logInfo("install: begin path={s} size={d}", .{ papp_path_z, m.size });
    } else {
        logInfo("install: begin path={s}", .{papp_path_z});
    }

    catalog_mod.ensureBaseDirs() catch {};
    recoverIncompleteInstalls(allocator);
    var pkg = fs.File.open(papp_path_z, fs.FS_READ) catch |err| switch (err) {
        Error.NotFound => return false,
        else => return err,
    };
    defer pkg.close() catch {};

    logInfo("install: reading manifest.json from package path={s}", .{papp_path_z});
    const manifest_bytes = zip_reader.firstPassFindManifest(&pkg, allocator) catch |err| {
        var buf: [256]u8 = undefined;
        const msg = std.fmt.bufPrintZ(
            &buf,
            "install: manifest.json read failed ({s}): {s}",
            .{ @errorName(err), papp_path_z },
        ) catch "install: manifest.json read failed";
        core.log.warn(msg);
        return false;
    };

    logInfo("install: manifest.json read ok bytes={d} path={s}", .{ manifest_bytes.len, papp_path_z });
    var m = manifest_mod.parse(allocator, manifest_bytes) catch |err| {
        allocator.free(manifest_bytes);
        var buf: [256]u8 = undefined;
        const msg = std.fmt.bufPrintZ(
            &buf,
            "install: invalid manifest.json ({s}): {s}",
            .{ @errorName(err), papp_path_z },
        ) catch "install: invalid manifest.json";
        core.log.warn(msg);
        return false;
    };
    defer m.deinit(allocator);

    logInfo(
        "install: parsed manifest id={s} name={s} version={s} checksum={s} signed={s}",
        .{
            m.id,
            m.name,
            m.version,
            m.checksum,
            if (m.signature_b64 != null) "yes" else "no",
        },
    );

    const existing = findCatalogEntry(catalog, m.id);
    const pinned_existing = if (existing) |e| e.pinned_publisher_pubkey_b64 else null;

    logInfo(
        "install: catalog lookup id={s} existing={s} pinned={s}",
        .{
            m.id,
            if (existing != null) "yes" else "no",
            if (pinned_existing != null) "yes" else "no",
        },
    );

    var new_pin: ?[]u8 = null;
    logInfo("install: verifying signature/pinning id={s}", .{m.id});
    new_pin = signing.verifyAndMaybePin(allocator, &m, pinned_existing) catch |err| {
        var buf: [256]u8 = undefined;
        const msg = std.fmt.bufPrintZ(
            &buf,
            "install: signature/pinning failed ({s}) id={s}",
            .{ @errorName(err), m.id },
        ) catch "install: signature/pinning failed";
        core.log.warn(msg);
        return false;
    };
    errdefer if (new_pin) |s| allocator.free(s);

    logInfo(
        "install: signature/pinning ok id={s} new_pin={s}",
        .{ m.id, if (new_pin != null) "yes" else "no" },
    );

    var staging_buf: [96]u8 = undefined;
    var backup_buf: [96]u8 = undefined;
    var app_buf: [96]u8 = undefined;
    const staging_z = paths.stagingRootZ(&staging_buf, m.id);
    const backup_z = paths.backupRootZ(&backup_buf, m.id);
    const appRootZ = paths.appRootZ(&app_buf, m.id);

    logInfo(
        "install: paths id={s} staging={s} backup={s} app_root={s}",
        .{ m.id, staging_z, backup_z, appRootZ },
    );

    logInfo("install: clearing old staging/backup id={s}", .{m.id});
    rmRf(allocator, staging_z) catch {};
    rmRf(allocator, backup_z) catch {};
    logInfo("install: creating staging dir id={s} path={s}", .{ m.id, staging_z });
    fs.Dir.mkdir(staging_z) catch {};

    logInfo("install: extracting package to staging id={s}", .{m.id});
    const extract = zip_reader.extractAll(allocator, &pkg, staging_z) catch |err| {
        var buf: [256]u8 = undefined;
        const msg = std.fmt.bufPrintZ(
            &buf,
            "install: extraction failed ({s}) id={s}",
            .{ @errorName(err), m.id },
        ) catch "install: extraction failed";
        core.log.warn(msg);
        rmRf(allocator, staging_z) catch {};
        return false;
    };
    {
        const got_hex = std.fmt.bytesToHex(extract.wasm_sha256, .lower);
        logInfo("install: extraction ok id={s} wasm_sha256=sha256:{s}", .{ m.id, got_hex[0..] });
    }

    if (!checksumMatches(m.checksum, extract.wasm_sha256)) {
        const got_hex = std.fmt.bytesToHex(extract.wasm_sha256, .lower);
        var buf: [320]u8 = undefined;
        const msg = std.fmt.bufPrintZ(
            &buf,
            "install: checksum mismatch id={s} expected={s} got=sha256:{s}",
            .{ m.id, m.checksum, got_hex[0..] },
        ) catch "install: checksum mismatch";
        core.log.warn(msg);
        rmRf(allocator, staging_z) catch {};
        return false;
    }
    logInfo("install: checksum ok id={s}", .{m.id});

    // Move existing app aside (best-effort).
    logInfo("install: backing up existing app (if present) id={s}", .{m.id});
    fs.rename(appRootZ, backup_z) catch |err| switch (err) {
        Error.NotFound => {},
        else => {
            var buf: [256]u8 = undefined;
            const msg = std.fmt.bufPrintZ(
                &buf,
                "install: failed to backup existing app ({s}) id={s}",
                .{ @errorName(err), m.id },
            ) catch "install: failed to backup existing app";
            core.log.warn(msg);
            rmRf(allocator, staging_z) catch {};
            return false;
        },
    };

    logInfo("install: activating staging dir id={s}", .{m.id});
    fs.rename(staging_z, appRootZ) catch |err| {
        var buf: [256]u8 = undefined;
        const msg = std.fmt.bufPrintZ(
            &buf,
            "install: failed to activate staging dir ({s}) id={s}",
            .{ @errorName(err), m.id },
        ) catch "install: failed to activate staging dir";
        core.log.warn(msg);
        fs.rename(backup_z, appRootZ) catch {};
        rmRf(allocator, staging_z) catch {};
        return false;
    };

    logInfo("install: removing backup dir id={s}", .{m.id});
    rmRf(allocator, backup_z) catch {};

    // Upsert catalog entry, taking ownership of id/name/raw manifest JSON.
    const id_for_log = m.id;
    const raw_manifest = m.disownRawJson();

    if (existing) |e| {
        logInfo("install: updating existing catalog entry id={s}", .{id_for_log});
        allocator.free(e.name);
        allocator.free(e.manifest_json);

        e.name = m.name;
        e.manifest_json = raw_manifest;
        if (new_pin) |s| {
            if (e.pinned_publisher_pubkey_b64) |old| allocator.free(old);
            e.pinned_publisher_pubkey_b64 = s;
            new_pin = null;
            logInfo("install: updated pinned publisher key id={s}", .{id_for_log});
        }

        m.name = &[_]u8{};
    } else {
        logInfo("install: adding new catalog entry id={s}", .{id_for_log});
        try catalog.apps.append(allocator, .{
            .id = m.id,
            .name = m.name,
            .manifest_json = raw_manifest,
            .pinned_publisher_pubkey_b64 = if (new_pin) |s| s else null,
        });
        m.id = &[_]u8{};
        m.name = &[_]u8{};
        if (new_pin != null) new_pin = null;
    }

    logInfo("install: success id={s}", .{id_for_log});
    return true;
}
