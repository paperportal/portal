#include <inttypes.h>
#include <mutex>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/ip4_addr.h"
#include "host/event_loop.h"
#include "services/devserver_service.h"
#include "services/wifi_service.h"
#include "services/settings_service.h"
#include "wasm/api/errors.h"

namespace devserver {

namespace {

static bool ENABLE_MDNS = true;
static const char* MDNS_HOSTNAME = "portal";

constexpr const char *kTag = "devserver";
constexpr uint16_t kPort = 80;
constexpr size_t kMaxWasmUploadBytes = 1024 * 1024;

constexpr size_t kLogCapacity = 256;
constexpr size_t kLogLineMax = 200;
constexpr int kSseBacklogLines = 40;
constexpr int kSseTaskStack = 4 * 1024;
constexpr int kStartTaskStack = 6 * 1024;

struct LogEntry {
    uint32_t seq;
    int32_t ts_ms;
    char line[kLogLineMax];
};

enum class ServerLifecycle : uint8_t {
    Stopped = 0,
    Starting = 1,
    Running = 2,
};

struct State {
    std::mutex mutex;

    ServerLifecycle lifecycle = ServerLifecycle::Stopped;
    bool start_cancel_requested = false;
    uint32_t start_generation = 0;
    TaskHandle_t start_task = nullptr;

    bool using_softap = false;
    bool started_softap = false;

    char url[96] = {};
    char ap_ssid[33] = {};
    char ap_password[17] = {};

    bool uploaded_running = false;
    bool uploaded_crashed = false;
    char crash_reason[160] = {};
    char last_server_error[160] = {};

    httpd_handle_t server = nullptr;

    LogEntry *logs = nullptr;
    size_t log_head = 0;
    size_t log_count = 0;
    uint32_t next_seq = 1;
};

static State g_state;

static void ensure_logs_allocated_locked()
{
    if (g_state.logs) {
        return;
    }

    const size_t bytes = sizeof(LogEntry) * kLogCapacity;
    g_state.logs = static_cast<LogEntry *>(
        heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!g_state.logs) {
        g_state.logs = static_cast<LogEntry *>(heap_caps_malloc(bytes, MALLOC_CAP_8BIT));
    }
    if (g_state.logs) {
        memset(g_state.logs, 0, bytes);
    }
}

static void random_password(char out[17])
{
    static const char kAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
    const size_t n = sizeof(kAlphabet) - 1;
    for (int i = 0; i < 12; i++) {
        uint32_t r = esp_random();
        out[i] = kAlphabet[r % n];
    }
    out[12] = '\0';
}

static void format_ip4(const esp_ip4_addr_t &ip, char *out, size_t out_len)
{
    ip4_addr_t lwip_ip = {};
    lwip_ip.addr = ip.addr;
    ip4addr_ntoa_r(&lwip_ip, out, (int)out_len);
}

static void set_server_error_locked(const char *msg)
{
    if (!msg) {
        g_state.last_server_error[0] = '\0';
        return;
    }
    snprintf(g_state.last_server_error, sizeof(g_state.last_server_error), "%s", msg);
}

static bool lifecycle_is_running_locked()
{
    return g_state.lifecycle == ServerLifecycle::Running;
}

static bool lifecycle_is_starting_locked()
{
    return g_state.lifecycle == ServerLifecycle::Starting;
}

static void log_append_locked(const char *line)
{
    if (!line || line[0] == '\0') {
        return;
    }

    ensure_logs_allocated_locked();
    if (!g_state.logs) {
        return;
    }

    const size_t idx = (g_state.log_head + g_state.log_count) % kLogCapacity;
    LogEntry &e = g_state.logs[idx];
    e.seq = g_state.next_seq++;
    e.ts_ms = (int32_t)(esp_timer_get_time() / 1000);
    snprintf(e.line, sizeof(e.line), "%s", line);

    if (g_state.log_count < kLogCapacity) {
        g_state.log_count++;
    }
    else {
        g_state.log_head = (g_state.log_head + 1) % kLogCapacity;
    }
}

static esp_err_t handle_root(httpd_req_t *req)
{
    static const char kHtml[] =
        "<!doctype html><html><head><meta charset='utf-8'/>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'/>"
        "<title>Paper Portal Dev Server</title>"
        "<style>body{font-family:system-ui,Arial;margin:16px}#logs{white-space:pre-wrap;border:1px solid #ccc;"
        "padding:8px;height:50vh;overflow:auto;background:#111;color:#eee}button{margin-right:8px}</style>"
        "</head><body>"
        "<h2>Paper Portal Dev Server</h2>"
        "<p><input id='file' type='file' accept='.wasm'/>"
        " Args: <input id='args' type='text' style='width:40ch'/>"
        " <button id='run'>Run</button><button id='stop'>Stop app</button></p>"
        "<div id='status'></div>"
        "<div id='logs'></div>"
        "<script>"
        "const logs=document.getElementById('logs');"
        "function add(line){logs.textContent+=line+'\\n';logs.scrollTop=logs.scrollHeight;}"
        "async function status(){const r=await fetch('/api/status');const j=await r.json();"
        "document.getElementById('status').textContent=JSON.stringify(j);}"
        "const es=new EventSource('/api/logs');"
        "es.onmessage=e=>add(e.data);"
        "es.onerror=()=>{};"
        "status();setInterval(status,2000);"
        "document.getElementById('run').onclick=async()=>{"
        "const f=document.getElementById('file').files[0];if(!f){alert('pick a .wasm file');return;}"
        "const args=document.getElementById('args').value;"
        "const buf=await f.arrayBuffer();"
        "const r=await fetch('/api/run?args='+encodeURIComponent(args),{method:'POST',headers:{'Content-Type':'application/wasm'},body:buf});"
        "add('RUN '+r.status+' '+(await r.text()));};"
        "document.getElementById('stop').onclick=async()=>{"
        "const r=await fetch('/api/stop',{method:'POST'});add('STOP '+r.status+' '+(await r.text()));};"
        "</script></body></html>";

    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, kHtml, HTTPD_RESP_USE_STRLEN);
}

static void json_bool(char *out, size_t out_len, bool value)
{
    snprintf(out, out_len, value ? "true" : "false");
}

static esp_err_t handle_status(httpd_req_t *req)
{
    char json[640] = {};
    char running[6] = {};
    char starting[6] = {};
    char softap[6] = {};
    char uploaded[6] = {};
    char crashed[6] = {};

    std::lock_guard<std::mutex> lock(g_state.mutex);
    json_bool(running, sizeof(running), lifecycle_is_running_locked());
    json_bool(starting, sizeof(starting), lifecycle_is_starting_locked());
    json_bool(softap, sizeof(softap), g_state.using_softap);
    json_bool(uploaded, sizeof(uploaded), g_state.uploaded_running);
    json_bool(crashed, sizeof(crashed), g_state.uploaded_crashed);

    const int kMaxJsonStr = 80;
    int url_len = (int)strnlen(g_state.url, sizeof(g_state.url));
    int ssid_len = (int)strnlen(g_state.ap_ssid, sizeof(g_state.ap_ssid));
    int pw_len = (int)strnlen(g_state.ap_password, sizeof(g_state.ap_password));
    int crash_len = (int)strnlen(g_state.crash_reason, sizeof(g_state.crash_reason));
    int err_len = (int)strnlen(g_state.last_server_error, sizeof(g_state.last_server_error));

    if (url_len > kMaxJsonStr) url_len = kMaxJsonStr;
    if (ssid_len > kMaxJsonStr) ssid_len = kMaxJsonStr;
    if (pw_len > kMaxJsonStr) pw_len = kMaxJsonStr;
    if (crash_len > kMaxJsonStr) crash_len = kMaxJsonStr;
    if (err_len > kMaxJsonStr) err_len = kMaxJsonStr;

    snprintf(json, sizeof(json),
        "{"
        "\"server_running\":%s,"
        "\"server_starting\":%s,"
        "\"using_softap\":%s,"
        "\"url\":\"%.*s\","
        "\"ap_ssid\":\"%.*s\","
        "\"ap_password\":\"%.*s\","
        "\"uploaded_running\":%s,"
        "\"uploaded_crashed\":%s,"
        "\"crash_reason\":\"%.*s\","
        "\"last_error\":\"%.*s\""
        "}",
        running, starting, softap,
        url_len, g_state.url,
        ssid_len, g_state.ap_ssid,
        pw_len, g_state.ap_password,
        uploaded, crashed,
        crash_len, g_state.crash_reason,
        err_len, g_state.last_server_error);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t send_json(httpd_req_t *req, int status, bool ok, const char *message)
{
    char buf[256] = {};
    snprintf(buf, sizeof(buf), "{\"ok\":%s,\"message\":\"%s\"}", ok ? "true" : "false", message ? message : "");

    if (status != 200) {
        char status_str[8] = {};
        snprintf(status_str, sizeof(status_str), "%d", status);
        httpd_resp_set_status(req, status_str);
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handle_stop(httpd_req_t *req)
{
    struct ReplyGuard {
        DevCommandReply *reply = nullptr;
        ~ReplyGuard()
        {
            if (reply) {
                reply->Release();
            }
        }
    };

    DevCommandReply *reply = DevCommandReply::CreateForDevCommand();
    if (!reply) {
        return send_json(req, 500, false, "alloc failed");
    }
    ReplyGuard reply_guard{reply};

    DevCommand *cmd = new DevCommand();
    cmd->kind = DevCommandKind::StopUploadedWasm;
    cmd->reply = reply;

    const HostEvent ev = MakeDevCommandEvent((int32_t)(esp_timer_get_time() / 1000), cmd);
    if (!host_event_loop_enqueue(ev, pdMS_TO_TICKS(100))) {
        reply->Release(); // release host-event-loop ref
        delete cmd;
        return send_json(req, 500, false, "event queue not ready");
    }

    if (xSemaphoreTake(reply->done, pdMS_TO_TICKS(15000)) != pdTRUE) {
        return send_json(req, 500, false, "timeout");
    }

    if (reply->result == kWasmOk) {
        return send_json(req, 200, true, "stopped");
    }
    return send_json(req, 500, false, reply->message);
}

static esp_err_t handle_run(httpd_req_t *req)
{
    if (req->content_len <= 0) {
        return send_json(req, 400, false, "empty body");
    }
    if ((size_t)req->content_len > kMaxWasmUploadBytes) {
        return send_json(req, 413, false, "payload too large");
    }

    uint8_t *buf = (uint8_t *)heap_caps_malloc((size_t)req->content_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        buf = (uint8_t *)heap_caps_malloc((size_t)req->content_len, MALLOC_CAP_8BIT);
    }
    if (!buf) {
            return send_json(req, 500, false, "alloc failed");
    }

    int remaining = req->content_len;
    int offset = 0;
    while (remaining > 0) {
        int ret = httpd_req_recv(req, (char *)(buf + offset), remaining);
        if (ret <= 0) {
            heap_caps_free(buf);
            return send_json(req, 400, false, "recv failed");
        }
        remaining -= ret;
        offset += ret;
    }

    char args_value[192] = {};
    size_t query_len = httpd_req_get_url_query_len(req);
    if (query_len > 0 && query_len < 256) {
        char query[256] = {};
        if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
            httpd_query_key_value(query, "args", args_value, sizeof(args_value));
        }
    }

    char *args_heap = nullptr;
    if (args_value[0] != '\0') {
        args_heap = (char *)heap_caps_malloc(strlen(args_value) + 1, MALLOC_CAP_8BIT);
        if (args_heap) {
            strcpy(args_heap, args_value);
        }
    }

    struct ReplyGuard {
        DevCommandReply *reply = nullptr;
        ~ReplyGuard()
        {
            if (reply) {
                reply->Release();
            }
        }
    };

    DevCommandReply *reply = DevCommandReply::CreateForDevCommand();
    if (!reply) {
        if (args_heap) {
            heap_caps_free(args_heap);
        }
        heap_caps_free(buf);
        return send_json(req, 500, false, "alloc failed");
    }
    ReplyGuard reply_guard{reply};

    DevCommand *cmd = new DevCommand();
    cmd->kind = DevCommandKind::RunUploadedWasm;
    cmd->wasm_bytes = buf;
    cmd->wasm_len = (size_t)req->content_len;
    cmd->args = args_heap;
    cmd->reply = reply;

    const HostEvent ev = MakeDevCommandEvent((int32_t)(esp_timer_get_time() / 1000), cmd);
    if (!host_event_loop_enqueue(ev, pdMS_TO_TICKS(100))) {
        reply->Release(); // release host-event-loop ref
        delete cmd;
        if (args_heap) {
            heap_caps_free(args_heap);
        }
        heap_caps_free(buf);
        return send_json(req, 500, false, "event queue not ready");
    }

    if (xSemaphoreTake(reply->done, pdMS_TO_TICKS(20000)) != pdTRUE) {
        return send_json(req, 500, false, "timeout");
    }

    if (reply->result == kWasmOk) {
        return send_json(req, 200, true, "running");
    }
    return send_json(req, 500, false, reply->message);
}

struct SseTaskArgs {
    httpd_req_t *req = nullptr;
};

static esp_err_t sse_send_event(httpd_req_t *req, const char *line)
{
    if (!req || !line) {
        return ESP_ERR_INVALID_ARG;
    }

    char buf[kLogLineMax + 16] = {};
    snprintf(buf, sizeof(buf), "data: %s\n\n", line);
    return httpd_resp_send_chunk(req, buf, HTTPD_RESP_USE_STRLEN);
}

static void sse_task(void *arg)
{
    SseTaskArgs *ctx = static_cast<SseTaskArgs *>(arg);
    httpd_req_t *req = ctx ? ctx->req : nullptr;
    char (*backlog)[kLogLineMax] = nullptr;
    char (*lines)[kLogLineMax] = nullptr;

    if (req) {
        uint32_t last_seq = 0;

        // Allocate SSE buffers in internal memory. We've seen PSRAM heap corruption
        // crash inside `heap_caps_malloc()` here, so avoid touching SPIRAM for SSE.
        const int caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
        backlog = static_cast<char (*)[kLogLineMax]>(
            heap_caps_malloc(sizeof(*backlog) * kSseBacklogLines, caps));
        if (!backlog) {
            backlog = static_cast<char (*)[kLogLineMax]>(
                heap_caps_malloc(sizeof(*backlog) * kSseBacklogLines, MALLOC_CAP_8BIT));
        }
        lines = static_cast<char (*)[kLogLineMax]>(heap_caps_malloc(sizeof(*lines) * 8, caps));
        if (!lines) {
            lines = static_cast<char (*)[kLogLineMax]>(
                heap_caps_malloc(sizeof(*lines) * 8, MALLOC_CAP_8BIT));
        }

        if (!backlog || !lines) {
            goto done;
        }

        memset(backlog, 0, sizeof(*backlog) * kSseBacklogLines);
        memset(lines, 0, sizeof(*lines) * 8);
    
        int backlog_count = 0;
        {
            std::lock_guard<std::mutex> lock(g_state.mutex);
            ensure_logs_allocated_locked();
            if (!g_state.logs) {
                goto done;
            }
            const size_t to_send = (g_state.log_count > (size_t)kSseBacklogLines)
                ? (size_t)kSseBacklogLines
                : g_state.log_count;
            const size_t start = (g_state.log_count >= to_send)
                ? (g_state.log_head + (g_state.log_count - to_send)) % kLogCapacity
                : g_state.log_head;

            for (size_t i = 0; i < to_send; i++) {
                const size_t idx = (start + i) % kLogCapacity;
                const LogEntry &e = g_state.logs[idx];
                if (backlog_count < kSseBacklogLines) {
                    snprintf(backlog[backlog_count], sizeof(backlog[backlog_count]), "%s", e.line);
                    backlog_count++;
                }
                if (e.seq > last_seq) {
                    last_seq = e.seq;
                }
            }
        }

            for (int i = 0; i < backlog_count; i++) {
            if (sse_send_event(req, backlog[i]) != ESP_OK) {
                goto done;
            }
        }

        int idle_loops = 0;
        while (true) {
            memset(lines, 0, sizeof(*lines) * 8);
            int count = 0;
            bool did_send = false;

            {
                std::lock_guard<std::mutex> lock(g_state.mutex);
                if (!g_state.logs) {
                    goto done;
                }
                for (size_t i = 0; i < g_state.log_count && count < 8; i++) {
                    const size_t idx = (g_state.log_head + i) % kLogCapacity;
                    const LogEntry &e = g_state.logs[idx];
                    if (e.seq <= last_seq) {
                        continue;
                    }
                    snprintf(lines[count], sizeof(lines[count]), "%s", e.line);
                    last_seq = e.seq;
                    count++;
                }
            }

            for (int i = 0; i < count; i++) {
                if (sse_send_event(req, lines[i]) != ESP_OK) {
                    goto done;
                }
                did_send = true;
            }

            if (!did_send) {
                idle_loops++;
                if (idle_loops >= 50) {
                    if (httpd_resp_send_chunk(req, ": ping\n\n", HTTPD_RESP_USE_STRLEN) != ESP_OK) {
                        goto done;
                    }
                    idle_loops = 0;
                }
            }
            else {
                idle_loops = 0;
            }

            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

done:
    if (backlog) {
        heap_caps_free(backlog);
    }
    if (lines) {
        heap_caps_free(lines);
    }
    if (req) {
        (void)httpd_resp_send_chunk(req, nullptr, 0);
        httpd_req_async_handler_complete(req);
    }
    delete ctx;
    vTaskDelete(nullptr);
}

static esp_err_t handle_logs_sse(httpd_req_t *req)
{
    char ua[128] = {};
    const size_t ua_len = httpd_req_get_hdr_value_len(req, "User-Agent");
    if (ua_len > 0 && ua_len < sizeof(ua)) {
        (void)httpd_req_get_hdr_value_str(req, "User-Agent", ua, sizeof(ua));
    }
    ESP_LOGI(kTag, "SSE connect (ua=%s)", ua[0] ? ua : "(none)");

    httpd_req_t *async_req = nullptr;
    esp_err_t err = httpd_req_async_handler_begin(req, &async_req);
    if (err != ESP_OK) {
        return err;
    }

    httpd_resp_set_type(async_req, "text/event-stream");
    httpd_resp_set_hdr(async_req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(async_req, "Connection", "keep-alive");

    SseTaskArgs *ctx = new SseTaskArgs();
    ctx->req = async_req;

    BaseType_t ok = xTaskCreate(sse_task, "dev_sse", kSseTaskStack, ctx, 5, nullptr);
    if (ok != pdPASS) {
        httpd_req_async_handler_complete(async_req);
        delete ctx;
        return ESP_FAIL;
    }

    return ESP_OK;
}

struct StartupContext {
    bool using_softap = false;
    bool started_softap = false;
    char url[96] = {};
    char ap_ssid[33] = {};
    char ap_password[17] = {};
};

struct StartTaskArgs {
    uint32_t generation = 0;
};

static esp_err_t start_httpd(httpd_handle_t *out_server)
{
    if (!out_server) {
        return ESP_ERR_INVALID_ARG;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = kPort;
    config.lru_purge_enable = true;
    config.max_uri_handlers = 8;

    httpd_handle_t server = nullptr;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        return err;
    }

    httpd_uri_t root = {};
    root.uri = "/";
    root.method = HTTP_GET;
    root.handler = handle_root;

    httpd_uri_t run = {};
    run.uri = "/api/run";
    run.method = HTTP_POST;
    run.handler = handle_run;

    httpd_uri_t stop = {};
    stop.uri = "/api/stop";
    stop.method = HTTP_POST;
    stop.handler = handle_stop;

    httpd_uri_t status = {};
    status.uri = "/api/status";
    status.method = HTTP_GET;
    status.handler = handle_status;

    httpd_uri_t logs = {};
    logs.uri = "/api/logs";
    logs.method = HTTP_GET;
    logs.handler = handle_logs_sse;

    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &run);
    httpd_register_uri_handler(server, &stop);
    httpd_register_uri_handler(server, &status);
    httpd_register_uri_handler(server, &logs);

    *out_server = server;
    return ESP_OK;
}

static esp_err_t start_wifi_and_url(StartupContext *ctx)
{
    if (!ctx) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = wifi::init_once();
    if (err != ESP_OK) {
        return err;
    }

    if (ENABLE_MDNS && MDNS_HOSTNAME && MDNS_HOSTNAME[0] != '\0') {
        (void)wifi::set_hostname(MDNS_HOSTNAME);
    }

    // Try to load WiFi settings from SD card
    settings_service::WifiSettings wifi_settings = {};
    err = settings_service::get_wifi_settings(&wifi_settings);

    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Failed to load WiFi settings, will use softap");
    }

    // Try to join network if WiFi settings are configured
    if (wifi_settings.configured) {
        if (!wifi::sta_has_ip()) {
            wifi::StaCredentials creds = {};
            creds.ssid = wifi_settings.ssid;
            creds.password = wifi_settings.password;
            creds.persist_to_flash = false;

            wifi::StaJoinOptions opts = {};
            opts.timeout_ms = 15000;
            opts.max_retries = 5;
            opts.wait_for_ip = true;

            err = wifi::sta_join(creds, opts);
            if (err != ESP_OK) {
                ESP_LOGW(kTag, "STA join failed, falling back to softap");
                // Fall through to softap setup
                wifi_settings.configured = false;
            }
        }

        if (wifi_settings.configured && wifi::sta_has_ip()) {
            esp_netif_ip_info_t ip = {};
            err = wifi::get_sta_ip_info(&ip);
            if (err != ESP_OK) {
                return err;
            }

            char ip_str[32] = {};
            format_ip4(ip.ip, ip_str, sizeof(ip_str));
            snprintf(ctx->url, sizeof(ctx->url), "http://%s:%u/", ip_str, (unsigned)kPort);

            ctx->using_softap = false;
            ctx->started_softap = false;
            ctx->ap_ssid[0] = '\0';
            ctx->ap_password[0] = '\0';
            return ESP_OK;
        }
    }

    // Fallback: check if already connected via STA
    if (wifi::sta_has_ip()) {
        esp_netif_ip_info_t ip = {};
        err = wifi::get_sta_ip_info(&ip);
        if (err != ESP_OK) {
            return err;
        }

        char ip_str[32] = {};
        format_ip4(ip.ip, ip_str, sizeof(ip_str));
        snprintf(ctx->url, sizeof(ctx->url), "http://%s:%u/", ip_str, (unsigned)kPort);

        ctx->using_softap = false;
        ctx->started_softap = false;
        ctx->ap_ssid[0] = '\0';
        ctx->ap_password[0] = '\0';
        return ESP_OK;
    }

    uint8_t mac[6] = {};
    err = wifi::get_sta_mac(mac);
    if (err != ESP_OK) {
        return err;
    }

    snprintf(ctx->ap_ssid, sizeof(ctx->ap_ssid), "PaperPortal-DEV-%02X%02X%02X", mac[3], mac[4], mac[5]);
    random_password(ctx->ap_password);

    err = wifi::start_softap(ctx->ap_ssid, ctx->ap_password);
    if (err != ESP_OK) {
        return err;
    }
    ctx->started_softap = true;

    esp_netif_ip_info_t ip = {};
    err = wifi::get_softap_ip_info(&ip);
    if (err != ESP_OK) {
        return err;
    }

    char ip_str[32] = {};
    format_ip4(ip.ip, ip_str, sizeof(ip_str));
    snprintf(ctx->url, sizeof(ctx->url), "http://%s:%u/", ip_str, (unsigned)kPort);

    ctx->using_softap = true;
    ctx->started_softap = true;
    return ESP_OK;
}

static void maybe_init_mdns(void)
{
    if (!ENABLE_MDNS) {
        ESP_LOGI(kTag, "mDNS disabled");
        return;
    }

    if (!MDNS_HOSTNAME || MDNS_HOSTNAME[0] == '\0') {
        ESP_LOGW(kTag, "mDNS enabled but MDNS_HOSTNAME is empty");
        return;
    }

    esp_err_t err = wifi::start_mdns_http(kPort, MDNS_HOSTNAME, "paperportal-devserver");
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "wifi::start_mdns_http failed (%s)", esp_err_to_name(err));
    }
}

static bool start_attempt_still_active_locked(uint32_t generation)
{
    return g_state.lifecycle == ServerLifecycle::Starting && !g_state.start_cancel_requested
        && g_state.start_generation == generation;
}

static void finalize_start_failure(uint32_t generation, const char *reason)
{
    std::lock_guard<std::mutex> lock(g_state.mutex);
    if (g_state.start_generation != generation || g_state.lifecycle != ServerLifecycle::Starting) {
        return;
    }
    g_state.lifecycle = ServerLifecycle::Stopped;
    g_state.start_cancel_requested = false;
    g_state.start_task = nullptr;
    g_state.using_softap = false;
    g_state.started_softap = false;
    g_state.url[0] = '\0';
    g_state.ap_ssid[0] = '\0';
    g_state.ap_password[0] = '\0';
    set_server_error_locked(reason);
    if (reason && reason[0] != '\0') {
        char line[kLogLineMax] = {};
        snprintf(line, sizeof(line), "devserver error: %s", reason);
        log_append_locked(line);
    }
}

static void finalize_start_success(uint32_t generation, httpd_handle_t server, const StartupContext &ctx)
{
    bool active = false;
    {
        std::lock_guard<std::mutex> lock(g_state.mutex);
        active = start_attempt_still_active_locked(generation);
        if (active) {
            g_state.server = server;
            g_state.lifecycle = ServerLifecycle::Running;
            g_state.start_cancel_requested = false;
            g_state.start_task = nullptr;
            g_state.using_softap = ctx.using_softap;
            g_state.started_softap = ctx.started_softap;
            snprintf(g_state.url, sizeof(g_state.url), "%s", ctx.url);
            snprintf(g_state.ap_ssid, sizeof(g_state.ap_ssid), "%s", ctx.ap_ssid);
            snprintf(g_state.ap_password, sizeof(g_state.ap_password), "%s", ctx.ap_password);
            set_server_error_locked(nullptr);
            log_append_locked("devserver: started");
        }
    }
    if (active) {
        return;
    }

    if (server) {
        httpd_stop(server);
    }
    if (ctx.started_softap) {
        wifi::stop_softap();
    }
}

static void start_task(void *arg)
{
    StartTaskArgs *args = static_cast<StartTaskArgs *>(arg);
    const uint32_t generation = args ? args->generation : 0;
    delete args;

    StartupContext ctx = {};
    httpd_handle_t server = nullptr;

    esp_err_t err = start_wifi_and_url(&ctx);
    if (err != ESP_OK) {
        finalize_start_failure(generation, "wifi setup failed");
        vTaskDelete(nullptr);
        return;
    }

    bool canceled = false;
    {
        std::lock_guard<std::mutex> lock(g_state.mutex);
        if (!start_attempt_still_active_locked(generation)) {
            g_state.start_task = nullptr;
            canceled = true;
        }
    }
    if (canceled) {
        if (ctx.started_softap) {
            wifi::stop_softap();
        }
        vTaskDelete(nullptr);
        return;
    }

    err = start_httpd(&server);
    if (err != ESP_OK) {
        if (ctx.started_softap) {
            wifi::stop_softap();
        }
        finalize_start_failure(generation, "httpd_start failed");
        vTaskDelete(nullptr);
        return;
    }

    maybe_init_mdns();
    finalize_start_success(generation, server, ctx);
    vTaskDelete(nullptr);
}

} // namespace

esp_err_t start(void)
{
    uint32_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(g_state.mutex);
        if (g_state.lifecycle == ServerLifecycle::Running || g_state.lifecycle == ServerLifecycle::Starting) {
            return ESP_OK;
        }

        g_state.lifecycle = ServerLifecycle::Starting;
        g_state.start_cancel_requested = false;
        g_state.start_generation += 1;
        generation = g_state.start_generation;
        g_state.server = nullptr;
        g_state.using_softap = false;
        g_state.started_softap = false;
        g_state.url[0] = '\0';
        g_state.ap_ssid[0] = '\0';
        g_state.ap_password[0] = '\0';
        set_server_error_locked(nullptr);
    }

    StartTaskArgs *args = new StartTaskArgs();
    if (!args) {
        finalize_start_failure(generation, "start task alloc failed");
        return ESP_ERR_NO_MEM;
    }
    args->generation = generation;

    TaskHandle_t task = nullptr;
    BaseType_t ok = xTaskCreate(start_task, "dev_start", kStartTaskStack, args, 5, &task);
    if (ok != pdPASS) {
        delete args;
        finalize_start_failure(generation, "start task create failed");
        return ESP_FAIL;
    }

    {
        std::lock_guard<std::mutex> lock(g_state.mutex);
        if (g_state.start_generation == generation && g_state.lifecycle == ServerLifecycle::Starting) {
            g_state.start_task = task;
        }
    }
    return ESP_OK;
}

esp_err_t stop(void)
{
    httpd_handle_t server = nullptr;
    bool started_softap = false;
    bool should_log_stop = false;

    {
        std::lock_guard<std::mutex> lock(g_state.mutex);
        if (g_state.lifecycle == ServerLifecycle::Stopped) {
            return ESP_OK;
        }

        should_log_stop = true;
        g_state.start_cancel_requested = true;
        g_state.start_generation += 1;
        g_state.lifecycle = ServerLifecycle::Stopped;

        server = g_state.server;
        started_softap = g_state.started_softap;

        g_state.server = nullptr;
        g_state.start_task = nullptr;
        g_state.using_softap = false;
        g_state.started_softap = false;
        g_state.url[0] = '\0';
        g_state.ap_ssid[0] = '\0';
        g_state.ap_password[0] = '\0';
    }

    if (server) {
        httpd_stop(server);
    }

    if (started_softap) {
        wifi::stop_softap();
    }

    if (should_log_stop) {
        std::lock_guard<std::mutex> lock(g_state.mutex);
        log_append_locked("devserver: stopped");
    }
    return ESP_OK;
}

bool is_running(void)
{
    std::lock_guard<std::mutex> lock(g_state.mutex);
    return lifecycle_is_running_locked();
}

bool is_starting(void)
{
    std::lock_guard<std::mutex> lock(g_state.mutex);
    return lifecycle_is_starting_locked();
}

int get_url(char *out, size_t out_len)
{
    if (!out && out_len != 0) {
        return kWasmErrInvalidArgument;
    }
    std::lock_guard<std::mutex> lock(g_state.mutex);
    const size_t len = strnlen(g_state.url, sizeof(g_state.url));
    if (out_len == 0) {
        return 0;
    }
    const size_t to_copy = (len < (out_len - 1)) ? len : (out_len - 1);
    memcpy(out, g_state.url, to_copy);
    out[to_copy] = '\0';
    return (int)to_copy;
}

int get_ap_ssid(char *out, size_t out_len)
{
    if (!out && out_len != 0) {
        return kWasmErrInvalidArgument;
    }
    std::lock_guard<std::mutex> lock(g_state.mutex);
    const size_t len = strnlen(g_state.ap_ssid, sizeof(g_state.ap_ssid));
    if (out_len == 0) {
        return 0;
    }
    const size_t to_copy = (len < (out_len - 1)) ? len : (out_len - 1);
    memcpy(out, g_state.ap_ssid, to_copy);
    out[to_copy] = '\0';
    return (int)to_copy;
}

int get_ap_password(char *out, size_t out_len)
{
    if (!out && out_len != 0) {
        return kWasmErrInvalidArgument;
    }
    std::lock_guard<std::mutex> lock(g_state.mutex);
    const size_t len = strnlen(g_state.ap_password, sizeof(g_state.ap_password));
    if (out_len == 0) {
        return 0;
    }
    const size_t to_copy = (len < (out_len - 1)) ? len : (out_len - 1);
    memcpy(out, g_state.ap_password, to_copy);
    out[to_copy] = '\0';
    return (int)to_copy;
}

int get_last_error(char *out, size_t out_len)
{
    if (!out && out_len != 0) {
        return kWasmErrInvalidArgument;
    }
    std::lock_guard<std::mutex> lock(g_state.mutex);
    const size_t len = strnlen(g_state.last_server_error, sizeof(g_state.last_server_error));
    if (out_len == 0) {
        return 0;
    }
    const size_t to_copy = (len < (out_len - 1)) ? len : (out_len - 1);
    memcpy(out, g_state.last_server_error, to_copy);
    out[to_copy] = '\0';
    return (int)to_copy;
}

void log_push(const char *line)
{
    std::lock_guard<std::mutex> lock(g_state.mutex);
    log_append_locked(line);
}

void log_pushf(const char *fmt, ...)
{
    char buf[kLogLineMax] = {};
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    log_push(buf);
}

void notify_uploaded_started(void)
{
    std::lock_guard<std::mutex> lock(g_state.mutex);
    g_state.uploaded_running = true;
    g_state.uploaded_crashed = false;
    g_state.crash_reason[0] = '\0';
    log_append_locked("uploaded app: started");
}

void notify_uploaded_stopped(void)
{
    std::lock_guard<std::mutex> lock(g_state.mutex);
    g_state.uploaded_running = false;
    log_append_locked("uploaded app: stopped");
}

void notify_uploaded_crashed(const char *reason)
{
    std::lock_guard<std::mutex> lock(g_state.mutex);
    if (!g_state.uploaded_running) {
        return;
    }
    g_state.uploaded_crashed = true;
    snprintf(g_state.crash_reason, sizeof(g_state.crash_reason), "%s", reason ? reason : "");
    char line[kLogLineMax] = {};
    snprintf(line, sizeof(line), "uploaded app: crashed: %s", reason ? reason : "");
    log_append_locked(line);
}

void notify_server_error(const char *reason)
{
    std::lock_guard<std::mutex> lock(g_state.mutex);
    set_server_error_locked(reason);
    if (reason && reason[0] != '\0') {
        char line[kLogLineMax] = {};
        snprintf(line, sizeof(line), "devserver error: %s", reason);
        log_append_locked(line);
    }
}

bool uploaded_app_is_running(void)
{
    std::lock_guard<std::mutex> lock(g_state.mutex);
    return g_state.uploaded_running;
}

bool uploaded_app_is_crashed(void)
{
    std::lock_guard<std::mutex> lock(g_state.mutex);
    return g_state.uploaded_crashed;
}

int get_last_crash_reason(char *out, size_t out_len)
{
    if (!out && out_len != 0) {
        return kWasmErrInvalidArgument;
    }
    std::lock_guard<std::mutex> lock(g_state.mutex);
    const size_t len = strnlen(g_state.crash_reason, sizeof(g_state.crash_reason));
    if (out_len == 0) {
        return 0;
    }
    const size_t to_copy = (len < (out_len - 1)) ? len : (out_len - 1);
    memcpy(out, g_state.crash_reason, to_copy);
    out[to_copy] = '\0';
    return (int)to_copy;
}

} // namespace devserver
