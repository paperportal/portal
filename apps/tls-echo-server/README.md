# TLS Echo Server (example app)

This Zig/WASM example starts a TLS-protected echo server on port `8443`.

## Build

From `/Users/mika/code/paperportal/portal/apps/tls-echo-server`:

```sh
~/zig/zig build
```

The output is installed to `zig-out/bin/tls_echo_server.wasm`.

## Run (devserver)

1) On the device, enable Developer Mode and start the Dev Server.
2) Upload and run `zig-out/bin/tls_echo_server.wasm`.
3) From your laptop:

```sh
openssl s_client -connect <DEVICE_IP>:8443 -servername paperportal
```

Type a line and press enter; the server echoes it back.

Note: This app embeds a self-signed certificate/private key for development only.
