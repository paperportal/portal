const std = @import("std");
const sdk = @import("paper_portal_sdk");

pub fn build(b: *std.Build) void {
    _ = sdk.addPortalApp(b, .{
        .local_sdk_path = "../../../zig-sdk",
        .export_symbol_names = &.{ "ppInit", "ppShutdown", "ppOnGesture" },
    });
}
