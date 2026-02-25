#include <inttypes.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "esp_http_client.h"
#include "wasm_export.h"

#include "../api.h"
#include "errors.h"

namespace {

constexpr const char *kTag = "wasm_api_http";
constexpr int kMaxHeaders = 16;
constexpr int kMaxHeaderLen = 128;

struct HttpRequestContext {
    esp_http_client_handle_t client;
    char headers[kMaxHeaders][kMaxHeaderLen];
    int num_headers;
    int last_status_code;
};

static HttpRequestContext g_req_ctx = { NULL, {}, 0, 0 };

static void cleanup_client(void)
{
    if (g_req_ctx.client) {
        esp_http_client_cleanup(g_req_ctx.client);
        g_req_ctx.client = NULL;
    }
    g_req_ctx.num_headers = 0;
    g_req_ctx.last_status_code = 0;
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            break;
        case HTTP_EVENT_ON_FINISH:
            g_req_ctx.last_status_code = esp_http_client_get_status_code(g_req_ctx.client);
            break;
        default:
            break;
    }
    return ESP_OK;
}

int32_t httpGet(wasm_exec_env_t exec_env, const char *url, uint8_t *out_ptr, int32_t out_len, int32_t timeout_ms)
{
    (void)exec_env;

    if (!url) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "httpGet: url is null");
        return kWasmErrInvalidArgument;
    }

    if (!out_ptr && out_len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "httpGet: out_ptr is null");
        return kWasmErrInvalidArgument;
    }

    cleanup_client();

    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = (timeout_ms > 0) ? (int)timeout_ms : 5000;
    config.event_handler = http_event_handler;
    config.user_data = &g_req_ctx;

    g_req_ctx.client = esp_http_client_init(&config);
    if (!g_req_ctx.client) {
        wasm_api_set_last_error(kWasmErrInternal, "httpGet: esp_http_client_init failed");
        return kWasmErrInternal;
    }

    esp_err_t err = esp_http_client_set_method(g_req_ctx.client, HTTP_METHOD_GET);
    if (err != ESP_OK) {
        cleanup_client();
        wasm_api_set_last_error(kWasmErrInternal, "httpGet: esp_http_client_set_method failed");
        return kWasmErrInternal;
    }

    for (int i = 0; i < g_req_ctx.num_headers; i++) {
        esp_http_client_set_header(g_req_ctx.client, g_req_ctx.headers[i], strchr(g_req_ctx.headers[i], ':') + 1);
    }

    err = esp_http_client_perform(g_req_ctx.client);
    if (err != ESP_OK) {
        cleanup_client();
        wasm_api_set_last_error(kWasmErrInternal, "httpGet: esp_http_client_perform failed");
        return kWasmErrInternal;
    }

    g_req_ctx.last_status_code = esp_http_client_get_status_code(g_req_ctx.client);

    int content_length = esp_http_client_get_content_length(g_req_ctx.client);
    if (content_length <= 0) {
        content_length = 0;
    }

    int to_read = (out_len < content_length) ? out_len : content_length;
    int bytes_read = 0;

    if (to_read > 0) {
        err = esp_http_client_read(g_req_ctx.client, (char *)out_ptr, to_read);
        if (err < 0) {
            cleanup_client();
            wasm_api_set_last_error(kWasmErrInternal, "httpGet: esp_http_client_read failed");
            return kWasmErrInternal;
        }
        bytes_read = err;
    }

    return bytes_read;
}

int32_t httpPost(wasm_exec_env_t exec_env, const char *url, const char *content_type, const uint8_t *body_ptr,
    int32_t body_len, uint8_t *out_ptr, int32_t out_len, int32_t timeout_ms)
{
    (void)exec_env;

    if (!url) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "httpPost: url is null");
        return kWasmErrInvalidArgument;
    }

    if (!body_ptr && body_len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "httpPost: body_ptr is null");
        return kWasmErrInvalidArgument;
    }

    if (!out_ptr && out_len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "httpPost: out_ptr is null");
        return kWasmErrInvalidArgument;
    }

    cleanup_client();

    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = (timeout_ms > 0) ? (int)timeout_ms : 5000;
    config.event_handler = http_event_handler;
    config.user_data = &g_req_ctx;

    g_req_ctx.client = esp_http_client_init(&config);
    if (!g_req_ctx.client) {
        wasm_api_set_last_error(kWasmErrInternal, "httpPost: esp_http_client_init failed");
        return kWasmErrInternal;
    }

    esp_err_t err = esp_http_client_set_method(g_req_ctx.client, HTTP_METHOD_POST);
    if (err != ESP_OK) {
        cleanup_client();
        wasm_api_set_last_error(kWasmErrInternal, "httpPost: esp_http_client_set_method failed");
        return kWasmErrInternal;
    }

    if (content_type) {
        esp_http_client_set_header(g_req_ctx.client, "Content-Type", content_type);
    }

    for (int i = 0; i < g_req_ctx.num_headers; i++) {
        esp_http_client_set_header(g_req_ctx.client, g_req_ctx.headers[i], strchr(g_req_ctx.headers[i], ':') + 1);
    }

    err = esp_http_client_set_post_field(g_req_ctx.client, (const char *)body_ptr, body_len);
    if (err != ESP_OK) {
        cleanup_client();
        wasm_api_set_last_error(kWasmErrInternal, "httpPost: esp_http_client_set_post_field failed");
        return kWasmErrInternal;
    }

    err = esp_http_client_perform(g_req_ctx.client);
    if (err != ESP_OK) {
        cleanup_client();
        wasm_api_set_last_error(kWasmErrInternal, "httpPost: esp_http_client_perform failed");
        return kWasmErrInternal;
    }

    g_req_ctx.last_status_code = esp_http_client_get_status_code(g_req_ctx.client);

    int content_length = esp_http_client_get_content_length(g_req_ctx.client);
    if (content_length <= 0) {
        content_length = 0;
    }

    int to_read = (out_len < content_length) ? out_len : content_length;
    int bytes_read = 0;

    if (to_read > 0) {
        err = esp_http_client_read(g_req_ctx.client, (char *)out_ptr, to_read);
        if (err < 0) {
            cleanup_client();
            wasm_api_set_last_error(kWasmErrInternal, "httpPost: esp_http_client_read failed");
            return kWasmErrInternal;
        }
        bytes_read = err;
    }

    return bytes_read;
}

int32_t httpSetHeader(wasm_exec_env_t exec_env, const char *key, const char *value)
{
    (void)exec_env;

    if (!key) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "httpSetHeader: key is null");
        return kWasmErrInvalidArgument;
    }

    if (!value) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "httpSetHeader: value is null");
        return kWasmErrInvalidArgument;
    }

    if (g_req_ctx.num_headers >= kMaxHeaders) {
        wasm_api_set_last_error(kWasmErrInternal, "httpSetHeader: too many headers");
        return kWasmErrInternal;
    }

    snprintf(g_req_ctx.headers[g_req_ctx.num_headers], kMaxHeaderLen, "%s: %s", key, value);
    g_req_ctx.num_headers++;
    return kWasmOk;
}

int32_t httpGetStatusCode(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return g_req_ctx.last_status_code;
}

/* clang-format off */
#define REG_NATIVE_FUNC(funcName, signature) \
    { #funcName, (void *)funcName, signature, NULL }

static NativeSymbol g_http_native_symbols[] = {
    REG_NATIVE_FUNC(httpGet, "($*~i)i"),
    REG_NATIVE_FUNC(httpPost, "($$*~*~i)i"),
    REG_NATIVE_FUNC(httpSetHeader, "($$)i"),
    REG_NATIVE_FUNC(httpGetStatusCode, "()i"),
};
/* clang-format on */

} // namespace

bool wasm_api_register_http(void)
{
    const uint32_t count = sizeof(g_http_native_symbols) / sizeof(g_http_native_symbols[0]);
    bool ok = wasm_runtime_register_natives("portal_http", g_http_native_symbols, count);
    if (!ok) {
        ESP_LOGE(kTag, "Failed to register portal_http natives (count=%" PRIu32 ")", count);
        wasm_api_set_last_error(kWasmErrInternal, "register_http: wasm_runtime_register_natives failed");
    }
    return ok;
}
