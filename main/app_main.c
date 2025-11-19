#include "app_main.h"

#include "config_store.h"
#include "display_manager.h"
#include "i2c_scan.h"
#include "mqtt_bridge.h"
#include "power_manager.h"
#include "scheduler.h"
#include "sensor_manager.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "google_bridge.h"

#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#define TAG "app"
#define BUTTON_PIN GPIO_NUM_33
#define BUTTON_DEBOUNCE_MS 50
#define BUTTON_POLL_MS 10

static void button_task(void *ctx)
{
    (void)ctx;
    bool last_level = false; // pull-down -> idle low
    TickType_t last_change = xTaskGetTickCount();
    while (true) {
        bool level = gpio_get_level(BUTTON_PIN);
        if (level != last_level) {
            TickType_t now = xTaskGetTickCount();
            if (now - last_change >= pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS)) {
                last_change = now;
                last_level = level;
                if (level) { // active high
                    display_manager_next_screen();
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_MS));
    }
}

static void start_button_task(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BUTTON_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    xTaskCreate(button_task, "btn", 2048, NULL, 5, NULL);
}

static void publish_latest_snapshot(void)
{
    sensor_snapshot_t snapshot;
    sensor_manager_get_snapshot(&snapshot);
    mqtt_bridge_publish_snapshot(&snapshot);
    google_bridge_publish_snapshot(&snapshot);
    wifi_status_t wifi_status = wifi_manager_get_status();
    display_manager_show_snapshot(&snapshot, &wifi_status);
}

static void battery_task(void *ctx)
{
    (void)ctx;
    sensor_manager_trigger_battery_measurement();
    publish_latest_snapshot();
}

static void air_task(void *ctx)
{
    (void)ctx;
    sensor_manager_trigger_air_measurement();
    publish_latest_snapshot();
}

static void sea_task(void *ctx)
{
    (void)ctx;
    sensor_manager_trigger_sea_measurement();
    publish_latest_snapshot();
}

static void register_scheduled_tasks(void)
{
    scheduler_register_task(SCHED_TASK_BATTERY, battery_task, NULL);
    scheduler_register_task(SCHED_TASK_AIR, air_task, NULL);
    scheduler_register_task(SCHED_TASK_SEA, sea_task, NULL);
}

void app_main(void)
{
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(config_store_init());
    measurement_config_t config = config_store_get();

    ESP_ERROR_CHECK(power_manager_init());
    ESP_ERROR_CHECK(sensor_manager_init());
    ESP_ERROR_CHECK(mqtt_bridge_init());
    google_bridge_update_config(&config);
    ESP_ERROR_CHECK(google_bridge_init());
    ESP_ERROR_CHECK(wifi_manager_init(&config));
    ESP_ERROR_CHECK(web_server_start());
    ESP_ERROR_CHECK(display_manager_init(&config));
    // I2C-skann etter at bussen er startet av sensor/display-init
    i2c_scan_and_log();
    start_button_task();

    ESP_ERROR_CHECK(scheduler_init(&config));
    register_scheduled_tasks();

    ESP_LOGI(TAG, "SeaSensor firmware started");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
