#include "google_bridge.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

#define DEFAULT_AGENT_ID "sea-monitor"
#define DEFAULT_DEVICE_NAME "Sea monitor"

static const char *TAG = "google";

static SemaphoreHandle_t s_mutex;
static sensor_snapshot_t s_last_snapshot;
static int64_t s_last_update_us;
static bool s_has_snapshot;
static char s_device_id[64];
static char s_friendly_name[64];
static bool s_automation_enabled = true;

static void set_identity_defaults(void)
{
    strlcpy(s_device_id, "sea.monitor", sizeof(s_device_id));
    strlcpy(s_friendly_name, DEFAULT_DEVICE_NAME, sizeof(s_friendly_name));
}

static void update_identity_from_config(const measurement_config_t *cfg)
{
    const char *name = (cfg && cfg->device_name[0]) ? cfg->device_name : "sea";
    char sanitized[CONFIG_STORE_MAX_NAME_LEN];
    strlcpy(sanitized, name, sizeof(sanitized));
    // Ensure only lowercase letters/digits/-/_ to keep ID predictable.
    for (size_t i = 0; i < sizeof(sanitized); ++i) {
        char c = sanitized[i];
        if (c == '\0') {
            break;
        }
        bool ok = (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') ||
                  c == '-' || c == '_';
        if (!ok) {
            if (c >= 'A' && c <= 'Z') {
                sanitized[i] = (char)(c + 32);
            } else {
                sanitized[i] = '-';
            }
        }
    }
    if (sanitized[0] == '\0') {
        strlcpy(sanitized, "sea", sizeof(sanitized));
    }
    snprintf(s_device_id, sizeof(s_device_id), "sea.%s", sanitized);
    snprintf(s_friendly_name, sizeof(s_friendly_name), "Sea monitor (%s)", sanitized);
}

static void log_snapshot(const sensor_snapshot_t *snapshot)
{
    ESP_LOGI(TAG,
             "Snapshot: water=%.2fC level=%.1fcm air=%.2fC hum=%.1f%% pres=%.1fhPa batt=%.2fV",
             snapshot->water_temp_c,
             snapshot->sea_level_cm,
             snapshot->air_temp_c,
             snapshot->humidity_percent,
             snapshot->air_pressure_hpa,
             snapshot->battery_voltage);
}

esp_err_t google_bridge_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    set_identity_defaults();
    s_has_snapshot = false;
    s_last_update_us = 0;
    ESP_LOGI(TAG, "Google bridge ready (local REST placeholder)");
    return ESP_OK;
}

void google_bridge_publish_snapshot(const sensor_snapshot_t *snapshot)
{
    if (!snapshot) {
        return;
    }
    log_snapshot(snapshot);
    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        s_last_snapshot = *snapshot;
        s_last_update_us = esp_timer_get_time();
        s_has_snapshot = true;
        xSemaphoreGive(s_mutex);
    }
}

bool google_bridge_get_last_snapshot(sensor_snapshot_t *out, int64_t *timestamp_us)
{
    if (!out || !s_mutex) {
        return false;
    }
    bool has_snapshot = false;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (s_has_snapshot) {
            *out = s_last_snapshot;
            if (timestamp_us) {
                *timestamp_us = s_last_update_us;
            }
            has_snapshot = true;
        }
        xSemaphoreGive(s_mutex);
    }
    return has_snapshot;
}

void google_bridge_update_config(const measurement_config_t *cfg)
{
    update_identity_from_config(cfg);
}

const char *google_bridge_device_id(void)
{
    return s_device_id[0] ? s_device_id : "sea.monitor";
}

const char *google_bridge_friendly_name(void)
{
    return s_friendly_name[0] ? s_friendly_name : DEFAULT_DEVICE_NAME;
}

const char *google_bridge_agent_user_id(void)
{
    return DEFAULT_AGENT_ID;
}

void google_bridge_set_automation_enabled(bool enabled)
{
    s_automation_enabled = enabled;
    ESP_LOGI(TAG, "Automation %s", enabled ? "enabled" : "disabled");
}

bool google_bridge_is_automation_enabled(void)
{
    return s_automation_enabled;
}
