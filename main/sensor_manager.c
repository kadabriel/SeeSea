#include "sensor_manager.h"

#include "bme280_sensor.h"
#include "battery_monitor.h"
#include "config_store.h"
#include "ds18b20_sensor.h"
#include "esp_check.h"
#include "esp_log.h"
#include "aht20_sensor.h"
#include "power_manager.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "ultrasonic_sensor.h"

#define TAG "sensor_mgr"
#define AIR_SENSOR_I2C_PORT I2C_NUM_0
#define AIR_SENSOR_SDA GPIO_NUM_21
#define AIR_SENSOR_SCL GPIO_NUM_22
#define WATER_SENSOR_PIN GPIO_NUM_4
#define ULTRASONIC_TRIG_PIN GPIO_NUM_27
#define ULTRASONIC_ECHO_PIN GPIO_NUM_27
#define SENSOR_POWER_STABILIZE_MS 50

static sensor_snapshot_t s_snapshot;
static bool s_air_sensor_ready = false;
static bool s_water_sensor_ready = false;
static bool s_ultra_ready = false;
static bool s_aht_ready = false;

esp_err_t sensor_manager_init(void)
{
    // Start med BME/BMP for å sikre I2C-bus er konfigurert, så init AHT20
    esp_err_t err = bme280_sensor_init(AIR_SENSOR_I2C_PORT, AIR_SENSOR_SDA, AIR_SENSOR_SCL);
    if (err == ESP_OK) {
        s_air_sensor_ready = true;
    } else {
        ESP_LOGW(TAG, "BME/BMP init failed (%s), pressure/hum stubbed", esp_err_to_name(err));
    }
    err = aht20_init(AIR_SENSOR_I2C_PORT, AIR_SENSOR_SDA, AIR_SENSOR_SCL);
    if (err == ESP_OK) {
        s_aht_ready = true;
    } else {
        ESP_LOGW(TAG, "AHT20 init failed (%s)", esp_err_to_name(err));
    }

    power_manager_set(POWER_DOMAIN_SENSOR_POD, true);
    vTaskDelay(pdMS_TO_TICKS(SENSOR_POWER_STABILIZE_MS));
    if (ds18b20_sensor_init(WATER_SENSOR_PIN) == ESP_OK) {
        s_water_sensor_ready = true;
    } else {
        ESP_LOGW(TAG, "DS18B20 init failed");
    }
    if (ultrasonic_sensor_init(ULTRASONIC_TRIG_PIN, ULTRASONIC_ECHO_PIN) == ESP_OK) {
        s_ultra_ready = true;
    } else {
        ESP_LOGW(TAG, "Ultrasonic init failed");
    }
    power_manager_set(POWER_DOMAIN_SENSOR_POD, false);

    if ((err = battery_monitor_init()) != ESP_OK) {
        ESP_LOGW(TAG, "Battery monitor init failed: %s", esp_err_to_name(err));
    }

    s_snapshot = (sensor_snapshot_t){
        .water_temp_c = 10.0f,
        .sea_level_cm = 0.0f,
        .air_temp_c = 5.0f,
        .humidity_percent = 80.0f,
        .air_pressure_hpa = 1013.25f,
        .battery_percent = 100.0f,
        .battery_voltage = 4.1f,
    };
    return ESP_OK;
}

void sensor_manager_trigger_sea_measurement(void)
{
    ESP_LOGI(TAG, "Sea measurement triggered");
    measurement_config_t cfg = config_store_get();
    power_manager_set(POWER_DOMAIN_SENSOR_POD, true);
    vTaskDelay(pdMS_TO_TICKS(SENSOR_POWER_STABILIZE_MS));

    float temp_c = s_snapshot.water_temp_c;
    if (s_water_sensor_ready) {
        esp_err_t err = ds18b20_sensor_read(&temp_c);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "DS18B20 read failed (%s)", esp_err_to_name(err));
            s_water_sensor_ready = (ds18b20_sensor_init(WATER_SENSOR_PIN) == ESP_OK);
        }
    }
    s_snapshot.water_temp_c = temp_c + cfg.offsets.water_temp_c;

    float distance_cm = s_snapshot.sea_level_cm;
    if (s_ultra_ready) {
        esp_err_t err = ultrasonic_sensor_measure(&distance_cm);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Ultrasonic read failed (%s)", esp_err_to_name(err));
            s_ultra_ready = (ultrasonic_sensor_init(ULTRASONIC_TRIG_PIN, ULTRASONIC_ECHO_PIN) == ESP_OK);
        }
    }
    s_snapshot.sea_level_cm = distance_cm + cfg.offsets.sea_level_cm;
    if (s_snapshot.sea_level_cm < 0.0f) {
        s_snapshot.sea_level_cm = 0.0f;
    }

    power_manager_set(POWER_DOMAIN_SENSOR_POD, false);
}

void sensor_manager_trigger_air_measurement(void)
{
    ESP_LOGI(TAG, "Air measurement triggered");
    measurement_config_t cfg = config_store_get();

    bool have_temp = false;
    bool have_hum = false;

    // Hvis BME (med fukt) er oppe, bruk den fullt ut
    if (s_air_sensor_ready) {
        float bme_temp = 0.0f;
        float bme_hum = 0.0f;
        float bme_press = 0.0f;
        esp_err_t err = bme280_sensor_read(&bme_temp, &bme_hum, &bme_press);
        if (err == ESP_OK) {
            bool all_zero = (bme_temp == 0.0f && bme_hum == 0.0f && bme_press == 0.0f);
            if (all_zero) {
                ESP_LOGW(TAG, "BME/BMP all-zero sample, keeping previous values");
            } else {
                if (bme_press > 0.0f) {
                    s_snapshot.air_pressure_hpa = bme_press;
                } else {
                    ESP_LOGW(TAG, "BME/BMP pressure invalid (%.1f)", bme_press);
                }
                if (!(bme_temp == 0.0f && bme_hum == 0.0f)) {
                    s_snapshot.air_temp_c = bme_temp + cfg.offsets.air_temp_c;
                    s_snapshot.humidity_percent = bme_hum;
                    have_temp = true;
                    have_hum = (bme_hum > 0.0f); // blir 0 hvis BMP
                }
                ESP_LOGI(TAG, "BME/BMP: t=%.2fC h=%.1f%% p=%.1fhPa", bme_temp, bme_hum, bme_press);
            }
        } else {
            ESP_LOGW(TAG, "BME/BMP read failed: %s", esp_err_to_name(err));
            s_air_sensor_ready = false;
        }
    }

    // Hvis vi ikke fikk temp/hum fra BME (eller den er BMP), bruk AHT20 hvis mulig
    if ((!have_temp || !have_hum) && s_aht_ready) {
        float aht_temp = 0.0f;
        float aht_hum = 0.0f;
        esp_err_t err_aht = aht20_read(&aht_temp, &aht_hum);
        if (err_aht == ESP_OK) {
            if (!(aht_temp == 0.0f && aht_hum == 0.0f)) {
                s_snapshot.air_temp_c = aht_temp + cfg.offsets.air_temp_c;
                if (aht_hum < 0.0f) aht_hum = 0.0f;
                if (aht_hum > 100.0f) aht_hum = 100.0f;
                s_snapshot.humidity_percent = aht_hum;
            } else {
                ESP_LOGW(TAG, "AHT20 all-zero sample, keeping previous values");
            }
        } else {
            ESP_LOGW(TAG, "AHT20 read failed (%s)", esp_err_to_name(err_aht));
            s_aht_ready = (aht20_init(AIR_SENSOR_I2C_PORT, AIR_SENSOR_SDA, AIR_SENSOR_SCL) == ESP_OK);
        }
    }
}

void sensor_manager_trigger_battery_measurement(void)
{
    ESP_LOGI(TAG, "Battery measurement triggered");
    float voltage = s_snapshot.battery_voltage;
    float percent = s_snapshot.battery_percent;
    if (battery_monitor_read(&voltage, &percent) == ESP_OK) {
        s_snapshot.battery_voltage = voltage;
        s_snapshot.battery_percent = percent;
    }
}

void sensor_manager_get_snapshot(sensor_snapshot_t *out)
{
    if (!out) {
        return;
    }
    *out = s_snapshot;
}
