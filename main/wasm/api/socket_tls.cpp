#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <errno.h>
#include <mutex>

#include "esp_log.h"
#include "esp_tls.h"
#include "esp_tls_errors.h"
#include "lwip/sockets.h"
#include "wasm_export.h"

#include "../api.h"
#include "errors.h"
static constexpr const char *kTag = "wasm_api_socket_tls";

constexpr int32_t kTlsFlagsRequireClientCert = 1 << 0;

constexpr int kMaxTlsServerConfigs = 4;
constexpr int kMaxTlsSessions = 8;

#pragma pack(push, 1)
struct SocketAddr {
    int32_t family;
    uint16_t port;
    uint8_t ip[4];
    uint8_t _pad[2];
};
#pragma pack(pop)

static void sockaddr_to_wasm(const struct sockaddr_in *in, SocketAddr *out)
{
    out->family = in->sin_family;
    out->port = ntohs(in->sin_port);
    memcpy(out->ip, &in->sin_addr.s_addr, 4);
    memset(out->_pad, 0, sizeof(out->_pad));
}

static bool is_would_block_errno(int err)
{
    return err == EWOULDBLOCK || err == EAGAIN;
}

static int32_t wait_socket_ready(int32_t sockfd, bool want_read, int32_t timeout_ms)
{
    if (timeout_ms < 0) {
        return kWasmOk;
    }

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(sockfd, &fds);

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    struct timeval *tv_ptr = &tv;
    int rc = lwip_select(sockfd + 1, want_read ? &fds : nullptr, want_read ? nullptr : &fds, nullptr, tv_ptr);
    if (rc > 0) {
        return kWasmOk;
    }
    if (rc == 0) {
        wasm_api_set_last_error(kWasmErrNotReady, "socket_tls: operation timed out");
        return kWasmErrNotReady;
    }

    wasm_api_set_last_error(kWasmErrInternal, "socket_tls: select failed");
    return kWasmErrInternal;
}

static void apply_socket_timeouts(int sockfd, int32_t timeout_ms)
{
    if (timeout_ms < 0) {
        // 0 disables SO_RCVTIMEO/SO_SNDTIMEO in lwIP (wait forever).
        struct timeval tv = {};
        lwip_setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        lwip_setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        return;
    }

    // lwIP uses 0ms as "no timeout"; use 1ms to approximate "poll" without risking an unbounded block.
    if (timeout_ms == 0) {
        timeout_ms = 1;
    }

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    lwip_setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    lwip_setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

struct PemBuffer {
    uint8_t *data = nullptr;
    size_t len = 0; // includes NUL terminator
};

static void pem_free(PemBuffer *buf)
{
    if (!buf) {
        return;
    }
    if (buf->data) {
        free(buf->data);
        buf->data = nullptr;
    }
    buf->len = 0;
}

static bool pem_copy(PemBuffer *out, const uint8_t *ptr, size_t len)
{
    if (!out) {
        return false;
    }

    pem_free(out);

    if (!ptr || len == 0) {
        return true;
    }

    // esp-tls expects PEM buffers to be NUL terminated and the size to include the terminator.
    out->data = (uint8_t *)malloc(len + 1);
    if (!out->data) {
        return false;
    }
    memcpy(out->data, ptr, len);
    out->data[len] = 0;
    out->len = len + 1;
    return true;
}

struct TlsServerConfig {
    PemBuffer server_cert_pem;
    PemBuffer server_key_pem;
    PemBuffer client_ca_pem;
    int32_t flags = 0;
};

struct TlsSession {
    esp_tls_t *tls = nullptr;
    int sockfd = -1;
};

struct ConfigEntry {
    int32_t handle = 0;
    TlsServerConfig *config = nullptr;
};

struct SessionEntry {
    int32_t handle = 0;
    TlsSession *session = nullptr;
};

static std::mutex g_tls_mutex;
static ConfigEntry g_tls_configs[kMaxTlsServerConfigs] = {};
static SessionEntry g_tls_sessions[kMaxTlsSessions] = {};
static int32_t g_next_tls_config_handle = 1;
static int32_t g_next_tls_session_handle = 1;

static int alloc_config_slot_locked()
{
    for (int i = 0; i < kMaxTlsServerConfigs; i++) {
        if (g_tls_configs[i].handle == 0) {
            return i;
        }
    }
    return -1;
}

static int alloc_session_slot_locked()
{
    for (int i = 0; i < kMaxTlsSessions; i++) {
        if (g_tls_sessions[i].handle == 0) {
            return i;
        }
    }
    return -1;
}

static TlsServerConfig *get_config_locked(int32_t handle)
{
    if (handle <= 0) {
        return nullptr;
    }
    for (int i = 0; i < kMaxTlsServerConfigs; i++) {
        if (g_tls_configs[i].handle == handle) {
            return g_tls_configs[i].config;
        }
    }
    return nullptr;
}

static TlsSession *get_session_locked(int32_t handle)
{
    if (handle <= 0) {
        return nullptr;
    }
    for (int i = 0; i < kMaxTlsSessions; i++) {
        if (g_tls_sessions[i].handle == handle) {
            return g_tls_sessions[i].session;
        }
    }
    return nullptr;
}

static void close_session(TlsSession *s)
{
    if (!s) {
        return;
    }

    if (s->tls) {
        esp_tls_server_session_delete(s->tls);
        s->tls = nullptr;
    }

    if (s->sockfd >= 0) {
        lwip_close(s->sockfd);
        s->sockfd = -1;
    }
}

static void free_config(TlsServerConfig *cfg)
{
    if (!cfg) {
        return;
    }
    pem_free(&cfg->server_cert_pem);
    pem_free(&cfg->server_key_pem);
    pem_free(&cfg->client_ca_pem);
    delete cfg;
}

static void free_session(TlsSession *s)
{
    if (!s) {
        return;
    }
    close_session(s);
    delete s;
}

void wasm_api_socket_tls_close_all(void)
{
    std::lock_guard<std::mutex> lock(g_tls_mutex);

    for (int i = 0; i < kMaxTlsSessions; i++) {
        if (g_tls_sessions[i].handle != 0) {
            free_session(g_tls_sessions[i].session);
            g_tls_sessions[i] = {};
        }
    }

    for (int i = 0; i < kMaxTlsServerConfigs; i++) {
        if (g_tls_configs[i].handle != 0) {
            free_config(g_tls_configs[i].config);
            g_tls_configs[i] = {};
        }
    }
}

static int32_t tlsServerConfigCreate(
    wasm_exec_env_t exec_env,
    const uint8_t *server_cert_pem_ptr, size_t server_cert_pem_len,
    const uint8_t *server_key_pem_ptr, size_t server_key_pem_len,
    const uint8_t *client_ca_pem_ptr, size_t client_ca_pem_len,
    int32_t flags)
{
    (void)exec_env;

    if ((flags & ~kTlsFlagsRequireClientCert) != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "tlsServerConfigCreate: unknown flags");
        return kWasmErrInvalidArgument;
    }

    if (!server_cert_pem_ptr || server_cert_pem_len == 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "tlsServerConfigCreate: missing server_cert_pem");
        return kWasmErrInvalidArgument;
    }
    if (!server_key_pem_ptr || server_key_pem_len == 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "tlsServerConfigCreate: missing server_key_pem");
        return kWasmErrInvalidArgument;
    }

    const bool require_client_cert = (flags & kTlsFlagsRequireClientCert) != 0;
    if (require_client_cert) {
        if (!client_ca_pem_ptr || client_ca_pem_len == 0) {
            wasm_api_set_last_error(kWasmErrInvalidArgument, "tlsServerConfigCreate: require_client_cert set but client_ca_pem missing");
            return kWasmErrInvalidArgument;
        }
    } else {
        if (client_ca_pem_ptr || client_ca_pem_len != 0) {
            wasm_api_set_last_error(kWasmErrInvalidArgument, "tlsServerConfigCreate: client_ca_pem provided but require_client_cert flag not set");
            return kWasmErrInvalidArgument;
        }
    }

    TlsServerConfig *cfg = new TlsServerConfig();
    if (!cfg) {
        wasm_api_set_last_error(kWasmErrInternal, "tlsServerConfigCreate: out of memory");
        return kWasmErrInternal;
    }
    cfg->flags = flags;

    if (!pem_copy(&cfg->server_cert_pem, server_cert_pem_ptr, server_cert_pem_len)
        || !pem_copy(&cfg->server_key_pem, server_key_pem_ptr, server_key_pem_len)
        || !pem_copy(&cfg->client_ca_pem, client_ca_pem_ptr, client_ca_pem_len)) {
        free_config(cfg);
        wasm_api_set_last_error(kWasmErrInternal, "tlsServerConfigCreate: out of memory copying PEM");
        return kWasmErrInternal;
    }

    std::lock_guard<std::mutex> lock(g_tls_mutex);
    const int slot = alloc_config_slot_locked();
    if (slot < 0) {
        free_config(cfg);
        wasm_api_set_last_error(kWasmErrInternal, "tlsServerConfigCreate: too many configs");
        return kWasmErrInternal;
    }

    const int32_t handle = g_next_tls_config_handle++;
    g_tls_configs[slot].handle = handle;
    g_tls_configs[slot].config = cfg;
    return handle;
}

static int32_t tlsServerConfigFree(wasm_exec_env_t exec_env, int32_t config_handle)
{
    (void)exec_env;

    std::lock_guard<std::mutex> lock(g_tls_mutex);
    for (int i = 0; i < kMaxTlsServerConfigs; i++) {
        if (g_tls_configs[i].handle == config_handle) {
            free_config(g_tls_configs[i].config);
            g_tls_configs[i] = {};
            return kWasmOk;
        }
    }

    wasm_api_set_last_error(kWasmErrInvalidArgument, "tlsServerConfigFree: bad handle");
    return kWasmErrInvalidArgument;
}

static int32_t tlsAccept(
    wasm_exec_env_t exec_env,
    int32_t config_handle,
    int32_t listen_sockfd,
    uint8_t *out_addr_ptr, int32_t out_addr_len,
    int32_t timeout_ms)
{
    (void)exec_env;

    if (!out_addr_ptr && out_addr_len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "tlsAccept: out_addr_ptr is null");
        return kWasmErrInvalidArgument;
    }
    if (out_addr_len > 0 && out_addr_len < (int32_t)sizeof(SocketAddr)) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "tlsAccept: out_addr_len too small");
        return kWasmErrInvalidArgument;
    }

    TlsServerConfig *cfg = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_tls_mutex);
        cfg = get_config_locked(config_handle);
    }
    if (!cfg) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "tlsAccept: bad config handle");
        return kWasmErrInvalidArgument;
    }

    const int32_t ready = wait_socket_ready(listen_sockfd, true, timeout_ms);
    if (ready != kWasmOk) {
        return ready;
    }

    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int client_sockfd = lwip_accept(listen_sockfd, (struct sockaddr *)&client_addr, &client_addr_len);
    if (client_sockfd < 0) {
        const int err = errno;
        if (is_would_block_errno(err)) {
            wasm_api_set_last_error(kWasmErrNotReady, "tlsAccept: no pending client");
            return kWasmErrNotReady;
        }
        if (err == ETIMEDOUT) {
            wasm_api_set_last_error(kWasmErrNotReady, "tlsAccept: timed out");
            return kWasmErrNotReady;
        }
        wasm_api_set_last_error(kWasmErrInternal, "tlsAccept: lwip_accept failed");
        return kWasmErrInternal;
    }

    if (out_addr_ptr && out_addr_len >= (int32_t)sizeof(SocketAddr)) {
        SocketAddr wasm_addr;
        sockaddr_to_wasm(&client_addr, &wasm_addr);
        memcpy(out_addr_ptr, &wasm_addr, sizeof(SocketAddr));
    }

    // Handshake generally needs some time even when the caller is "polling" for pending connections.
    // Use a small default handshake timeout to avoid immediate failures when `timeout_ms == 0`.
    constexpr int32_t kDefaultHandshakeTimeoutMs = 5000;
    const int32_t handshake_timeout_ms = (timeout_ms == 0) ? kDefaultHandshakeTimeoutMs : timeout_ms;
    apply_socket_timeouts(client_sockfd, handshake_timeout_ms);

    // Note: esp-tls requires cfg buffers to stay alive during the call. We hold cfg in the handle table.
    esp_tls_cfg_server_t tls_cfg = {};
    tls_cfg.servercert_buf = cfg->server_cert_pem.data;
    tls_cfg.servercert_bytes = (unsigned int)cfg->server_cert_pem.len;
    tls_cfg.serverkey_buf = cfg->server_key_pem.data;
    tls_cfg.serverkey_bytes = (unsigned int)cfg->server_key_pem.len;

    if ((cfg->flags & kTlsFlagsRequireClientCert) != 0) {
        tls_cfg.cacert_buf = cfg->client_ca_pem.data;
        tls_cfg.cacert_bytes = (unsigned int)cfg->client_ca_pem.len;
    }

    esp_tls_t *tls = esp_tls_init();
    if (!tls) {
        lwip_close(client_sockfd);
        wasm_api_set_last_error(kWasmErrInternal, "tlsAccept: esp_tls_init failed");
        return kWasmErrInternal;
    }

    const int rc = esp_tls_server_session_create(&tls_cfg, client_sockfd, tls);
    if (rc != 0) {
        char msg[120] = {};
        const int err = errno;
        if (rc == ESP_TLS_ERR_SSL_TIMEOUT || err == ETIMEDOUT) {
            snprintf(msg, sizeof(msg), "tlsAccept: handshake timed out (rc=%d)", rc);
            wasm_api_set_last_error(kWasmErrNotReady, msg);
            esp_tls_server_session_delete(tls);
            lwip_close(client_sockfd);
            return kWasmErrNotReady;
        }
        snprintf(msg, sizeof(msg), "tlsAccept: handshake failed (rc=%d)", rc);
        wasm_api_set_last_error(kWasmErrInternal, msg);
        esp_tls_server_session_delete(tls);
        lwip_close(client_sockfd);
        return kWasmErrInternal;
    }

    TlsSession *session = new TlsSession();
    if (!session) {
        wasm_api_set_last_error(kWasmErrInternal, "tlsAccept: out of memory");
        esp_tls_server_session_delete(tls);
        lwip_close(client_sockfd);
        return kWasmErrInternal;
    }

    session->tls = tls;
    session->sockfd = client_sockfd;

    std::lock_guard<std::mutex> lock(g_tls_mutex);
    const int slot = alloc_session_slot_locked();
    if (slot < 0) {
        wasm_api_set_last_error(kWasmErrInternal, "tlsAccept: too many sessions");
        free_session(session);
        return kWasmErrInternal;
    }

    const int32_t handle = g_next_tls_session_handle++;
    g_tls_sessions[slot].handle = handle;
    g_tls_sessions[slot].session = session;
    return handle;
}

static int32_t tlsRecv(
    wasm_exec_env_t exec_env,
    int32_t tls_handle,
    uint8_t *buf_ptr, int32_t buf_len,
    int32_t timeout_ms)
{
    (void)exec_env;

    if (!buf_ptr && buf_len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "tlsRecv: buf_ptr is null");
        return kWasmErrInvalidArgument;
    }
    if (buf_len < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "tlsRecv: buf_len < 0");
        return kWasmErrInvalidArgument;
    }

    TlsSession *session = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_tls_mutex);
        session = get_session_locked(tls_handle);
    }
    if (!session || !session->tls) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "tlsRecv: bad handle");
        return kWasmErrInvalidArgument;
    }

    const int32_t ready = wait_socket_ready(session->sockfd, true, timeout_ms);
    if (ready != kWasmOk) {
        return ready;
    }

    apply_socket_timeouts(session->sockfd, timeout_ms);

    const ssize_t rc = esp_tls_conn_read(session->tls, buf_ptr, (size_t)buf_len);
    if (rc > 0) {
        return (int32_t)rc;
    }
    if (rc == 0) {
        // Peer performed an orderly TLS shutdown (or underlying connection closed).
        return 0;
    }

    const int err = errno;
    if (rc == ESP_TLS_ERR_SSL_WANT_READ || rc == ESP_TLS_ERR_SSL_WANT_WRITE || is_would_block_errno(err) || err == ETIMEDOUT
        || rc == ESP_TLS_ERR_SSL_TIMEOUT) {
        wasm_api_set_last_error(kWasmErrNotReady, "tlsRecv: would block");
        return kWasmErrNotReady;
    }
    if (err == ENOTCONN || err == ECONNRESET) {
        wasm_api_set_last_error(kWasmErrNotReady, "tlsRecv: closed");
        return kWasmErrNotReady;
    }

    char msg[96] = {};
    snprintf(msg, sizeof(msg), "tlsRecv: read failed (rc=%d)", (int)rc);
    wasm_api_set_last_error(kWasmErrInternal, msg);
    return kWasmErrInternal;
}

static int32_t tlsSend(
    wasm_exec_env_t exec_env,
    int32_t tls_handle,
    const uint8_t *buf_ptr, int32_t buf_len,
    int32_t timeout_ms)
{
    (void)exec_env;

    if (!buf_ptr && buf_len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "tlsSend: buf_ptr is null");
        return kWasmErrInvalidArgument;
    }
    if (buf_len < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "tlsSend: buf_len < 0");
        return kWasmErrInvalidArgument;
    }

    TlsSession *session = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_tls_mutex);
        session = get_session_locked(tls_handle);
    }
    if (!session || !session->tls) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "tlsSend: bad handle");
        return kWasmErrInvalidArgument;
    }

    const int32_t ready = wait_socket_ready(session->sockfd, false, timeout_ms);
    if (ready != kWasmOk) {
        return ready;
    }

    apply_socket_timeouts(session->sockfd, timeout_ms);

    const ssize_t rc = esp_tls_conn_write(session->tls, buf_ptr, (size_t)buf_len);
    if (rc >= 0) {
        return (int32_t)rc;
    }

    const int err = errno;
    if (rc == ESP_TLS_ERR_SSL_WANT_READ || rc == ESP_TLS_ERR_SSL_WANT_WRITE || is_would_block_errno(err) || err == ETIMEDOUT
        || rc == ESP_TLS_ERR_SSL_TIMEOUT) {
        wasm_api_set_last_error(kWasmErrNotReady, "tlsSend: would block");
        return kWasmErrNotReady;
    }
    if (err == ENOTCONN || err == ECONNRESET || err == EPIPE) {
        wasm_api_set_last_error(kWasmErrNotReady, "tlsSend: closed");
        return kWasmErrNotReady;
    }

    char msg[96] = {};
    snprintf(msg, sizeof(msg), "tlsSend: write failed (rc=%d)", (int)rc);
    wasm_api_set_last_error(kWasmErrInternal, msg);
    return kWasmErrInternal;
}

static int32_t tlsClose(wasm_exec_env_t exec_env, int32_t tls_handle)
{
    (void)exec_env;

    TlsSession *session = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_tls_mutex);
        for (int i = 0; i < kMaxTlsSessions; i++) {
            if (g_tls_sessions[i].handle == tls_handle) {
                session = g_tls_sessions[i].session;
                g_tls_sessions[i] = {};
                break;
            }
        }
    }

    if (!session) {
        // Idempotent close.
        return kWasmOk;
    }

    free_session(session);
    return kWasmOk;
}

/* clang-format off */
#define REG_NATIVE_FUNC(funcName, signature) \
    { #funcName, (void *)funcName, signature, NULL }

static NativeSymbol g_socket_tls_native_symbols[] = {
    REG_NATIVE_FUNC(tlsServerConfigCreate, "(*~*~*~i)i"),
    REG_NATIVE_FUNC(tlsServerConfigFree, "(i)i"),
    REG_NATIVE_FUNC(tlsAccept, "(ii*ii)i"),
    REG_NATIVE_FUNC(tlsSend, "(i*ii)i"),
    REG_NATIVE_FUNC(tlsRecv, "(i*ii)i"),
    REG_NATIVE_FUNC(tlsClose, "(i)i"),
};
/* clang-format on */

bool wasm_api_register_socket_tls(void)
{
    const uint32_t count = sizeof(g_socket_tls_native_symbols) / sizeof(g_socket_tls_native_symbols[0]);
    bool ok = wasm_runtime_register_natives("portal_socket_tls", g_socket_tls_native_symbols, count);
    if (!ok) {
        ESP_LOGE(kTag, "Failed to register portal_socket_tls natives (count=%" PRIu32 ")", count);
        wasm_api_set_last_error(kWasmErrInternal, "register_socket_tls: wasm_runtime_register_natives failed");
    }
    return ok;
}
