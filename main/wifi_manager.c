#include "wifi_manager.h"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_netif_ip_addr.h"
#include "mdns.h"
#include "lwip/inet.h"
#include <stdio.h>
#include <string.h>

#define TAG "wifi_mgr"

static measurement_config_t s_cached_config;
static bool s_wifi_started = false;
static wifi_status_t s_status;
static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;
static bool s_mdns_ready = false;

static const char *device_hostname(void)
{
    return s_cached_config.device_name[0] ? s_cached_config.device_name : "sea";
}

static void ensure_mdns_started(void)
{
    if (s_mdns_ready) {
        return;
    }
    if (!s_sta_netif && !s_ap_netif) {
        return;
    }
    esp_err_t err = mdns_init();
    if (err == ESP_ERR_INVALID_STATE) {
        // Already running from a previous boot; continue.
        err = ESP_OK;
    }
    ESP_ERROR_CHECK(err);
    if (s_sta_netif) {
        err = mdns_register_netif(s_sta_netif);
        if (err != ESP_ERR_INVALID_STATE) {
            ESP_ERROR_CHECK(err);
        }
    }
    if (s_ap_netif) {
        err = mdns_register_netif(s_ap_netif);
        if (err != ESP_ERR_INVALID_STATE) {
            ESP_ERROR_CHECK(err);
        }
    }
    if (!mdns_service_exists("_http", "_tcp", NULL)) {
        err = mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE && err != ESP_ERR_INVALID_ARG) {
            ESP_ERROR_CHECK(err);
        }
    }
    s_mdns_ready = true;
}

static void update_mdns(void)
{
    ensure_mdns_started();
    if (!s_mdns_ready) {
        return;
    }
    const char *hostname = device_hostname();
    ESP_ERROR_CHECK(mdns_hostname_set(hostname));

    char instance[64];
    snprintf(instance, sizeof(instance), "%s panel", hostname);
    esp_err_t err = mdns_instance_name_set(instance);
    if (err != ESP_OK && err != ESP_ERR_INVALID_ARG) {
        ESP_ERROR_CHECK(err);
    }
    err = mdns_service_instance_name_set("_http", "_tcp", instance);
    if (err != ESP_OK && err != ESP_ERR_INVALID_ARG && err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err);
    }
}

static void update_hostnames(void)
{
    const char *name = device_hostname();
    if (s_sta_netif) {
        esp_netif_set_hostname(s_sta_netif, name);
    }
    if (s_ap_netif) {
        esp_netif_set_hostname(s_ap_netif, name);
    }
    update_mdns();
}

static void reconnect_sta_if_ready(void)
{
    if (!s_wifi_started) {
        return;
    }
    if (s_cached_config.wifi_ssid[0] == '\0') {
        (void)esp_wifi_disconnect();
        s_status.sta_connected = false;
        memset(&s_status.sta_ip, 0, sizeof(s_status.sta_ip));
        return;
    }
    (void)esp_wifi_disconnect();
    esp_err_t err = esp_wifi_connect();
    if (err == ESP_ERR_WIFI_CONN || err == ESP_ERR_WIFI_STATE) {
        ESP_LOGW(TAG, "STA already connecting (%s)", esp_err_to_name(err));
        return;
    }
    ESP_ERROR_CHECK(err);
}

static void refresh_ap_ip(void)
{
    if (!s_ap_netif) {
        memset(&s_status.ap_ip, 0, sizeof(s_status.ap_ip));
        return;
    }
    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(s_ap_netif, &info) == ESP_OK) {
        s_status.ap_ip = info.ip;
    }
}

static void handle_wifi_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "STA start");
                if (s_cached_config.wifi_ssid[0] != '\0') {
                    esp_wifi_connect();
                }
                break;
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "Connected to %s", s_cached_config.wifi_ssid);
                s_status.sta_connected = true;
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGW(TAG, "STA disconnected, retrying");
                s_status.sta_connected = false;
                memset(&s_status.sta_ip, 0, sizeof(s_status.sta_ip));
                if (s_cached_config.wifi_ssid[0] != '\0') {
                    esp_wifi_connect();
                }
                break;
            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        s_status.sta_ip = event->ip_info.ip;
        s_status.sta_connected = true;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

static void make_ap_ssid(char *out_ssid, size_t len)
{
    if (!out_ssid || len == 0) {
        return;
    }
    if (s_cached_config.device_name[0] != '\0') {
        snprintf(out_ssid, len, "SeaMonitor-%s", s_cached_config.device_name);
    } else {
        strlcpy(out_ssid, "SeaMonitor", len);
    }
}

static void apply_wifi_config(void)
{
    wifi_config_t sta_cfg = {0};
    if (s_cached_config.wifi_ssid[0] != '\0') {
        strlcpy((char *)sta_cfg.sta.ssid, s_cached_config.wifi_ssid, sizeof(sta_cfg.sta.ssid));
        strlcpy((char *)sta_cfg.sta.password, s_cached_config.wifi_password, sizeof(sta_cfg.sta.password));
        sta_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    }

    wifi_config_t ap_cfg = {0};
    make_ap_ssid((char *)ap_cfg.ap.ssid, sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.ssid_len = strlen((char *)ap_cfg.ap.ssid);
    ap_cfg.ap.channel = 1;
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.authmode = WIFI_AUTH_OPEN;

    if (s_cached_config.wifi_password[0] != '\0') {
        strlcpy((char *)ap_cfg.ap.password, s_cached_config.wifi_password, sizeof(ap_cfg.ap.password));
        if (strlen((char *)ap_cfg.ap.password) >= 8) {
            ap_cfg.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
        }
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    if (s_cached_config.wifi_ssid[0] != '\0') {
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    }
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_LOGI(TAG, "AP SSID=%s", ap_cfg.ap.ssid);

    if (!s_wifi_started) {
        ESP_ERROR_CHECK(esp_wifi_start());
        s_wifi_started = true;
        refresh_ap_ip();
        return;
    }

    reconnect_sta_if_ready();
}

esp_err_t wifi_manager_init(const measurement_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    s_cached_config = *config;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &handle_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &handle_wifi_event, NULL));

    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();
    update_hostnames();

    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_cfg));

    apply_wifi_config();
    return ESP_OK;
}

void wifi_manager_update_config(const measurement_config_t *config)
{
    if (!config) {
        return;
    }
    s_cached_config = *config;
    update_hostnames();
    if (!s_wifi_started) {
        return;
    }
    apply_wifi_config();
}

wifi_status_t wifi_manager_get_status(void)
{
    refresh_ap_ip();
    return s_status;
}
