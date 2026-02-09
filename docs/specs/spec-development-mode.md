# Development mode specification

This file specifies the developer mode functionality.

## Overview

Developer mode allows developers to quickly test WebAssembly applications on the device without needing to package them or transfer them via SD card. Apps are uploaded over WiFi via HTTP and executed immediately.

## Enabling developer mode

1. Developer mode is disabled by default for security reasons
2. From settings view, user can enable "Developer Mode"
3. When enabled, a new "Dev Server" option appears in settings
4. The developer mode setting persists in NVS

## Starting the development server

From settings view (when developer mode is enabled):
- Tap "Dev Server" to start the development HTTP server
- Startup is asynchronous: launcher UI remains responsive while server networking/HTTP setup runs in background
- Screen displays:
  - QR code with server URL
  - Device IP address and port
  - Upload instructions
- A "Stop" button shuts down the server

## Upload and run workflow

1. Developer connects browser to the displayed URL
2. Web interface shows:
   - Form to select a `.wasm` file
   - Optional: arguments/memory parameters
   - "Run" button
3. Upon upload:
   - WASM file is transferred to device
   - Launcher validates the WASM module
   - App is executed in a sandboxed environment
   - App output (stdout/stderr/logs) is streamed back to the web interface
4. App runs until:
   - User clicks "Stop" in the web interface
   - App exits naturally
   - App crashes (error displayed)

## Technical details

| Property | Value |
|----------|-------|
| Server implementation | Native code in paperportal-launcher (C/C++) |
| Protocol | HTTP |
| Port | Configurable, default 80 |
| Background capability | Server runs while app is being executed |
| App isolation | Apps run in sandboxed WASM runtime |

## SDK APIs for development server

The SDK must provide APIs for controlling the development server from launcher WASM:

```zig
// Development server control
pub const devserver = struct {
    // Start the development server asynchronously
    fn start() Error!void;

    // Stop the development server
    fn stop() Error!void;

    // Check if development server is running
    fn is_running() bool;

    // Check if server startup is in progress
    fn is_starting() bool;

    // Get the server URL
    fn get_url(buffer: []u8) Error![]const u8;

    // Get the last startup/runtime server error text
    fn get_last_error(buffer: []u8) Error![]const u8;
};
```

## Security considerations

- Developer mode should be clearly indicated in the UI (e.g., warning badge)
- Development server only listens on local network (WLAN)
- No authentication required (trusted network assumption)
- Apps run with same permissions as normal apps
- Consider adding optional password protection for public networks

## Error handling

| Error | Handling |
|-------|----------|
| Invalid WASM file | Display error in web interface, don't execute |
| Out of memory | Display error, suggest memory parameters |
| App crash | Display crash reason in web interface |
| Network disconnected | Gracefully stop running app, clean up |
