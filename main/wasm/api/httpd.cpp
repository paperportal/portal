#include <inttypes.h>
#include <stdint.h>
#include <string.h>
#include <mutex>

#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "wasm_export.h"

#include "host/event_loop.h"
#include "host/httpd_host.h"

#include "../api.h"
#include "errors.h"

static constexpr int kMaxActiveRequests = 8;

struct ActiveRequest {
    int32_t req_id;
    httpd_req_t *req;
    bool active;
};

static ActiveRequest g_active_requests[kMaxActiveRequests] = {};
static std::mutex g_httpd_mutex;

namespace {

constexpr const char *kTag = "wasm_api_httpd";
constexpr int kMaxHandlers = 8;
constexpr int kMaxUriLen = 64;

#pragma pack(push, 1)
struct HttpRequestInfo {
    int32_t req_id;
    int32_t method;
    int32_t content_len;
    char uri[kMaxUriLen];
};
#pragma pack(pop)

struct HandlerEntry {
    char uri[kMaxUriLen];
    httpd_method_t method;
    bool active;
};

static httpd_handle_t g_server = NULL;
static HandlerEntry g_handlers[kMaxHandlers] = {};
static int32_t g_next_req_id = 1;

static esp_err_t request_handler(httpd_req_t *req)
{
    std::lock_guard<std::mutex> lock(g_httpd_mutex);
    // Find a free slot in the active requests table
    int free_slot = -1;
    for (int i = 0; i < kMaxActiveRequests; i++) {
        if (!g_active_requests[i].active) {
            free_slot = i;
            break;
        }
    }

    if (free_slot < 0) {
        httpd_resp_send_500(req);
        ESP_LOGE(kTag, "request_handler: no free slots in active requests table");
        return ESP_FAIL;
    }

    // Store the request (keep the original pointer, don't copy)
    const int32_t req_id = g_next_req_id++;
    g_active_requests[free_slot].req_id = req_id;
    g_active_requests[free_slot].req = req;
    g_active_requests[free_slot].active = true;

    HostEvent ev = MakeHttpRequestEvent((int32_t)(esp_timer_get_time() / 1000), req_id,
        (int32_t)req->method, (int32_t)req->content_len);
    if (!host_event_loop_enqueue(ev, 0)) {
        ESP_LOGW(kTag, "request_handler: event queue not ready (req_id=%" PRId32 ")", req_id);
    }

    return ESP_OK;
}

static httpd_method_t wasm_method_to_httpd(int32_t method)
{
    switch (method) {
        case 0:
            return HTTP_GET;
        case 1:
            return HTTP_POST;
        default:
            return HTTP_GET;
    }
}

int32_t httpdStart(wasm_exec_env_t exec_env, int32_t port)
{
    (void)exec_env;
    std::lock_guard<std::mutex> lock(g_httpd_mutex);

    if (g_server) {
        return kWasmErrInternal;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = (port > 0 && port <= 65535) ? (uint16_t)port : 80;
    config.lru_purge_enable = true;

    esp_err_t err = ::httpd_start(&g_server, &config);
    if (err != ESP_OK) {
        wasm_api_set_last_error(kWasmErrInternal, "httpd_start: ::httpd_start failed");
        return kWasmErrInternal;
    }

    return kWasmOk;
}

int32_t httpdStop(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    std::lock_guard<std::mutex> lock(g_httpd_mutex);

    if (!g_server) {
        return kWasmOk;
    }

    ::httpd_stop(g_server);
    g_server = NULL;

    for (int i = 0; i < kMaxHandlers; i++) {
        g_handlers[i].active = false;
    }

    for (int i = 0; i < kMaxActiveRequests; i++) {
        g_active_requests[i].active = false;
        g_active_requests[i].req = NULL;
    }

    return kWasmOk;
}

int32_t httpdRegisterHandler(wasm_exec_env_t exec_env, const char *uri, int32_t method)
{
    (void)exec_env;
    std::lock_guard<std::mutex> lock(g_httpd_mutex);

    if (!uri) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "httpd_register_handler: uri is null");
        return kWasmErrInvalidArgument;
    }

    if (!g_server) {
        wasm_api_set_last_error(kWasmErrNotReady, "httpd_register_handler: server not started");
        return kWasmErrNotReady;
    }

    int free_slot = -1;
    for (int i = 0; i < kMaxHandlers; i++) {
        if (g_handlers[i].active && strncmp(g_handlers[i].uri, uri, kMaxUriLen) == 0
            && g_handlers[i].method == wasm_method_to_httpd(method)) {
            wasm_api_set_last_error(kWasmErrInternal, "httpd_register_handler: handler already registered");
            return kWasmErrInternal;
        }
        if (!g_handlers[i].active && free_slot < 0) {
            free_slot = i;
        }
    }

    if (free_slot < 0) {
        wasm_api_set_last_error(kWasmErrInternal, "httpd_register_handler: too many handlers");
        return kWasmErrInternal;
    }

    httpd_uri_t uri_config = {};
    uri_config.uri = uri;
    uri_config.method = wasm_method_to_httpd(method);
    uri_config.handler = request_handler;
    uri_config.user_ctx = NULL;

    esp_err_t err = ::httpd_register_uri_handler(g_server, &uri_config);
    if (err != ESP_OK) {
        wasm_api_set_last_error(kWasmErrInternal, "httpd_register_handler: ::httpd_register_uri_handler failed");
        return kWasmErrInternal;
    }

    strncpy(g_handlers[free_slot].uri, uri, kMaxUriLen - 1);
    g_handlers[free_slot].uri[kMaxUriLen - 1] = '\0';
    g_handlers[free_slot].method = uri_config.method;
    g_handlers[free_slot].active = true;

    return kWasmOk;
}

int32_t httpdUnregisterHandler(wasm_exec_env_t exec_env, const char *uri, int32_t method)
{
    (void)exec_env;
    std::lock_guard<std::mutex> lock(g_httpd_mutex);

    if (!uri) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "httpd_unregister_handler: uri is null");
        return kWasmErrInvalidArgument;
    }

    if (!g_server) {
        return kWasmOk;
    }

    httpd_method_t http_method = wasm_method_to_httpd(method);
    for (int i = 0; i < kMaxHandlers; i++) {
        if (g_handlers[i].active && strncmp(g_handlers[i].uri, uri, kMaxUriLen) == 0 && g_handlers[i].method == http_method) {
            g_handlers[i].active = false;
            return kWasmOk;
        }
    }

    return kWasmOk;
}

int32_t httpdPoll(wasm_exec_env_t exec_env, uint8_t *out_ptr, int32_t out_len)
{
    (void)exec_env;
    std::lock_guard<std::mutex> lock(g_httpd_mutex);

    if (!out_ptr && out_len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "httpd_poll: out_ptr is null");
        return kWasmErrInvalidArgument;
    }

    if (out_len < (int32_t)sizeof(HttpRequestInfo)) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "httpd_poll: out_len too small");
        return kWasmErrInvalidArgument;
    }

    // Scan for the oldest pending request (smallest req_id that's still active)
    int32_t found_req_id = -1;
    int found_slot = -1;

    for (int i = 0; i < kMaxActiveRequests; i++) {
        if (g_active_requests[i].active) {
            if (found_req_id < 0 || g_active_requests[i].req_id < found_req_id) {
                found_req_id = g_active_requests[i].req_id;
                found_slot = i;
            }
        }
    }

    if (found_slot < 0) {
        // No pending requests
        return 0;
    }

    // Return the request info (but keep the request active for respond())
    httpd_req_t *req = g_active_requests[found_slot].req;
    HttpRequestInfo info = {};

    if (req) {
        info.req_id = g_active_requests[found_slot].req_id;
        info.method = (int32_t)req->method;
        info.content_len = (int32_t)req->content_len;
        strncpy(info.uri, req->uri, kMaxUriLen - 1);
        info.uri[kMaxUriLen - 1] = '\0';

        memcpy(out_ptr, &info, sizeof(HttpRequestInfo));
        return sizeof(HttpRequestInfo);
    }

    return 0;
}

int32_t httpdRespond(wasm_exec_env_t exec_env, int32_t req_id, int32_t status, const char *content_type,
    const uint8_t *body_ptr, int32_t body_len)
{
    (void)exec_env;
    std::lock_guard<std::mutex> lock(g_httpd_mutex);

    if (!g_server) {
        wasm_api_set_last_error(kWasmErrNotReady, "httpd_respond: server not started");
        return kWasmErrNotReady;
    }

    // Find the request by req_id
    int found_slot = -1;
    for (int i = 0; i < kMaxActiveRequests; i++) {
        if (g_active_requests[i].active && g_active_requests[i].req_id == req_id) {
            found_slot = i;
            break;
        }
    }

    if (found_slot < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "httpd_respond: req_id not found");
        return kWasmErrInvalidArgument;
    }

    httpd_req_t *req = g_active_requests[found_slot].req;
    if (!req) {
        g_active_requests[found_slot].active = false;
        wasm_api_set_last_error(kWasmErrInternal, "httpd_respond: request is null");
        return kWasmErrInternal;
    }

    // Set status code
    if (status != 200) {
        char status_str[16];
        snprintf(status_str, sizeof(status_str), "%" PRId32, status);
        httpd_resp_set_status(req, status_str);
    }

    // Set content type if provided
    if (content_type) {
        esp_err_t err = httpd_resp_set_type(req, content_type);
        if (err != ESP_OK) {
            g_active_requests[found_slot].active = false;
            wasm_api_set_last_error(kWasmErrInternal, "httpd_respond: httpd_resp_set_type failed");
            return kWasmErrInternal;
        }
    }

    // Send response body
    esp_err_t err;
    if (body_ptr && body_len > 0) {
        err = httpd_resp_send(req, (const char *)body_ptr, body_len);
    } else {
        err = httpd_resp_send(req, NULL, 0); // Send empty response
    }

    // Mark request as inactive (response sent, connection will be closed by httpd)
    g_active_requests[found_slot].active = false;
    g_active_requests[found_slot].req = NULL;

    if (err != ESP_OK) {
        wasm_api_set_last_error(kWasmErrInternal, "httpd_respond: httpd_resp_send failed");
        return kWasmErrInternal;
    }

    return kWasmOk;
}

/* clang-format off */
#define REG_NATIVE_FUNC(funcName, signature) \
    { #funcName, (void *)funcName, signature, NULL }

static NativeSymbol g_httpd_native_symbols[] = {
    REG_NATIVE_FUNC(httpdStart, "(i)i"),
    REG_NATIVE_FUNC(httpdStop, "()i"),
    REG_NATIVE_FUNC(httpdRegisterHandler, "(*i)i"),
    REG_NATIVE_FUNC(httpdUnregisterHandler, "(*i)i"),
    REG_NATIVE_FUNC(httpdPoll, "(*i)i"),
    REG_NATIVE_FUNC(httpdRespond, "(ii**i)i"),
};
/* clang-format on */

} // namespace

bool httpd_host_get_request_info(int32_t req_id, HttpdHostRequestInfo *out_info)
{
    if (!out_info) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_httpd_mutex);
    for (int i = 0; i < kMaxActiveRequests; i++) {
        if (g_active_requests[i].active && g_active_requests[i].req_id == req_id) {
            httpd_req_t *req = g_active_requests[i].req;
            if (!req) {
                return false;
            }
            out_info->req_id = req_id;
            out_info->method = (int32_t)req->method;
            out_info->content_len = (int32_t)req->content_len;
            out_info->uri = req->uri;
            out_info->req = req;
            return true;
        }
    }

    return false;
}

bool wasm_api_register_httpd(void)
{
    const uint32_t count = sizeof(g_httpd_native_symbols) / sizeof(g_httpd_native_symbols[0]);
    bool ok = wasm_runtime_register_natives("portal_httpd", g_httpd_native_symbols, count);
    if (!ok) {
        ESP_LOGE(kTag, "Failed to register portal_httpd natives (count=%" PRIu32 ")", count);
        wasm_api_set_last_error(kWasmErrInternal, "register_httpd: wasm_runtime_register_natives failed");
    }
    return ok;
}
