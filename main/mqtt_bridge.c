#include "mqtt_bridge.h"

#include "esp_log.h"

#define TAG "mqtt"

esp_err_t mqtt_bridge_init(void)
{
    ESP_LOGI(TAG, "MQTT bridge init (stub)");
    // TODO: configure MQTT client for Fibaro integration
    return ESP_OK;
}

void mqtt_bridge_publish_snapshot(const sensor_snapshot_t *snapshot)
{
    if (!snapshot) {
        return;
    }
    ESP_LOGI(TAG, "Publishing to MQTT: water=%.2fC level=%.1fcm air=%.2fC hum=%.1f%% pres=%.1fhPa batt=%.2fV",
             snapshot->water_temp_c,
             snapshot->sea_level_cm,
             snapshot->air_temp_c,
             snapshot->humidity_percent,
             snapshot->air_pressure_hpa,
             snapshot->battery_voltage);
}
