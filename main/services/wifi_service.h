#pragma once

#include "esp_netif.h"
#include "esp_wifi_types.h"

/**
 * @file wifi_service.h
 *
 * @brief Wi-Fi service (ESP-IDF).
 *
 * This header declares a small, app-facing API for:
 * - Checking current Wi-Fi status (STA/AP state, IP info, current SSID/RSSI when connected).
 * - Subscribing to Wi-Fi lifecycle events.
 * - Joining an existing Wi-Fi network (STA) using saved or explicit credentials.
 * - Starting/stopping a SoftAP network for local access.
 * - Running a synchronous Wi-Fi scan.
 *
 * @note Exclusive mode: this service supports either STA or SoftAP at a time. Starting one will stop the other; it does
 * not keep `WIFI_MODE_APSTA` running.
 *
 * @warning Callback context: subscriber callbacks are invoked from the ESP-IDF default event loop task (ESP event handler
 * context). Keep callbacks fast and non-blocking; enqueue work to another task/queue for anything heavy.
 *
 * @note NVS prerequisite: if you persist STA credentials (`WIFI_STORAGE_FLASH`, the default), the app must initialize NVS
 * (call `nvs_flash_init()` during boot) before using `sta_join()`/`sta_join_saved()`.
 */

namespace wifi {

/**
 * @brief High-level active mode as observed by `esp_wifi_get_mode()`.
 *
 * This is restricted to STA/AP/off for this service (it does not actively maintain `WIFI_MODE_APSTA`).
 */
enum class Mode : uint8_t {
    kOff = 0, ///< Wi-Fi driver is stopped or mode is `WIFI_MODE_NULL`.
    kSta = 1, ///< Station mode (`WIFI_MODE_STA`).
    kAp = 2,  ///< SoftAP mode (`WIFI_MODE_AP`).
};

/**
 * @brief Simplified STA lifecycle state tracked from Wi-Fi/IP events.
 */
enum class StaState : uint8_t {
    kStopped = 0,         ///< STA is not started (service is not in STA mode).
    kDisconnected = 1,    ///< STA is started but not associated.
    kConnecting = 2,      ///< A connect attempt is in progress.
    kConnectedNoIp = 3,   ///< Associated to an AP but no IPv4 address yet.
    kConnectedHasIp = 4,  ///< Associated and has an IPv4 address.
};

/**
 * @brief SoftAP lifecycle state.
 */
enum class ApState : uint8_t {
    kStopped = 0, ///< AP is not running.
    kRunning = 1, ///< AP is running.
};

/**
 * @brief Event kinds delivered to subscribers.
 */
enum class EventKind : uint8_t {
    kStaStart = 1,          ///< STA interface started (`WIFI_EVENT_STA_START`).
    kStaConnected = 2,      ///< STA associated to an AP (`WIFI_EVENT_STA_CONNECTED`).
    kStaDisconnected = 3,   ///< STA disconnected (`WIFI_EVENT_STA_DISCONNECTED`).
    kStaGotIp = 4,          ///< STA obtained an IPv4 address (`IP_EVENT_STA_GOT_IP`).
    kStaLostIp = 5,         ///< STA lost its IPv4 address (`IP_EVENT_STA_LOST_IP`).

    kApStart = 6,           ///< SoftAP started (`WIFI_EVENT_AP_START`).
    kApStop = 7,            ///< SoftAP stopped (`WIFI_EVENT_AP_STOP`).
    kApStaConnected = 8,    ///< A station connected to the SoftAP (`WIFI_EVENT_AP_STACONNECTED`).
    kApStaDisconnected = 9, ///< A station disconnected from the SoftAP (`WIFI_EVENT_AP_STADISCONNECTED`).
};

/**
 * @brief Wi-Fi service event payload delivered to subscribers.
 *
 * Only some members of `data` are meaningful for a given `kind`.
 *
 * Valid `data` fields by `kind`:
 * - `EventKind::kStaDisconnected`: `data.sta_disconnected.reason`
 * - `EventKind::kStaGotIp`:        `data.sta_got_ip.ip`
 * - `EventKind::kApStaConnected`:  `data.ap_sta.mac`, `data.ap_sta.aid`
 * - `EventKind::kApStaDisconnected`: `data.ap_sta.mac`, `data.ap_sta.aid`
 *
 * For other kinds, the `data` union contents are unspecified.
 */
struct Event {
    EventKind kind; ///< The event kind.
    int32_t now_ms; ///< Timestamp in milliseconds since boot (from `esp_timer_get_time()` / 1000).
    union {
        struct {
            wifi_err_reason_t reason; ///< Disconnect reason (`wifi_err_reason_t`).
        } sta_disconnected; ///< Payload for `EventKind::kStaDisconnected`.
        struct {
            esp_netif_ip_info_t ip; ///< IPv4 info (`ip`, `netmask`, `gw`) for STA.
        } sta_got_ip; ///< Payload for `EventKind::kStaGotIp`.
        struct {
            uint8_t mac[6]; ///< Station MAC address.
            uint8_t aid;    ///< Station AID.
        } ap_sta; ///< Payload for `EventKind::kApStaConnected` and `EventKind::kApStaDisconnected`.
    } data; ///< Kind-specific payload.
};

/**
 * @brief Subscriber callback type for Wi-Fi service events.
 *
 * @param event Event payload.
 * @param user_ctx Opaque pointer provided to `subscribe()`.
 *
 * @warning Invoked from ESP event handler context. Do not block.
 */
using EventCallback = void (*)(const Event &event, void *user_ctx);

/**
 * @brief Subscription handle returned by `subscribe()`.
 *
 * Store this handle and pass it to `unsubscribe()` to remove the callback.
 */
struct Subscription {
    int32_t id = 0; ///< Internal subscription identifier. A value of 0 means "invalid".
};

/**
 * @brief Best-effort snapshot of Wi-Fi status.
 *
 * Some fields may be unset depending on mode/state.
 */
struct Status {
    Mode mode;     ///< Current Wi-Fi mode as reported by ESP-IDF (STA/AP/off).
    StaState sta;  ///< Current STA lifecycle state tracked by the service.
    ApState ap;    ///< Current SoftAP lifecycle state tracked by the service.

    bool sta_has_ip;             ///< True if STA netif has a non-zero IPv4 address.
    esp_netif_ip_info_t sta_ip;  ///< STA IPv4 configuration (valid when `sta_has_ip == true`).

    bool ap_has_ip;              ///< True if AP netif has a non-zero IPv4 address.
    esp_netif_ip_info_t ap_ip;   ///< AP IPv4 configuration (valid when `ap_has_ip == true`).

    char sta_ssid[33]; ///< Connected SSID (NUL-terminated). Empty string when not connected/unknown.
    int32_t sta_rssi;   ///< Connected RSSI in dBm. Set to -127 when unknown.
};

/**
 * @brief Credentials for joining a Wi-Fi network as a STA.
 */
struct StaCredentials {
    const char *ssid = nullptr;     ///< SSID (required; must be non-null and non-empty).
    const char *password = nullptr; ///< Password (nullptr/"" for open networks).

    /**
     * @brief Whether to persist credentials to flash.
     *
     * When true, the service configures `WIFI_STORAGE_FLASH` (requires NVS initialized).
     * When false, credentials are configured in RAM only via `WIFI_STORAGE_RAM`.
     */
    bool persist_to_flash = true;
};

/**
 * @brief Options controlling STA connect/join operations.
 */
struct StaJoinOptions {
    /**
     * @brief Connect timeout in milliseconds.
     *
     * - If `timeout_ms == 0`, the call returns immediately after starting the connection attempt (async).
     *   Use events (`subscribe`) or polling (`get_status`) to observe progress.
     * - If `timeout_ms > 0`, the call blocks waiting for the chosen condition (`wait_for_ip`).
     */
    int32_t timeout_ms = 0;

    /**
     * @brief Maximum number of retries while a connect attempt is in progress.
     *
     * - `max_retries == 0`: do not auto-retry.
     * - `max_retries > 0`: retry up to that many disconnects during a connect attempt.
     * - `max_retries == -1`: retry indefinitely.
     */
    int32_t max_retries = 0;

    /**
     * @brief When blocking, wait for an IPv4 address rather than association.
     *
     * - `true`: wait for `EventKind::kStaGotIp`
     * - `false`: wait for `EventKind::kStaConnected` (or `EventKind::kStaGotIp`)
     */
    bool wait_for_ip = true;
};

/**
 * @brief Configuration for starting a SoftAP network.
 *
 * Notes:
 * - `ssid` is required.
 * - If `password` is null/empty, the AP is open (`WIFI_AUTH_OPEN`).
 * - If `password` is non-empty, ESP-IDF enforces WPA password rules (typically >= 8 chars); invalid values will cause
 *   `ap_start()` to fail from `esp_wifi_set_config()`.
 */
struct SoftApConfig {
    const char *ssid = nullptr;     ///< SSID (required; must be non-null and non-empty).
    const char *password = nullptr; ///< Password (nullptr/"" for open AP).
    uint8_t channel = 1;            ///< Wi-Fi channel (1-13; 0 treated as default 1).
    uint8_t max_connections = 4;    ///< Max simultaneous client connections (0 treated as default 4).
    bool hidden = false;            ///< If true, SSID is hidden (`ssid_hidden = 1`).
};

/**
 * @brief One Wi-Fi scan result record.
 */
struct ScanRecord {
    int32_t rssi;            ///< RSSI in dBm.
    wifi_auth_mode_t authmode; ///< Authentication mode (`wifi_auth_mode_t`).
    char ssid[33];           ///< SSID (NUL-terminated).
};

/**
 * @brief Initialize the Wi-Fi service once.
 *
 * Safe to call multiple times.
 *
 * This function initializes the ESP netif system, ensures the default event loop exists, initializes the Wi-Fi driver,
 * creates/looks up the default STA netif, and registers Wi-Fi/IP event handlers used by this service.
 *
 * @return `ESP_OK` on success; otherwise an ESP-IDF error code.
 */
esp_err_t init_once(void);

/**
 * @brief Subscribe to Wi-Fi service events.
 *
 * @param cb Callback invoked for Wi-Fi lifecycle events.
 * @param user_ctx Opaque pointer forwarded to `cb` (may be nullptr).
 * @param out_sub Output subscription handle.
 *
 * @return `ESP_OK` on success;
 * `ESP_ERR_INVALID_ARG` if `cb` or `out_sub` is null;
 * `ESP_ERR_NO_MEM` if the subscriber table is full;
 * otherwise an ESP-IDF error code.
 */
esp_err_t subscribe(EventCallback cb, void *user_ctx, Subscription *out_sub);

/**
 * @brief Unsubscribe a previously registered callback.
 *
 * @param sub Subscription handle returned by `subscribe()`.
 *
 * @return `ESP_OK` on success;
 * `ESP_ERR_INVALID_ARG` if `sub.id` is invalid;
 * `ESP_ERR_NOT_FOUND` if the subscription is not present.
 */
esp_err_t unsubscribe(Subscription sub);

/**
 * @brief Get a best-effort snapshot of Wi-Fi status.
 *
 * @param out Output status structure to populate.
 *
 * @return `ESP_OK` on success; `ESP_ERR_INVALID_ARG` if `out` is null; otherwise an ESP-IDF error code.
 */
esp_err_t get_status(Status *out);

/**
 * @brief Ensure STA mode is started (but does not configure credentials or connect).
 *
 * This switches the Wi-Fi driver to `WIFI_MODE_STA` (stopping SoftAP if needed) and starts the driver.
 *
 * @return `ESP_OK` on success; otherwise an ESP-IDF error code.
 */
esp_err_t sta_start(void);

/**
 * @brief Connect as a STA using credentials already stored by ESP-IDF (NVS / Wi-Fi storage).
 *
 * The call can be used in async mode (`opts.timeout_ms == 0`) or blocking mode (`opts.timeout_ms > 0`).
 *
 * @param opts Connection options (timeout/retry behavior).
 *
 * @return In async mode: `ESP_OK` if the connect attempt was started, otherwise an ESP-IDF error code.
 * In blocking mode: `ESP_OK` if the desired condition was met before the timeout; `ESP_ERR_TIMEOUT` on timeout;
 * `ESP_FAIL` if a disconnect occurred and retries were exhausted; otherwise an ESP-IDF error code.
 */
esp_err_t sta_join_saved(const StaJoinOptions &opts);

/**
 * @brief Configure STA credentials and connect.
 *
 * The call can be used in async mode (`opts.timeout_ms == 0`) or blocking mode (`opts.timeout_ms > 0`).
 *
 * @param creds Credentials to configure.
 * @param opts Connection options (timeout/retry behavior).
 *
 * @return In async mode: `ESP_OK` if the connect attempt was started, otherwise an ESP-IDF error code.
 * In blocking mode: `ESP_OK` if the desired condition was met before the timeout; `ESP_ERR_TIMEOUT` on timeout;
 * `ESP_FAIL` if a disconnect occurred and retries were exhausted; otherwise an ESP-IDF error code.
 */
esp_err_t sta_join(const StaCredentials &creds, const StaJoinOptions &opts);

/**
 * @brief Disconnect STA (does not stop the Wi-Fi driver).
 *
 * @return `ESP_OK` on success; otherwise an ESP-IDF error code.
 */
esp_err_t sta_disconnect(void);

/**
 * @brief Start a SoftAP network (exclusive with STA).
 *
 * This switches the Wi-Fi driver to `WIFI_MODE_AP` (stopping STA if needed), applies the provided configuration, and
 * starts the driver.
 *
 * @param cfg SoftAP configuration.
 *
 * @return `ESP_OK` on success; `ESP_ERR_INVALID_ARG` if required fields are missing/invalid; otherwise an ESP-IDF error
 * code.
 */
esp_err_t ap_start(const SoftApConfig &cfg);

/**
 * @brief Stop the Wi-Fi driver / SoftAP.
 *
 * @return `ESP_OK` on success; otherwise an ESP-IDF error code.
 */
esp_err_t ap_stop(void);

/**
 * @brief Set hostname on the Wi-Fi netifs (STA/AP if created).
 *
 * @param hostname Hostname string (must be non-null and non-empty).
 *
 * @return `ESP_OK` on success; `ESP_ERR_INVALID_ARG` if `hostname` is null/empty; otherwise an ESP-IDF error code.
 */
esp_err_t set_hostname(const char *hostname);

/**
 * @brief Start (once) the ESP-IDF mDNS responder and advertise an HTTP service.
 *
 * This is an idempotent helper: if mDNS has already been started by this service, it returns `ESP_OK`.
 *
 * @param port HTTP port to advertise via mDNS.
 * @param hostname mDNS hostname (required, without ".local").
 * @param instance_name Optional instance name for discovery UIs (nullptr/empty uses `hostname`).
 *
 * @return `ESP_OK` on success;
 * `ESP_ERR_INVALID_ARG` for invalid arguments; otherwise an ESP-IDF error code.
 */
esp_err_t start_mdns_http(uint16_t port, const char *hostname, const char *instance_name = nullptr);

/**
 * @brief Stop the ESP-IDF mDNS responder if started by this service.
 *
 * @return `ESP_OK` on success; otherwise an ESP-IDF error code.
 */
esp_err_t stop_mdns(void);

/**
 * @brief Check whether mDNS has been started by this service.
 */
bool mdns_is_running(void);

/**
 * @brief Run a blocking Wi-Fi scan in STA mode.
 *
 * This ensures STA mode is active and performs a blocking scan via `esp_wifi_scan_start(..., true)`.
 *
 * @param out_records Output array of records, or nullptr.
 * @param inout_count In/out record count:
 * - If `out_records == nullptr`, `*inout_count` is set to the number of APs found and the function returns `ESP_OK`.
 * - If `out_records != nullptr`, `*inout_count` must contain the array capacity on input, and is set to the number of
 *   entries written on output.
 *
 * @return `ESP_OK` on success;
 * `ESP_ERR_INVALID_ARG` for invalid arguments;
 * `ESP_ERR_INVALID_STATE` if Wi-Fi is currently in AP/APSTA mode;
 * otherwise an ESP-IDF error code.
 */
esp_err_t scan_sync(ScanRecord *out_records, size_t *inout_count);

/**
 * @brief Fetch the STA MAC address.
 *
 * @param mac_out Output buffer for the MAC address (6 bytes).
 *
 * @return `ESP_OK` on success; `ESP_ERR_INVALID_ARG` if `mac_out` is null; otherwise an ESP-IDF error code.
 */
esp_err_t get_sta_mac(uint8_t mac_out[6]);

/**
 * @brief Legacy-compatible alias for `sta_start()`.
 *
 * @return `ESP_OK` on success; otherwise an ESP-IDF error code.
 */
esp_err_t ensure_sta_started(void);

/**
 * @brief Start STA mode (if needed) and call `esp_wifi_connect()` using the current STA configuration.
 *
 * This does not change credentials; to configure credentials, use `sta_join()`.
 *
 * @return `ESP_OK` if the connect attempt was started; otherwise an ESP-IDF error code.
 */
esp_err_t sta_connect(void);

/**
 * @brief Check whether STA is connected and has an IPv4 address.
 *
 * @return True if STA is in `StaState::kConnectedHasIp`, false otherwise.
 */
bool sta_has_ip(void);

/**
 * @brief Get current STA IPv4 configuration from the STA netif.
 *
 * @param out_ip Output IPv4 configuration (`ip`, `netmask`, `gw`).
 *
 * @return `ESP_OK` on success;
 * `ESP_ERR_INVALID_ARG` if `out_ip` is null;
 * `ESP_ERR_INVALID_STATE` if the STA netif is not available;
 * otherwise an ESP-IDF error code.
 */
esp_err_t get_sta_ip_info(esp_netif_ip_info_t *out_ip);

/**
 * @brief Get the STA netif handle owned/managed by this service.
 *
 * @return STA netif pointer, or nullptr if the service has not been initialized.
 */
esp_netif_t *netif_sta(void);

/**
 * @brief Legacy-compatible helper to start a WPA/WPA2 SoftAP with a required password.
 *
 * @param ssid SoftAP SSID (required).
 * @param password SoftAP password (required; must be non-empty; ESP-IDF enforces WPA password rules).
 *
 * @return `ESP_OK` on success; `ESP_ERR_INVALID_ARG` for invalid arguments; otherwise an ESP-IDF error code.
 */
esp_err_t start_softap(const char *ssid, const char *password);

/**
 * @brief Legacy-compatible alias for `ap_stop()`.
 *
 * @return `ESP_OK` on success; otherwise an ESP-IDF error code.
 */
esp_err_t stop_softap(void);

/**
 * @brief Check whether the SoftAP is running.
 *
 * @return True if the SoftAP state is `ApState::kRunning`, false otherwise.
 */
bool softap_is_running(void);

/**
 * @brief Get current SoftAP IPv4 configuration from the AP netif.
 *
 * @param out_ip Output IPv4 configuration (`ip`, `netmask`, `gw`).
 *
 * @return `ESP_OK` on success;
 * `ESP_ERR_INVALID_ARG` if `out_ip` is null;
 * `ESP_ERR_INVALID_STATE` if the AP netif is not available;
 * otherwise an ESP-IDF error code.
 */
esp_err_t get_softap_ip_info(esp_netif_ip_info_t *out_ip);

/**
 * @brief Get the SoftAP netif handle owned/managed by this service.
 *
 * @return AP netif pointer, or nullptr if a SoftAP netif has not been created.
 */
esp_netif_t *netif_softap(void);

} // namespace wifi
