# Web configuration utility specification

This file specifies the web-based configuration utility that allows configuring the Paper Portal launcher from a browser.

## Overview

The web configuration utility is a web server implemented fully in WASM using the SDK's networking APIs. It provides a browser-based interface for configuring the device and managing applications.

## Starting the web config

- From settings view, user can tap "Web Config" to start the web server
- When active, the screen displays:
  - A QR code containing the HTTP server URL
  - The device's IP address and port
  - Instructions to connect from a browser
- A "Stop" button is provided to shut down the server
- The launcher UI is blocked while the web server is running (no background execution needed)

## Web interface features

The web configuration utility provides:

- All settings from the on-device settings menu (brightness, date/time, etc.)
- App management (install/uninstall/reorder apps)
- File browser for SD card contents
- System logs and diagnostics

## Technical details

| Property | Value |
|----------|-------|
| Server implementation | WASM code using SDK networking APIs |
| Protocol | HTTP |
| Port | Configurable, default 8080 |
| Security | Local network only (no internet exposure) |
| Concurrency | Server runs while launcher displays connection info; launcher blocked until server stopped |

## SDK APIs required

The SDK must provide networking APIs sufficient for implementing a basic HTTP server in WASM:

```zig
pub const net = struct {
    // Create a TCP socket
    fn socket() Error!Socket;

    // Bind socket to address and port
    fn bind(socket: Socket, addr: []const u8, port: u16) Error!void;

    // Start listening for connections
    fn listen(socket: Socket, backlog: i32) Error!void;

    // Accept incoming connection
    fn accept(socket: Socket) Error!Connection;

    // Receive data from connection
    fn recv(connection: Connection, buffer: []u8) Error!usize;

    // Send data to connection
    fn send(connection: Connection, data: []const u8) Error!usize;

    // Close connection or socket
    fn close(handle: SocketOrConnection) Error!void;
};

// Get device's IP address
pub fn net_get_ip_addr(buffer: []u8) Error![]const u8;
```

## HTTP server implementation (WASM)

The launcher implements a minimal HTTP server with the following characteristics:

- Single-threaded, event-driven architecture
- Serves static HTML/CSS/JS assets (embedded in WASM)
- RESTful API endpoints for settings and file operations
- Multipart form upload support for app installation
- Basic authentication (optional, for public networks)

### Example endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | Serve web interface HTML |
| `/api/settings` | GET | Get all settings |
| `/api/settings` | POST | Update settings |
| `/api/apps` | GET | List installed apps |
| `/api/apps` | POST | Upload/install new app |
| `/api/apps/:id` | DELETE | Uninstall app |
| `/api/files` | GET | Browse SD card files |
| `/api/logs` | GET | Get system logs |

## QR code generation

The QR code displayed on screen contains the HTTP server URL (e.g., `http://192.168.1.100:8080`).

The SDK must provide a QR code generation API:

```zig
// Generate QR code for the given text
// Returns a 2D array of booleans (true = black, false = white)
pub const qr = struct {
    fn encode(text: []const u8) Error![][]bool;
};
```

The launcher renders the QR code using the display APIs (`display.draw_pixel` or `display.fill_rect`).

## Error handling

| Error | Handling |
|-------|----------|
| Network not available | Display error message, don't start server |
| Port already in use | Display error, offer to use different port |
| Client disconnect | Clean connection, continue serving |
| Invalid request | Return 400 Bad Request |
| Server crash | Log error, return to settings view |
