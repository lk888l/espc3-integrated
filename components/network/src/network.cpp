#include "c3/network.hpp"
#include "c3/pairing.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "app_task.hpp"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/queue.h"
#include "nvs.h"
#include "sdkconfig.h"
namespace c3::network
{
namespace
{
constexpr char tag[] = "network";
portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
Telemetry state;
std::atomic<bool> want_enabled{true}, connected{false}, associated{false}, associating{false},
    got_ip{false}, lost_ip{false}, want_sync{false};
std::atomic<int64_t> synchronized_at{-1};
std::atomic<uint32_t> association_epoch{0}, dhcp_epoch{0};
struct Credentials
{
    uint8_t version = 1;
    char ssid[33]{}, password[64]{};
};
Credentials credentials;
enum class ConfigMode { save, trial, reconnect };
struct ConfigCommand { Credentials next; ConfigMode mode; int64_t apply_after; };
std::atomic<bool> config_busy{false};
QueueHandle_t commands_queue = nullptr;
esp_netif_t* sta = nullptr;
esp_netif_t* ap = nullptr;
esp_event_handler_instance_t wifi_handler = nullptr, ip_handler = nullptr;
bool wifi_initialized = false, event_loop_owned = false, sntp_initialized = false,
     initialized = false;
template <class F>
void update(F fn)
{
    portENTER_CRITICAL(&state_lock);
    fn(state);
    portEXIT_CRITICAL(&state_lock);
}
void time_received(timeval*)
{
    synchronized_at.store(esp_timer_get_time());
    ESP_LOGI(tag, "SNTP time synchronized");
}
void event(void*, esp_event_base_t base, int32_t id, void* data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED)
    {
        ++association_epoch;
        associated = true;
        associating = false;
    }
    if ((base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) ||
        (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP))
    {
        if (base == WIFI_EVENT)
        {
            associated = false;
            associating = false;
        }
        connected = false;
        lost_ip = true;
        update(
            [](Telemetry& s)
            {
                s.station_connected = false;
                std::strcpy(s.sta_ip, "0.0.0.0");
            });
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
    {
        auto* ip = static_cast<ip_event_got_ip_t*>(data);
        char text[16];
        std::snprintf(text, sizeof(text), IPSTR, IP2STR(&ip->ip_info.ip));
        connected = true;
        associated = true;
        associating = false;
        dhcp_epoch = association_epoch.load();
        got_ip = true;
        update(
            [&](Telemetry& s)
            {
                s.station_connected = true;
                std::strcpy(s.sta_ip, text);
            });
    }
}
bool valid_credentials(const char* ssid, const char* password)
{
    if (!ssid || !password) return false;
    const auto ssid_size = std::strlen(ssid), password_size = std::strlen(password);
    return ssid_size > 0 && ssid_size <= 32 &&
           (password_size == 0 || (password_size >= 8 && password_size <= 63));
}
esp_err_t save_credentials(const Credentials& value)
{
    nvs_handle_t handle;
    auto result = nvs_open("network", NVS_READWRITE, &handle);
    if (result != ESP_OK) return result;
    result = nvs_set_blob(handle, "credentials", &value, sizeof(value));
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    return result;
}
esp_err_t configure_station()
{
    wifi_config_t config{};
    std::memcpy(config.sta.ssid, credentials.ssid, std::strlen(credentials.ssid));
    std::memcpy(config.sta.password, credentials.password, std::strlen(credentials.password));
    config.sta.threshold.authmode = credentials.password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    config.sta.pmf_cfg.capable = true;
    return esp_wifi_set_config(WIFI_IF_STA, &config);
}
class Worker final : public AppTask
{
   public:
    Worker() : AppTask("network", 4096, 3, 0) {}

   private:
    bool enabled = false;
    int64_t retry_at = 0, association_until = 0, dhcp_until = 0;
    uint32_t retry_ms = 1000;
    int64_t rssi_at = 0;
    Credentials previous{};
    int64_t trial_until = 0;
    uint32_t prior_association = 0;
    bool config_dirty = false;
    esp_err_t reconfigure()
    {
        if (enabled) esp_wifi_disconnect();
        connected = associated = associating = false;
        got_ip = lost_ip = false;
        association_until = dhcp_until = 0;
        retry_ms = 1000;
        retry_at = esp_timer_get_time() + 200000;
        update([](Telemetry& s) {
            s.station_connected = false;
            s.rssi = 0;
            std::strcpy(s.sta_ip, "0.0.0.0");
        });
        const auto result = enabled ? configure_station() : ESP_OK;
        config_dirty = result != ESP_OK;
        return result;
    }
    void finish_trial(ProvisionState result)
    {
        if (result != ProvisionState::saved)
        {
            credentials = previous;
            const auto err = reconfigure();
            if (err != ESP_OK) ESP_LOGE(tag, "restore station: %s", esp_err_to_name(err));
        }
        previous = {};
        trial_until = 0;
        update([&](Telemetry& s) {
            s.provisioning = result;
            s.station_configured = credentials.ssid[0] != 0;
        });
        config_busy = false;
    }
    void apply(const ConfigCommand& cmd)
    {
        if (cmd.mode == ConfigMode::save)
        {
            // Explicit USB setup may be performed offline. Keep the working link
            // and its configuration if the replacement cannot be made durable.
            const auto stored = save_credentials(cmd.next);
            if (stored == ESP_OK)
            {
                credentials = cmd.next;
                const auto err = reconfigure();
                if (err != ESP_OK) ESP_LOGE(tag, "station config: %s", esp_err_to_name(err));
            }
            else ESP_LOGE(tag, "credential save: %s", esp_err_to_name(stored));
            update([&](Telemetry& s) {
                s.provisioning = stored == ESP_OK ? ProvisionState::idle : ProvisionState::storage_error;
                s.station_configured = credentials.ssid[0] != 0;
            });
            config_busy = false;
            return;
        }
        previous = credentials;
        prior_association = association_epoch.load();
        trial_until = esp_timer_get_time() + 45000000;
        if (cmd.mode != ConfigMode::reconnect) credentials = cmd.next;
        const auto configured = reconfigure();
        if (configured != ESP_OK || !enabled) finish_trial(ProvisionState::failed);
    }
    void start_radio()
    {
        esp_err_t result = configure_station();
        if (result == ESP_OK) result = esp_wifi_start();
        if (result == ESP_OK)
        {
            enabled = true;
            config_dirty = false;
            retry_at = esp_timer_get_time() + 200000;
            retry_ms = 1000;
            association_until = dhcp_until = rssi_at = 0;
            update([](Telemetry& s) { s.wifi_enabled = true; });
            ESP_LOGI(tag, "Wi-Fi AP enabled; station credentials %s",
                     credentials.ssid[0] ? "configured" : "empty");
        }
        else
        {
            want_enabled = false;
            update([](Telemetry& s) { s.wifi_enabled = false; });
            ESP_LOGE(tag, "Wi-Fi start failed: %s", esp_err_to_name(result));
        }
    }
    void stop_radio()
    {
        const auto result = esp_wifi_stop();
        if (result != ESP_OK) ESP_LOGW(tag, "Wi-Fi stop: %s", esp_err_to_name(result));
        enabled = false;
        if (trial_until) finish_trial(ProvisionState::failed);
        pairing::revoke_all();
        connected = associated = associating = false;
        retry_at = association_until = dhcp_until = 0;
        update(
            [](Telemetry& s)
            {
                s.wifi_enabled = false;
                s.station_connected = false;
                std::strcpy(s.sta_ip, "0.0.0.0");
            });
    }
    void main() override
    {
        while (!should_exit())
        {
            if (want_enabled.load() != enabled)
            {
                if (want_enabled)
                    start_radio();
                else
                    stop_radio();
            }
            ConfigCommand next;
            if (xQueuePeek(commands_queue, &next, 0) == pdTRUE &&
                (!enabled || esp_timer_get_time() >= next.apply_after) &&
                xQueueReceive(commands_queue, &next, 0) == pdTRUE)
                apply(next);
            if (got_ip.exchange(false) && connected)
            {
                retry_at = 0;
                retry_ms = 1000;
                want_sync = true;
                if (trial_until && dhcp_epoch.load() != prior_association)
                {
                    wifi_ap_record_t actual{};
                    if (esp_wifi_sta_get_ap_info(&actual) == ESP_OK &&
                        std::strncmp(reinterpret_cast<const char*>(actual.ssid), credentials.ssid, 32) == 0)
                        finish_trial(save_credentials(credentials) == ESP_OK ? ProvisionState::saved
                                                                            : ProvisionState::storage_error);
                }
            }
            if (lost_ip.exchange(false) && !connected)
            {
                retry_at = esp_timer_get_time() + int64_t(retry_ms) * 1000;
                retry_ms = std::min<uint32_t>(retry_ms * 2, 60000);
            }
            const int64_t now = esp_timer_get_time();
            if (trial_until && now >= trial_until) finish_trial(ProvisionState::failed);
            if (associated && !connected && !dhcp_until) dhcp_until = now + 30000000;
            if (!associated || connected) dhcp_until = 0;
            if (connected) association_until = 0;
            if (enabled && ((associating && association_until && now >= association_until) ||
                            (dhcp_until && now >= dhcp_until)))
            {
                ESP_LOGW(tag, "station association/DHCP timeout; reconnecting");
                esp_wifi_disconnect();
                connected = associated = associating = false;
                association_until = dhcp_until = 0;
                retry_at = now + int64_t(retry_ms) * 1000;
            }
            if (enabled && credentials.ssid[0] && !connected && !associated && !associating &&
                retry_at && now >= retry_at)
            {
                // Set before calling the driver: an event may preempt this task.
                associating = true;
                auto result = config_dirty ? configure_station() : ESP_OK;
                config_dirty = result != ESP_OK;
                if (!config_dirty) result = esp_wifi_connect();
                retry_at = now + int64_t(retry_ms) * 1000;
                if (result == ESP_OK)
                    association_until = now + 15000000;
                else
                {
                    associating = false;
                    retry_ms = std::min<uint32_t>(retry_ms * 2, 60000);
                    if (result != ESP_ERR_WIFI_CONN)
                        ESP_LOGW(tag, "station connect: %s", esp_err_to_name(result));
                }
            }
            if (connected && now - rssi_at >= 1000000)
            {
                wifi_ap_record_t record{};
                if (esp_wifi_sta_get_ap_info(&record) == ESP_OK)
                    update([&](Telemetry& s) { s.rssi = record.rssi; });
                rssi_at = now;
            }
            if (want_sync.exchange(false) && connected)
            {
                const auto result = esp_netif_sntp_start();
                if (result != ESP_OK) ESP_LOGW(tag, "SNTP start: %s", esp_err_to_name(result));
            }
            vTaskDelay(pdMS_TO_TICKS(25));
        }
        if (enabled) stop_radio();
    }
};
Worker worker;
}  // namespace
esp_err_t initialize()
{
    if (initialized) return ESP_ERR_INVALID_STATE;
    auto result = esp_netif_init();
    if (result != ESP_OK) return result;
    result = esp_event_loop_create_default();
    if (result == ESP_OK)
        event_loop_owned = true;
    else if (result != ESP_ERR_INVALID_STATE)
        return result;
    sta = esp_netif_create_default_wifi_sta();
    ap = esp_netif_create_default_wifi_ap();
    if (!sta || !ap) return ESP_ERR_NO_MEM;
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    result = esp_wifi_init(&config);
    if (result != ESP_OK) return result;
    wifi_initialized = true;
    if ((result = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK) return result;
    if ((result = esp_wifi_set_mode(WIFI_MODE_APSTA)) != ESP_OK) return result;
    if ((result = esp_wifi_set_ps(WIFI_PS_NONE)) != ESP_OK) return result;
    uint8_t mac[6];
    if ((result = esp_read_mac(mac, ESP_MAC_WIFI_STA)) != ESP_OK) return result;
    char ssid[33], password[64]{};
    std::snprintf(ssid, sizeof(ssid), "C3GB-%02X%02X%02X", mac[3], mac[4], mac[5]);
    if ((result = esp_netif_set_hostname(sta, ssid)) != ESP_OK) return result;
    if (CONFIG_C3_AP_PASSWORD[0])
    {
        if (std::strlen(CONFIG_C3_AP_PASSWORD) < 8 || std::strlen(CONFIG_C3_AP_PASSWORD) > 63)
            return ESP_ERR_INVALID_ARG;
        std::strcpy(password, CONFIG_C3_AP_PASSWORD);
    }
    else
    {
        nvs_handle_t ap_store;
        if ((result = nvs_open("network", NVS_READWRITE, &ap_store)) != ESP_OK) return result;
        std::size_t size = sizeof(password);
        const auto loaded = nvs_get_str(ap_store, "ap_password", password, &size);
        if (loaded != ESP_OK || std::strlen(password) < 8 || std::strlen(password) > 63)
        {
            uint8_t random[12];
            esp_fill_random(random, sizeof(random));
            constexpr char alphabet[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
            std::memcpy(password, "C3-", 3);
            for (unsigned i = 0; i < 12; ++i) password[i + 3] = alphabet[random[i] & 31];
            password[15] = 0;
            result = nvs_set_str(ap_store, "ap_password", password);
            if (result == ESP_OK) result = nvs_commit(ap_store);
        }
        nvs_close(ap_store);
        if (result != ESP_OK) return result;
    }
    if (std::strlen(password) < 8 || std::strlen(password) > 63) return ESP_ERR_INVALID_ARG;
    update(
        [&](Telemetry& s)
        {
            s = {};
            s.wifi_enabled = false;
            std::strcpy(s.ap_ssid, ssid);
            std::strcpy(s.ap_password, password);
        });
    wifi_config_t ap_config{};
    std::memcpy(ap_config.ap.ssid, ssid, std::strlen(ssid));
    ap_config.ap.ssid_len = std::strlen(ssid);
    std::memcpy(ap_config.ap.password, password, std::strlen(password));
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap_config.ap.max_connection = 4;
    ap_config.ap.channel = 1;
    if ((result = esp_wifi_set_config(WIFI_IF_AP, &ap_config)) != ESP_OK) return result;
    credentials = {};
    nvs_handle_t handle;
    if (nvs_open("network", NVS_READONLY, &handle) == ESP_OK)
    {
        std::size_t size = sizeof(credentials);
        const auto read = nvs_get_blob(handle, "credentials", &credentials, &size);
        nvs_close(handle);
        if (read != ESP_OK || size != sizeof(credentials) || credentials.version != 1 ||
            credentials.ssid[32] || credentials.password[63] ||
            !valid_credentials(credentials.ssid, credentials.password))
            credentials = {};
    }
    if (!credentials.ssid[0] && CONFIG_C3_WIFI_SSID[0])
    {
        if (!valid_credentials(CONFIG_C3_WIFI_SSID, CONFIG_C3_WIFI_PASSWORD))
            return ESP_ERR_INVALID_ARG;
        std::strcpy(credentials.ssid, CONFIG_C3_WIFI_SSID);
        std::strcpy(credentials.password, CONFIG_C3_WIFI_PASSWORD);
    }
    update([](Telemetry& s) { s.station_configured = credentials.ssid[0] != 0; });
    if ((result = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event, nullptr,
                                                      &wifi_handler)) != ESP_OK)
        return result;
    if ((result = esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, event, nullptr,
                                                      &ip_handler)) != ESP_OK)
        return result;
    setenv("TZ", CONFIG_C3_TIMEZONE, 1);
    tzset();
    esp_sntp_config_t sntp{};
    sntp.start = false;
    sntp.num_of_servers = 2;
    sntp.servers[0] = CONFIG_C3_NTP_SERVER;
    sntp.servers[1] = CONFIG_C3_NTP_SERVER_BACKUP;
    sntp.sync_cb = time_received;
    if ((result = esp_netif_sntp_init(&sntp)) != ESP_OK) return result;
    sntp_initialized = true;
    commands_queue = xQueueCreate(1, sizeof(ConfigCommand));
    if (!commands_queue) return ESP_ERR_NO_MEM;
    want_enabled = true;
    synchronized_at = -1;
    connected = associated = associating = false;
    want_sync = false;
    got_ip = false;
    lost_ip = false;
    config_busy = false;
    if (!worker.start()) return ESP_ERR_NO_MEM;
    initialized = true;
    ESP_LOGI(tag, "AP SSID: %s; password is shown on the Network page", ssid);
    return ESP_OK;
}
bool deinitialize()
{
    if (!worker.stop()) return false;
    if (sntp_initialized)
    {
        esp_netif_sntp_deinit();
        sntp_initialized = false;
    }
    if (wifi_handler)
    {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_handler);
        wifi_handler = nullptr;
    }
    if (ip_handler)
    {
        esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID, ip_handler);
        ip_handler = nullptr;
    }
    if (wifi_initialized)
    {
        if (esp_wifi_deinit() != ESP_OK) return false;
        wifi_initialized = false;
    }
    if (sta)
    {
        esp_netif_destroy_default_wifi(sta);
        sta = nullptr;
    }
    if (ap)
    {
        esp_netif_destroy_default_wifi(ap);
        ap = nullptr;
    }
    if (event_loop_owned)
    {
        if (esp_event_loop_delete_default() != ESP_OK) return false;
        event_loop_owned = false;
    }
    if (commands_queue)
    {
        vQueueDelete(commands_queue);
        commands_queue = nullptr;
    }
    initialized = false;
    return true;
}
void set_enabled(bool enabled) { want_enabled = enabled; }
bool enabled_requested() { return want_enabled.load(); }
bool radio_running()
{
    portENTER_CRITICAL(&state_lock);
    const auto enabled = state.wifi_enabled;
    portEXIT_CRITICAL(&state_lock);
    return enabled;
}
static SubmitResult submit(const char* ssid, const char* password, ConfigMode mode)
{
    const bool reuse = mode == ConfigMode::reconnect;
    const bool trial = mode != ConfigMode::save;
    if (!reuse && !valid_credentials(ssid, password)) return SubmitResult::invalid;
    if (!commands_queue || config_busy.exchange(true)) return SubmitResult::busy;
    if (reuse && !snapshot().station_configured)
    {
        config_busy = false;
        return SubmitResult::unconfigured;
    }
    // Give the paired client time to receive its 202 on the current STA link.
    ConfigCommand cmd{{}, mode, esp_timer_get_time() + (trial ? 500000 : 0)};
    if (!reuse)
    {
        std::strcpy(cmd.next.ssid, ssid);
        std::strcpy(cmd.next.password, password);
    }
    if (trial) update([](Telemetry& s) { s.provisioning = ProvisionState::trying; });
    if (xQueueSend(commands_queue, &cmd, 0) == pdTRUE) return SubmitResult::queued;
    if (trial) update([](Telemetry& s) { s.provisioning = ProvisionState::failed; });
    config_busy = false;
    return SubmitResult::busy;
}
bool set_credentials(const char* ssid, const char* password)
{
    return submit(ssid, password, ConfigMode::save) == SubmitResult::queued;
}
SubmitResult try_credentials(const char* ssid, const char* password)
{
    return submit(ssid, password, ConfigMode::trial);
}
SubmitResult reconnect()
{
    return submit(nullptr, nullptr, ConfigMode::reconnect);
}
void sync_time() { want_sync = true; }
Telemetry snapshot()
{
    Telemetry result;
    portENTER_CRITICAL(&state_lock);
    result = state;
    portEXIT_CRITICAL(&state_lock);
    const auto at = synchronized_at.load();
    result.time_synced = at >= 0;
    if (at >= 0) result.sync_age_s = uint32_t((esp_timer_get_time() - at) / 1000000);
    const time_t now = std::time(nullptr);
    tm local{};
    localtime_r(&now, &local);
    result.time_valid = local.tm_year >= 124;
    if (result.time_valid)
    {
        std::strftime(result.time, sizeof(result.time), "%H:%M:%S", &local);
        std::strftime(result.date, sizeof(result.date), "%Y/%m/%d", &local);
    }
    return result;
}
}  // namespace c3::network
