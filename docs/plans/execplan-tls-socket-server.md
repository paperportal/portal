# Add TLS-protected raw socket server support (WASM API + Zig SDK)

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and `Outcomes & Retrospective` must be kept up to date as work proceeds.

This repository does not currently include a `PLANS.md` file. Follow the “ExecPlans” skill requirements directly when updating this document.

## Purpose / Big Picture

After this change, a Zig/WASM app running on Paper Portal can accept inbound TCP connections that are protected by TLS (Transport Layer Security). “Protected by TLS” means the client and server perform a TLS handshake using an X.509 certificate and private key, and all subsequent bytes read/written are encrypted and authenticated.

The user-visible outcome is an example WASM app that starts a TLS echo server on a chosen port. From a laptop on the same network, running an OpenSSL client against the device’s IP and port completes a TLS handshake and receives echoed plaintext, demonstrating that the traffic is traveling over TLS and not raw TCP.

## Progress

- [x] (2026-02-26 22:58Z) Implement host-side TLS socket WASM API in `portal` (`portal_socket_tls`).
- [x] (2026-02-26 22:58Z) Export the new API in the Zig SDK (`zig-sdk`) as a thin wrapper (`sdk.tls_socket`).
- [x] (2026-02-26 22:58Z) Add an example Zig/WASM app that uses the new API (TLS echo server) at `portal/apps/tls-echo-server`.
- [ ] (2026-02-26 22:58Z) Validate on device: handshake + echo using OpenSSL client.
- [x] (2026-02-26 22:58Z) Update docs/specs for the new API surface (`portal/docs/specs/spec-socket-tls.md`).

## Surprises & Discoveries

- Observation: `esp_tls_server_session_delete()` frees TLS state but does not close the underlying TCP socket fd; the host must close it explicitly.
  Evidence: ESP-IDF `esp_tls_mbedtls.c` deletes the session via `esp_mbedtls_cleanup(tls)` + `free(tls)` without calling `close(tls->sockfd)`.

- Observation: lwIP treats `SO_RCVTIMEO/SO_SNDTIMEO` of `0` as “no timeout” (wait forever), so “poll” APIs must not rely on `0` as “non-blocking”.
  Evidence: lwIP netconn `recv_timeout` defaults to `0` and is passed directly to `sys_arch_mbox_fetch(..., timeout)`.

- Observation: A strictly non-blocking server-side TLS handshake is not feasible using `esp_tls_server_session_create()` alone because it performs the handshake loop internally using socket I/O.
  Evidence: `esp_tls_server_session_create()` returns only after handshake success/failure; there is no async/step API for server sessions in `esp-tls`.

## Decision Log

- Decision: Expose TLS as a separate handle type (not reusing raw socket fds).
  Rationale: A TLS session needs additional state (handshake, mbedTLS context) and must control the lifecycle of the underlying TCP socket. Treating it as a distinct handle keeps the Zig SDK thin and avoids confusing “fd vs TLS session” behavior.
  Date/Author: 2026-02-26 / Codex

- Decision: Provide a combined “accept + handshake” API (`tlsAccept`) instead of “accept raw then wrap”.
  Rationale: Keeps app code simpler and reduces the chance a caller accidentally reads/writes plaintext over the accepted TCP fd before upgrading to TLS.
  Date/Author: 2026-02-26 / Codex

- Decision: `tlsRecv()` returns `0` bytes on an orderly close (like BSD `recv()`), rather than mapping clean close to `kWasmErrNotReady`.
  Rationale: Allows simple “`n == 0` means EOF/closed” loops without consulting `lastErrorMessage`, and matches `portal_socket.sockRecv` behavior on FIN/close-notify.
  Date/Author: 2026-02-26 / Codex

- Decision: Clamp `tlsAccept(timeout_ms == 0)` to use a small default handshake socket timeout once a client is accepted.
  Rationale: Makes poll-based servers usable while still bounding worst-case stall; lwIP uses `0ms` timeouts to mean “no timeout”, not “non-blocking”.
  Date/Author: 2026-02-26 / Codex

- Decision: Close all TLS configs/sessions on app teardown via `clear_app_runtime_state()`.
  Rationale: Prevent leaked TLS handles from persisting across app switches/reloads.
  Date/Author: 2026-02-26 / Codex

## Outcomes & Retrospective

- (2026-02-26 22:58Z) Added `portal_socket_tls` host API + `sdk.tls_socket` wrappers + a `tls-echo-server` example app runnable via the devserver; remaining work is on-device validation and capturing the acceptance artifacts.

## Context and Orientation

### What exists today

Paper Portal exposes a WASM host API surface to apps via native symbol registration in `portal/main/wasm/api/*.cpp`.

Raw sockets are currently supported via:

    /Users/mika/code/paperportal/portal/main/wasm/api/socket.cpp

That module registers natives under the name `portal_socket` and implements basic BSD socket operations (`sockSocket`, `sockBind`, `sockListen`, `sockAcceptWithTimeout`, `sockSend`, `sockRecv`, `sockClose`) backed by lwIP (`lwip/sockets.h`).

The Zig SDK wraps those host calls via:

    /Users/mika/code/paperportal/zig-sdk/sdk/socket.zig
    /Users/mika/code/paperportal/zig-sdk/sdk/ffi.zig

The Zig SDK is a thin layer over host FFI and should remain so.

### What is missing

There is no host API to:

- load a TLS server certificate/key for an app,
- perform a TLS handshake for an accepted client connection, or
- read/write application bytes through a TLS session.

### Definitions used in this plan

- TLS: a protocol layered on top of TCP that encrypts and authenticates the byte stream.
- X.509 certificate: public certificate presented by the server during the TLS handshake.
- Private key: secret key corresponding to the server certificate; used to prove server identity.
- PEM: a common ASCII text encoding for certificates/keys (the typical “-----BEGIN …-----” format).

## Plan of Work

### 1) Host-side API design (portal)

Add a new WASM native module named `portal_socket_tls` implemented in a new file:

    /Users/mika/code/paperportal/portal/main/wasm/api/socket_tls.cpp

This module will create and manage server-side TLS sessions using ESP-IDF’s `esp-tls` component (which wraps mbedTLS). The API will be handle-based:

- “TLS config handle”: holds immutable configuration for a server (certificate chain, private key, and optional client-CA bundle for mutual TLS).
- “TLS session handle”: holds the per-connection state (handshake state, negotiated session, and the underlying TCP socket).

The key behavior: `tlsAccept` blocks (or timeouts) waiting for a TCP client, then performs the TLS handshake, and returns a TLS session handle if successful.

#### Proposed WASM native functions

All functions return `kWasmOk (0)` on success when the return type is an error code, or a positive value (e.g. bytes read/written) when that is the natural return. On failure they return a negative `wasm_error_code` and set `lastErrorMessage` to a helpful string.

1) Create a TLS server configuration:

    int32_t tlsServerConfigCreate(
        wasm_exec_env_t exec_env,
        const uint8_t *server_cert_pem_ptr,  size_t server_cert_pem_len,
        const uint8_t *server_key_pem_ptr,   size_t server_key_pem_len,
        const uint8_t *client_ca_pem_ptr,    size_t client_ca_pem_len,
        int32_t flags);

Returns a positive config handle on success.

Notes:
- `server_cert_pem_*` and `server_key_pem_*` are required. The implementation must copy these buffers into host-owned memory so the WASM caller can free/reuse its memory after the call returns.
- `client_ca_pem_*` may be null/0 to disable mutual TLS (client certificates). If present, require/verify client certs.
- `flags` initially supports only:
  - bit 0: require client certificate (mutual TLS). If set, `client_ca_pem_*` must be provided.

2) Free a TLS server configuration:

    int32_t tlsServerConfigFree(wasm_exec_env_t exec_env, int32_t config_handle);

3) Accept a new TLS client connection (accept + handshake):

    int32_t tlsAccept(
        wasm_exec_env_t exec_env,
        int32_t config_handle,
        int32_t listen_sockfd,
        uint8_t *out_addr_ptr, int32_t out_addr_len,
        int32_t timeout_ms);

Returns a positive TLS session handle on success.

Notes:
- `listen_sockfd` is created by existing `portal_socket` functions (`sockSocket`/`sockBind`/`sockListen`).
- `timeout_ms` matches the existing socket API semantics:
  - `timeout_ms == 0`: non-blocking poll (return `kWasmErrNotReady` if no client is pending).
  - `timeout_ms > 0`: wait up to that duration.
  - `timeout_ms < 0`: block until a client arrives.
- The implementation should:
  - wait for readability on `listen_sockfd` using the existing `wait_socket_ready` helper logic from `socket.cpp` (copy it into `socket_tls.cpp` to avoid cross-file linkage),
  - call `lwip_accept`,
  - set `SO_RCVTIMEO`/`SO_SNDTIMEO` on the accepted client socket based on `timeout_ms` (for handshake/read/write timeouts),
  - create the TLS session and perform handshake using `esp_tls_server_session_create` (or the async variant if needed; start with synchronous for simplicity),
  - on failure, close the accepted TCP socket and return an error.

4) Read plaintext bytes from a TLS session:

    int32_t tlsRecv(
        wasm_exec_env_t exec_env,
        int32_t tls_handle,
        uint8_t *buf_ptr, int32_t buf_len,
        int32_t timeout_ms);

Returns number of bytes read (`>= 0`), or a negative wasm error code.

5) Write plaintext bytes to a TLS session:

    int32_t tlsSend(
        wasm_exec_env_t exec_env,
        int32_t tls_handle,
        const uint8_t *buf_ptr, int32_t buf_len,
        int32_t timeout_ms);

Returns number of bytes written (`>= 0`), or a negative wasm error code.

6) Close a TLS session:

    int32_t tlsClose(wasm_exec_env_t exec_env, int32_t tls_handle);

This must:
- call the appropriate esp-tls teardown (`esp_tls_server_session_delete` or `esp_tls_conn_destroy` depending on how the session was created),
- close the underlying lwIP socket if esp-tls does not do it,
- remove the handle from the handle table,
- be idempotent (closing twice returns `kWasmOk`).

#### Error mapping rules

For consistency with existing socket behavior:

- “would block / no data / timeout” should be reported as `kWasmErrNotReady` with an explanatory message.
- “connection reset/abrupt close” should map to `kWasmErrNotReady` with message “closed”.
- “orderly close” should return `0` bytes from `tlsRecv` (EOF).
- unexpected failures map to `kWasmErrInternal`.

During implementation, determine the precise esp-tls return codes and how they surface (e.g., `ESP_TLS_ERR_SSL_WANT_READ`/`…_WANT_WRITE`, mbedTLS error codes, or `errno`). Record this in `Surprises & Discoveries` with evidence (logs).

#### Handle storage and lifetime

Implement small handle tables inside `socket_tls.cpp`:

- Use a `std::mutex` to protect access.
- Use a monotonically increasing `int32_t` for handle IDs.
- On create, allocate a struct that owns:
  - copied PEM buffers (for config handles),
  - `esp_tls_t *` (for session handles),
  - the underlying `int client_sockfd` (for session handles, if needed).
- On free/close, ensure memory is released.

This module must not leak handles across app switches. If the portal runtime has an “app teardown” hook for closing resources, add a “close all TLS sessions/configs” call and invoke it on app exit. If there is no existing hook, add a TODO note and at least ensure normal closure is correct; capture what you found in `Surprises & Discoveries`.

Implementation note (what we found): `clear_app_runtime_state()` in `/Users/mika/code/paperportal/portal/main/host/event_loop.cpp` is called on app unload/switch and is a suitable teardown hook; we call `wasm_api_socket_tls_close_all()` there.

### 2) Wire the API into portal registration and feature flags

1) Add declaration in:

    /Users/mika/code/paperportal/portal/main/wasm/api.h

Add:

    bool wasm_api_register_socket_tls(void);

2) Update registration in:

    /Users/mika/code/paperportal/portal/main/wasm/api/core.cpp

- Add a new feature bit in:

    /Users/mika/code/paperportal/portal/main/wasm/api/features.h

For example:

    kWasmFeatureSocketTls = 1ULL << 20,

Then OR it into `kApiFeatures` in `core.cpp`.

- Call `wasm_api_register_socket_tls()` from `wasm_api_register_all()` (after `wasm_api_register_socket()` is fine).

3) Add the new source file to the build:

    /Users/mika/code/paperportal/portal/main/CMakeLists.txt

- Add `wasm/api/socket_tls.cpp` to `SRCS`.
- Add `esp-tls` (ESP-IDF component name is `esp-tls`) to `PRIV_REQUIRES` so the build links the esp-tls library.

### 3) Zig SDK surface (zig-sdk)

Update the Zig SDK to expose the new host calls while staying a thin wrapper.

1) Add FFI declarations in:

    /Users/mika/code/paperportal/zig-sdk/sdk/ffi.zig

Add a new section similar to the existing socket section:

- `pub extern "portal_socket_tls" fn tlsServerConfigCreate(...) i32;`
- `pub extern "portal_socket_tls" fn tlsServerConfigFree(handle: i32) i32;`
- `pub extern "portal_socket_tls" fn tlsAccept(...) i32;`
- `pub extern "portal_socket_tls" fn tlsSend(...) i32;`
- `pub extern "portal_socket_tls" fn tlsRecv(...) i32;`
- `pub extern "portal_socket_tls" fn tlsClose(handle: i32) i32;`

2) Add a Zig wrapper module:

    /Users/mika/code/paperportal/zig-sdk/sdk/tls_socket.zig

Expose:

- `pub const ServerConfig = struct { handle: i32, ... }` with `create(...)`, `deinit()`.
- `pub const TlsSocket = struct { handle: i32, ... }` with `recv(...)`, `send(...)`, `close()`.
- `pub fn accept(config: *ServerConfig, listen: *socket.Socket, ...) Error!TlsSocket` that calls `ffi.tlsAccept`.

Keep it consistent with existing patterns in:

    /Users/mika/code/paperportal/zig-sdk/sdk/socket.zig

3) Re-export in the SDK entrypoint:

    /Users/mika/code/paperportal/zig-sdk/sdk.zig

Add:

    pub const tls_socket = @import("sdk/tls_socket.zig");

### 4) Example app: TLS echo server

Add a small Zig/WASM app under `portal/apps/` (or in an existing example app location if there is a preferred pattern). The app should:

- connect to Wi-Fi (using existing `sdk.net.connect()` flow if needed),
- print the device IPv4 address and chosen port,
- create a raw listening socket with `sdk.socket.Socket.tcp()` + `bind(any(port))` + `listen(backlog)`,
- create a TLS server config by passing embedded PEM certificate and key bytes,
- loop:
  - `tlsAccept` with a timeout (so the app can remain responsive),
  - on accept, read from TLS and echo back until the client closes.

Keep logs simple and visible so a human can verify the handshake and data flow.

Certificate/key approach for the example:

- Generate a self-signed certificate and key locally and paste the PEM into the Zig source as multiline strings (byte slices).
- The plan must include the exact OpenSSL commands used and what files they produce (see “Concrete Steps”).

### 5) Documentation

Update or add a short API description in `portal/docs/specs/` if this is a new user-facing capability. At minimum, document:

- the module name `portal_socket_tls`,
- the function list and how to use them from Zig,
- the meaning of `timeout_ms`,
- the memory ownership rules (PEM copied into host at config creation),
- security notes (self-signed cert is for development; production provisioning is separate).

## Concrete Steps

All commands below are meant to be run from a shell with the relevant working directory as stated.

### A) Implement portal host API

1) Create:

    /Users/mika/code/paperportal/portal/main/wasm/api/socket_tls.cpp

2) Update:

    /Users/mika/code/paperportal/portal/main/wasm/api.h
    /Users/mika/code/paperportal/portal/main/wasm/api/features.h
    /Users/mika/code/paperportal/portal/main/wasm/api/core.cpp
    /Users/mika/code/paperportal/portal/main/CMakeLists.txt

3) Build (requires ESP-IDF environment set up):

    cd /Users/mika/code/paperportal/portal
    idf.py build

Expected outcome: firmware builds with the new `portal_socket_tls` natives registered.

If the build fails due to missing component names, open ESP-IDF’s component list and adjust `PRIV_REQUIRES` to the correct component target (usually `esp-tls`). Record the fix in `Surprises & Discoveries`.

### B) Implement Zig SDK wrapper

1) Update:

    /Users/mika/code/paperportal/zig-sdk/sdk/ffi.zig
    /Users/mika/code/paperportal/zig-sdk/sdk.zig

2) Create:

    /Users/mika/code/paperportal/zig-sdk/sdk/tls_socket.zig

3) Validate:

    cd /Users/mika/code/paperportal/zig-sdk
    ~/zig/zig build
    ~/zig/zig fmt .

Expected outcome: `zig build` succeeds and the SDK exports the new module.

### C) Create and test the example app

1) Generate a self-signed cert and key locally (example values; adjust CN/IP as desired):

    mkdir -p /tmp/paperportal-tls
    cd /tmp/paperportal-tls
    openssl req -x509 -newkey rsa:2048 -nodes \
      -keyout server.key.pem -out server.crt.pem \
      -days 365 -subj "/CN=paperportal"

2) Embed `server.crt.pem` and `server.key.pem` in the Zig app source as byte slices.

3) Build the example app wasm:

    cd /Users/mika/code/paperportal/portal/apps/tls-echo-server
    ~/zig/zig build

4) Build/flash firmware and start the Dev Server on device (exact flashing steps depend on your setup):

    cd /Users/mika/code/paperportal/portal
    idf.py -p /dev/tty.usbserial-XXXX flash monitor

5) Upload and run the wasm via the Dev Server web UI (or via build helper):

    cd /Users/mika/code/paperportal/portal/apps/tls-echo-server
    ~/zig/zig build upload -Dpaperportal-host=<DEVICE_IP_OR_HOSTNAME> -Dpaperportal-port=<DEVSERVER_PORT>

6) From a laptop on the same network, connect with OpenSSL:

    openssl s_client -connect <DEVICE_IP>:8443 -servername paperportal

Then type a line and press enter; expect the server to echo it back.

Record:
- the device log showing accept/handshake/read/write,
- a short OpenSSL transcript showing handshake success,
in `Artifacts and Notes`.

## Validation and Acceptance

This work is complete when all of the following are true:

1) `idf.py build` succeeds for `/Users/mika/code/paperportal/portal`.
2) `zig build` succeeds for `/Users/mika/code/paperportal/zig-sdk` and `zig fmt .` produces no diffs.
3) The example WASM app can be run on device (e.g. via Dev Server upload) and logs:
   - that it is listening,
   - the device IPv4 address and port,
   - a message when a TLS client connects.
4) Running:

    openssl s_client -connect <DEVICE_IP>:<PORT>

completes a TLS handshake and exchanging plaintext results in echoed data (proving the app is using TLS read/write, not raw TCP).

## Idempotence and Recovery

- Building the portal firmware and Zig SDK is repeatable and should be safe to rerun.
- If a TLS config/session handle is leaked in the WASM app, the portal host should still behave correctly (the leak is bounded by the handle table). During implementation, add defensive “close all handles” behavior on app teardown if a suitable hook exists.
- If the TLS handshake fails, the accepted TCP socket must be closed before returning to avoid exhausting file descriptors.

## Artifacts and Notes

During implementation, paste minimal evidence here:

- (2026-02-26 22:58Z) Zig SDK build:

    cd /Users/mika/code/paperportal/zig-sdk
    ~/zig/zig build

- (2026-02-26 22:58Z) Example app build:

    cd /Users/mika/code/paperportal/portal/apps/tls-echo-server
    ~/zig/zig build

- `idf.py build` success snippet (last ~10 lines).
- `openssl s_client` transcript (first ~10 lines including “Verify return code:”).
- Device logs around a successful handshake and one echo exchange.

- (2026-02-26 22:58Z) Note: `idf.py` could not be run in this sandbox because the ESP-IDF Python environment is missing (`idf5.3_py3.9_env/bin/python` not found; importing `click` fails without the venv). Run `idf_tools.py install-python-env` (or the ESP-IDF install script) on your host machine to enable `idf.py build`.

## Interfaces and Dependencies

### portal (C++ / ESP-IDF)

Primary dependency: ESP-IDF `esp-tls` component (mbedTLS-based).

The implementation should be based on these core concepts (describe them in-code and/or in this doc during implementation):

- A TLS “server config” that includes the certificate and private key.
- A TLS “session” created per accepted TCP connection.
- TLS I/O that reads/writes plaintext while esp-tls handles encryption over the underlying socket.

### zig-sdk (Zig)

The Zig SDK must remain a thin wrapper:

- It should not attempt to implement TLS in WASM.
- It should define small structs that own integer handles and delegate to `sdk/ffi.zig` externs.

### Change note (why this plan changed)

- (2026-02-26 22:58Z) Updated `tlsRecv` semantics to return `0` on orderly close (EOF) to match BSD sockets and avoid requiring apps to consult `lastErrorMessage` to detect clean shutdown.
- (2026-02-26 22:58Z) Documented/implemented a small default handshake timeout for `tlsAccept(timeout_ms == 0)` to keep poll-based servers usable with synchronous `esp_tls_server_session_create()`.
- (2026-02-26 22:58Z) Updated the example app workflow to run via the Dev Server upload flow (not embedded as a firmware asset).
