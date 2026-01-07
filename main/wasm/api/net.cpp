#include <inttypes.h>
#include <stdint.h>
#include <string.h>
#include <mutex>

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "wasm_export.h"

#include "../api.h"
#include "services/wifi_service.h"
#include "wasm/app_contract.h"
#include "errors.h"
#include "marshalling.h"

namespace {

constexpr const char *kTag = "wasm_api_net";

constexpr int kMaxScanRecords = 16;
constexpr size_t kWifiRecordSize = 37;

struct WifiRecord
{
    int32_t rssi;
    char ssid[33];
};

static wifi_ap_record_t g_scan_ap_info[kMaxScanRecords];
static WifiRecord g_scan_records[kMaxScanRecords];
static int g_scan_count = 0;
static WifiRecord g_best_record = { -100, {0} };
static bool g_scan_in_progress = false;
static bool g_scan_task_started = false;
static std::mutex g_scan_mutex;

static int32_t wifi_init_once(void)
{
    esp_err_t err = wifi::init_once();
    if (err != ESP_OK) {
        wasm_api_set_last_error(kWasmErrInternal, "wifi_init_once: wifi::init_once failed");
        return kWasmErrInternal;
    }
    return kWasmOk;
}

static int32_t wifi_ensure_started(void)
{
    (void)wifi_init_once();
    esp_err_t err = wifi::ensure_sta_started();
    if (err != ESP_OK) {
        wasm_api_set_last_error(kWasmErrInternal, "wifi_ensure_started: wifi::ensure_sta_started failed");
        return kWasmErrInternal;
    }
    return kWasmOk;
}

static void sort_records(WifiRecord *records, int count)
{
    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            if (records[j].rssi > records[i].rssi) {
                WifiRecord tmp = records[i];
                records[i] = records[j];
                records[j] = tmp;
            }
        }
    }
}

static void update_scan_results(const WifiRecord *records, int count, const WifiRecord &best)
{
    std::lock_guard<std::mutex> lock(g_scan_mutex);
    g_scan_count = 0;
    g_best_record = best;
    if (count <= 0) {
        return;
    }
    const int to_copy = (count < kMaxScanRecords) ? count : kMaxScanRecords;
    for (int i = 0; i < to_copy; i++) {
        g_scan_records[i] = records[i];
    }
    g_scan_count = to_copy;
}

static void write_wifi_record(const WifiRecord &rec, uint8_t *out)
{
    memcpy(out, &rec.rssi, sizeof(rec.rssi));
    memcpy(out + 4, rec.ssid, sizeof(rec.ssid));
}

static void wifi_scan_task(void *arg)
{
    (void)arg;
    while (true) {
        {
            std::lock_guard<std::mutex> lock(g_scan_mutex);
            g_scan_in_progress = true;
        }

        if (wifi_ensure_started() == kWasmOk) {
            memset(g_scan_ap_info, 0, sizeof(g_scan_ap_info));

            uint16_t number = kMaxScanRecords;
            uint16_t ap_count = 0;

            esp_err_t ret = esp_wifi_scan_start(NULL, true);
            if (ret == ESP_OK) {
                ret = esp_wifi_scan_get_ap_num(&ap_count);
            }
            if (ret == ESP_OK) {
                ret = esp_wifi_scan_get_ap_records(&number, g_scan_ap_info);
            }

            WifiRecord local_records[kMaxScanRecords] = {};
            int local_count = 0;
            WifiRecord best = { -100, {0} };

            if (ret == ESP_OK) {
                for (int i = 0; i < number && local_count < kMaxScanRecords; i++) {
                    const char *ssid = (const char *)g_scan_ap_info[i].ssid;
                    if (!ssid || ssid[0] == '\0') {
                        continue;
                    }
                    WifiRecord rec = {};
                    rec.rssi = g_scan_ap_info[i].rssi;
                    strncpy(rec.ssid, ssid, sizeof(rec.ssid) - 1);
                    rec.ssid[sizeof(rec.ssid) - 1] = '\0';

                    local_records[local_count++] = rec;

                    if (rec.rssi > best.rssi) {
                        best = rec;
                    }
                }

                sort_records(local_records, local_count);
                update_scan_results(local_records, local_count, best);
            }
            else {
                ESP_LOGW(kTag, "wifi scan failed: %s", esp_err_to_name(ret));
            }
        }
        else {
            ESP_LOGW(kTag, "wifi scan skipped: wifi not ready");
        }

        {
            std::lock_guard<std::mutex> lock(g_scan_mutex);
            g_scan_in_progress = false;
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

int32_t netIsReady(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return wifi::sta_has_ip() ? 1 : 0;
}

int32_t netConnect(wasm_exec_env_t exec_env)
{
    (void)exec_env;

    (void)wifi_init_once();
    esp_err_t err = wifi::sta_connect();
    if (err != ESP_OK) {
        wasm_api_set_last_error(kWasmErrInternal, "netConnect: wifi::sta_connect failed");
        return kWasmErrInternal;
    }

    return kWasmOk;
}

int32_t netDisconnect(wasm_exec_env_t exec_env)
{
    (void)exec_env;

    esp_err_t err = wifi::sta_disconnect();
    if (err != ESP_OK) {
        wasm_api_set_last_error(kWasmErrInternal, "netDisconnect: wifi::sta_disconnect failed");
        return kWasmErrInternal;
    }
    return kWasmOk;
}

int32_t netGetIpv4(wasm_exec_env_t exec_env, uint8_t *out_ptr, int32_t out_len)
{
    if (!out_ptr && out_len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "netGetIpv4: out_ptr is null");
        return kWasmErrInvalidArgument;
    }

    if (out_len < 4) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "netGetIpv4: out_len too small (need 4)");
        return kWasmErrInvalidArgument;
    }

    esp_netif_t *netif_sta = wifi::netif_sta();
    if (!netif_sta) {
        memset(out_ptr, 0, 4);
        return 0;
    }

    esp_netif_ip_info_t ip_info;
    esp_err_t err = esp_netif_get_ip_info(netif_sta, &ip_info);
    if (err != ESP_OK) {
        memset(out_ptr, 0, 4);
        return 0;
    }

    memcpy(out_ptr, &ip_info.ip.addr, 4);
    return 4;
}

int32_t netResolveIpv4(wasm_exec_env_t exec_env, const char *host, uint8_t *out_ptr, int32_t out_len)
{
    if (!host) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "netResolveIpv4: host is null");
        return kWasmErrInvalidArgument;
    }

    if (!out_ptr && out_len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "netResolveIpv4: out_ptr is null");
        return kWasmErrInvalidArgument;
    }

    if (out_len < 4) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "netResolveIpv4: out_len too small (need 4)");
        return kWasmErrInvalidArgument;
    }

    struct addrinfo hints;
    struct addrinfo *result;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int rc = lwip_getaddrinfo(host, NULL, &hints, &result);
    if (rc != 0 || !result) {
        memset(out_ptr, 0, 4);
        return 0;
    }

    struct sockaddr_in *addr_in = (struct sockaddr_in *)result->ai_addr;
    memcpy(out_ptr, &addr_in->sin_addr.s_addr, 4);
    lwip_freeaddrinfo(result);
    return 4;
}

int32_t wifiGetMode(wasm_exec_env_t exec_env)
{
    (void)exec_env;

    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (err == ESP_OK) {
        return (int32_t)mode;
    }

    if (err == ESP_ERR_WIFI_NOT_INIT || err == ESP_ERR_WIFI_NOT_STARTED) {
        // Treat "not initialized" as "off" for a simple mode check.
        return (int32_t)WIFI_MODE_NULL;
    }

    wasm_api_set_last_error(kWasmErrInternal, "wifiGetMode: esp_wifi_get_mode failed");
    return kWasmErrInternal;
}

int32_t wifiScanStart(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    if (g_scan_task_started) {
        return kWasmOk;
    }

    int32_t rc = wifi_ensure_started();
    if (rc != kWasmOk) {
        return rc;
    }

    BaseType_t ok = xTaskCreate(wifi_scan_task, "wifi_scan", 1024 * 4, NULL, 5, NULL);
    if (ok != pdPASS) {
        wasm_api_set_last_error(kWasmErrInternal, "wifiScanStart: task create failed");
        return kWasmErrInternal;
    }

    g_scan_task_started = true;
    return kWasmOk;
}

int32_t wifiScanIsRunning(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    std::lock_guard<std::mutex> lock(g_scan_mutex);
    return g_scan_in_progress ? 1 : 0;
}

int32_t wifiScanGetBest(wasm_exec_env_t exec_env, uint8_t *out_ptr, int32_t out_len)
{
    (void)exec_env;
    if (!out_ptr && out_len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "wifiScanGetBest: out_ptr is null");
        return kWasmErrInvalidArgument;
    }
    if (out_len < (int32_t)kWifiRecordSize) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "wifiScanGetBest: out_len too small (need 37)");
        return kWasmErrInvalidArgument;
    }

    WifiRecord best = { -100, {0} };
    {
        std::lock_guard<std::mutex> lock(g_scan_mutex);
        best = g_best_record;
    }
    write_wifi_record(best, out_ptr);
    return (int32_t)kWifiRecordSize;
}

int32_t wifiScanGetCount(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    std::lock_guard<std::mutex> lock(g_scan_mutex);
    return g_scan_count;
}

int32_t wifiScanGetRecord(wasm_exec_env_t exec_env, int32_t index, uint8_t *out_ptr, int32_t out_len)
{
    (void)exec_env;
    if (index < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "wifiScanGetRecord: index < 0");
        return kWasmErrInvalidArgument;
    }
    if (!out_ptr && out_len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "wifiScanGetRecord: out_ptr is null");
        return kWasmErrInvalidArgument;
    }
    if (out_len < (int32_t)kWifiRecordSize) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "wifiScanGetRecord: out_len too small (need 37)");
        return kWasmErrInvalidArgument;
    }

    WifiRecord rec = { -100, {0} };
    {
        std::lock_guard<std::mutex> lock(g_scan_mutex);
        if (index >= g_scan_count) {
            wasm_api_set_last_error(kWasmErrInvalidArgument, "wifiScanGetRecord: index out of range");
            return kWasmErrInvalidArgument;
        }
        rec = g_scan_records[index];
    }

    write_wifi_record(rec, out_ptr);
    return (int32_t)kWifiRecordSize;
}

/* clang-format off */
#define REG_NATIVE_FUNC(funcName, signature) \
    { #funcName, (void *)funcName, signature, NULL }

static NativeSymbol g_net_native_symbols[] = {
    REG_NATIVE_FUNC(netIsReady, "()i"),
    REG_NATIVE_FUNC(netConnect, "()i"),
    REG_NATIVE_FUNC(netDisconnect, "()i"),
    REG_NATIVE_FUNC(netGetIpv4, "(*i)i"),
    REG_NATIVE_FUNC(netResolveIpv4, "(**i)i"),
    REG_NATIVE_FUNC(wifiGetMode, "()i"),
    REG_NATIVE_FUNC(wifiScanStart, "()i"),
    REG_NATIVE_FUNC(wifiScanIsRunning, "()i"),
    REG_NATIVE_FUNC(wifiScanGetBest, "(*i)i"),
    REG_NATIVE_FUNC(wifiScanGetCount, "()i"),
    REG_NATIVE_FUNC(wifiScanGetRecord, "(i*i)i"),
};
/* clang-format on */

} // namespace

bool wasm_api_register_net(void)
{
    const uint32_t count = sizeof(g_net_native_symbols) / sizeof(g_net_native_symbols[0]);
    bool ok = wasm_runtime_register_natives("m5_net", g_net_native_symbols, count);
    if (!ok) {
        ESP_LOGE(kTag, "Failed to register m5_net natives (count=%" PRIu32 ")", count);
        wasm_api_set_last_error(kWasmErrInternal, "register_net: wasm_runtime_register_natives failed");
    }
    return ok;
}
