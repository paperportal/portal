#include <inttypes.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <errno.h>

#include "esp_log.h"
#include "lwip/sockets.h"
#include "wasm_export.h"

#include "../api.h"
#include "errors.h"

namespace {

constexpr const char *kTag = "wasm_api_socket";

#pragma pack(push, 1)
struct SocketAddr {
    int32_t family;
    uint16_t port;
    uint8_t ip[4];
    uint8_t _pad[2];
};
#pragma pack(pop)

static int lwip_family_from_wasm(int32_t family)
{
    switch (family) {
        case 2:
            return AF_INET;
        case 10:
            return AF_INET6;
        default:
            return AF_INET;
    }
}

static int lwip_type_from_wasm(int32_t type)
{
    switch (type) {
        case 1:
            return SOCK_STREAM;
        case 2:
            return SOCK_DGRAM;
        default:
            return SOCK_STREAM;
    }
}

static void sockaddr_from_wasm(const SocketAddr *wasm_addr, struct sockaddr_in *out)
{
    memset(out, 0, sizeof(*out));
    out->sin_family = AF_INET;
    out->sin_port = htons(wasm_addr->port);
    memcpy(&out->sin_addr.s_addr, wasm_addr->ip, 4);
}

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
        wasm_api_set_last_error(kWasmErrNotReady, "socket: operation timed out");
        return kWasmErrNotReady;
    }

    wasm_api_set_last_error(kWasmErrInternal, "socket: select failed");
    return kWasmErrInternal;
}

int32_t sockAcceptWithTimeout(
    wasm_exec_env_t exec_env,
    int32_t sockfd,
    uint8_t *out_addr_ptr,
    int32_t out_addr_len,
    int32_t timeout_ms);

int32_t sockSocket(wasm_exec_env_t exec_env, int32_t domain, int32_t type, int32_t protocol)
{
    (void)exec_env;

    int sock = lwip_socket(lwip_family_from_wasm(domain), lwip_type_from_wasm(type), protocol);
    if (sock < 0) {
        wasm_api_set_last_error(kWasmErrInternal, "sockSocket: lwip_socket failed");
        return kWasmErrInternal;
    }

    return sock;
}

int32_t sockConnect(wasm_exec_env_t exec_env, int32_t sockfd, const uint8_t *addr_ptr, int32_t addr_len,
    int32_t timeout_ms)
{
    (void)exec_env;

    if (!addr_ptr || addr_len < (int32_t)sizeof(SocketAddr)) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "sockConnect: invalid address");
        return kWasmErrInvalidArgument;
    }

    struct sockaddr_in addr;
    sockaddr_from_wasm((const SocketAddr *)addr_ptr, &addr);

    if (timeout_ms > 0) {
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        lwip_setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        lwip_setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    }

    int rc = lwip_connect(sockfd, (struct sockaddr *)&addr, sizeof(addr));
    if (rc < 0) {
        wasm_api_set_last_error(kWasmErrInternal, "sockConnect: lwip_connect failed");
        return kWasmErrInternal;
    }

    return kWasmOk;
}

int32_t sockBind(wasm_exec_env_t exec_env, int32_t sockfd, const uint8_t *addr_ptr, int32_t addr_len)
{
    (void)exec_env;

    if (!addr_ptr || addr_len < (int32_t)sizeof(SocketAddr)) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "sockBind: invalid address");
        return kWasmErrInvalidArgument;
    }

    struct sockaddr_in addr;
    sockaddr_from_wasm((const SocketAddr *)addr_ptr, &addr);

    int rc = lwip_bind(sockfd, (struct sockaddr *)&addr, sizeof(addr));
    if (rc < 0) {
        wasm_api_set_last_error(kWasmErrInternal, "sockBind: lwip_bind failed");
        return kWasmErrInternal;
    }

    return kWasmOk;
}

int32_t sockListen(wasm_exec_env_t exec_env, int32_t sockfd, int32_t backlog)
{
    (void)exec_env;

    int rc = lwip_listen(sockfd, (backlog > 0) ? backlog : 5);
    if (rc < 0) {
        wasm_api_set_last_error(kWasmErrInternal, "sockListen: lwip_listen failed");
        return kWasmErrInternal;
    }

    return kWasmOk;
}

int32_t sockAccept(wasm_exec_env_t exec_env, int32_t sockfd, uint8_t *out_addr_ptr, int32_t out_addr_len)
{
    return sockAcceptWithTimeout(exec_env, sockfd, out_addr_ptr, out_addr_len, -1);
}

int32_t sockAcceptWithTimeout(
    wasm_exec_env_t exec_env,
    int32_t sockfd,
    uint8_t *out_addr_ptr,
    int32_t out_addr_len,
    int32_t timeout_ms)
{
    (void)exec_env;

    if (!out_addr_ptr && out_addr_len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "sockAccept: out_addr_ptr is null");
        return kWasmErrInvalidArgument;
    }

    if (out_addr_len > 0 && out_addr_len < (int32_t)sizeof(SocketAddr)) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "sockAccept: out_addr_len too small");
        return kWasmErrInvalidArgument;
    }

    const int32_t ready = wait_socket_ready(sockfd, true, timeout_ms);
    if (ready != kWasmOk) {
        return ready;
    }

    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    int client_sock = lwip_accept(sockfd, (struct sockaddr *)&client_addr, &addr_len);
    if (client_sock < 0) {
        const int err = errno;
        if (is_would_block_errno(err)) {
            wasm_api_set_last_error(kWasmErrNotReady, "sockAccept: no pending client");
            return kWasmErrNotReady;
        }
        if (err == ETIMEDOUT) {
            wasm_api_set_last_error(kWasmErrNotReady, "sockAccept: timed out");
            return kWasmErrNotReady;
        }
        wasm_api_set_last_error(kWasmErrInternal, "sockAccept: lwip_accept failed");
        return kWasmErrInternal;
    }

    if (out_addr_ptr && out_addr_len >= (int32_t)sizeof(SocketAddr)) {
        SocketAddr wasm_addr;
        sockaddr_to_wasm(&client_addr, &wasm_addr);
        memcpy(out_addr_ptr, &wasm_addr, sizeof(SocketAddr));
    }

    return client_sock;
}

int32_t sockSend(wasm_exec_env_t exec_env, int32_t sockfd, const uint8_t *buf_ptr, int32_t buf_len, int32_t timeout_ms)
{
    (void)exec_env;

    if (!buf_ptr && buf_len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "sockSend: buf_ptr is null");
        return kWasmErrInvalidArgument;
    }

    const int32_t ready = wait_socket_ready(sockfd, false, timeout_ms);
    if (ready != kWasmOk) {
        return ready;
    }

    int rc = lwip_send(sockfd, buf_ptr, buf_len, 0);
    if (rc < 0) {
        const int err = errno;
        if (is_would_block_errno(err) || err == ETIMEDOUT) {
            wasm_api_set_last_error(kWasmErrNotReady, "sockSend: would block");
            return kWasmErrNotReady;
        }
        wasm_api_set_last_error(kWasmErrInternal, "sockSend: lwip_send failed");
        return kWasmErrInternal;
    }

    return rc;
}

int32_t sockRecv(wasm_exec_env_t exec_env, int32_t sockfd, uint8_t *buf_ptr, int32_t buf_len, int32_t timeout_ms)
{
    (void)exec_env;

    if (!buf_ptr && buf_len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "sockRecv: buf_ptr is null");
        return kWasmErrInvalidArgument;
    }

    const int32_t ready = wait_socket_ready(sockfd, true, timeout_ms);
    if (ready != kWasmOk) {
        return ready;
    }

    int rc = lwip_recv(sockfd, buf_ptr, buf_len, 0);
    if (rc < 0) {
        const int err = errno;
        if (is_would_block_errno(err) || err == ETIMEDOUT) {
            wasm_api_set_last_error(kWasmErrNotReady, "sockRecv: would block");
            return kWasmErrNotReady;
        }
        if (err == ENOTCONN || err == ECONNRESET) {
            wasm_api_set_last_error(kWasmErrNotReady, "sockRecv: closed");
            return kWasmErrNotReady;
        }
        wasm_api_set_last_error(kWasmErrInternal, "sockRecv: lwip_recv failed");
        return kWasmErrInternal;
    }

    return rc;
}

int32_t sockClose(wasm_exec_env_t exec_env, int32_t sockfd)
{
    (void)exec_env;

    lwip_close(sockfd);
    return kWasmOk;
}

/* clang-format off */
#define REG_NATIVE_FUNC(funcName, signature) \
    { #funcName, (void *)funcName, signature, NULL }

static NativeSymbol g_socket_native_symbols[] = {
    REG_NATIVE_FUNC(sockSocket, "(iii)i"),
    REG_NATIVE_FUNC(sockConnect, "(i*ii)i"),
    REG_NATIVE_FUNC(sockBind, "(i*i)i"),
    REG_NATIVE_FUNC(sockListen, "(ii)i"),
    REG_NATIVE_FUNC(sockAccept, "(i*ii)i"),
    REG_NATIVE_FUNC(sockAcceptWithTimeout, "(i*ii)i"),
    REG_NATIVE_FUNC(sockSend, "(i*ii)i"),
    REG_NATIVE_FUNC(sockRecv, "(i*ii)i"),
    REG_NATIVE_FUNC(sockClose, "(i)i"),
};
/* clang-format on */

} // namespace

bool wasm_api_register_socket(void)
{
    const uint32_t count = sizeof(g_socket_native_symbols) / sizeof(g_socket_native_symbols[0]);
    bool ok = wasm_runtime_register_natives("m5_socket", g_socket_native_symbols, count);
    if (!ok) {
        ESP_LOGE(kTag, "Failed to register m5_socket natives (count=%" PRIu32 ")", count);
        wasm_api_set_last_error(kWasmErrInternal, "register_socket: wasm_runtime_register_natives failed");
    }
    return ok;
}
