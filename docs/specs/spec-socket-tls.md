# TLS socket server API specification

This file specifies the server-side TLS socket capability exposed to Paper Portal WASM apps.

## Overview

- Apps can accept inbound TCP connections and protect the byte stream with TLS (Transport Layer Security).
- TLS is layered on top of an existing listening TCP socket created via `portal_socket` (`sockSocket`/`sockBind`/`sockListen`).

## Feature flag

- The firmware advertises TLS socket support via `apiFeatures()`:
  - `kWasmFeatureSocketTls` (bit `1 << 20`)
- The Zig SDK exposes this feature bit as:
  - `sdk.core.Feature.socket_tls`

Apps should check this bit before using the TLS socket API.

## Host module: `portal_socket_tls`

The host registers a WASM native module named `portal_socket_tls`.

The API is handle-based:
- **TLS server config handle**: immutable TLS configuration (server cert/key and optional client-CA).
- **TLS session handle**: a negotiated TLS connection for a single client.

### `tlsServerConfigCreate`

Creates a TLS server configuration.

- Inputs:
  - `server_cert_pem_ptr/server_cert_pem_len` (required): server certificate in PEM encoding.
  - `server_key_pem_ptr/server_key_pem_len` (required): server private key in PEM encoding.
  - `client_ca_pem_ptr/client_ca_pem_len` (optional): client CA bundle (PEM) for mutual TLS.
  - `flags`:
    - bit 0: require/verify client certificates (mutual TLS). When set, `client_ca_pem_*` is required.
- Output:
  - Returns a positive config handle on success.

Memory ownership:
- The host copies the PEM buffers into host-owned memory during this call.
- The caller may free/reuse its buffers after the call returns.

### `tlsServerConfigFree`

Frees a TLS server config handle.

### `tlsAccept`

Accepts a pending TCP connection from `listen_sockfd`, performs the TLS handshake, and returns a TLS session handle.

- Inputs:
  - `config_handle`: TLS server config handle created by `tlsServerConfigCreate`.
  - `listen_sockfd`: a TCP listening socket fd created via `portal_socket`.
  - `out_addr_ptr/out_addr_len`: optional output buffer for peer address. If provided, it uses the same `SocketAddr` format as `portal_socket.sockAcceptWithTimeout`.
  - `timeout_ms`:
    - `timeout_ms == 0`: non-blocking poll for a pending client.
    - `timeout_ms > 0`: wait up to that duration for a pending client.
    - `timeout_ms < 0`: block until a client arrives.

Notes:
- This API combines accept + TLS handshake to avoid accidental plaintext I/O on the accepted TCP socket.
- When `timeout_ms == 0`, the accept portion is a non-blocking poll, but the handshake may still take a small bounded amount of time to complete.

### `tlsRecv`

Reads plaintext bytes from a TLS session.

- Returns `> 0` bytes read on success.
- Returns `0` when the peer cleanly closes the TLS connection.
- Returns a negative `kWasmErr*` on failure.

`timeout_ms` uses the same semantics as `tlsAccept`.

### `tlsSend`

Writes plaintext bytes to a TLS session.

- Returns `>= 0` bytes written on success.
- Returns a negative `kWasmErr*` on failure.

`timeout_ms` uses the same semantics as `tlsAccept`.

### `tlsClose`

Closes a TLS session handle and releases host resources.

- Must close the underlying TCP socket.
- Is idempotent (closing the same handle twice returns `kWasmOk`).

## Error handling

- On success, functions return `kWasmOk (0)` where that is the natural return type, or a positive value (handle or bytes).
- On failure, functions return a negative `kWasmErr*` and set the global last-error via `lastErrorMessage`.
- Transient I/O conditions (no data yet / would-block / timeout) use `kWasmErrNotReady`.

## Zig SDK usage

The Zig SDK wraps these natives in `sdk.tls_socket`:

- `tls_socket.ServerConfig.create(...)` / `deinit()`
- `tls_socket.accept(...)`
- `tls_socket.TlsSocket.recv(...)` / `send(...)` / `close()`

