const std = @import("std");
const sdk = @import("paper_portal_sdk");

pub fn build(b: *std.Build) void {
    const app = sdk.addPortalApp(b, .{
        .export_symbol_names = &.{ "ppInit", "ppTick", "ppShutdown", "ppOnGesture" },
    });

    const install_step = b.addInstallFile(app.exe.getEmittedBin(), "../../../main/assets/entrypoint.wasm");
    b.getInstallStep().dependOn(&install_step.step);
}
