#include "power_manager.h"

#include "driver/gpio.h"
#include "esp_log.h"

#define TAG "power_mgr"

// TODO: confirm final GPIO assignments with hardware
#define GPIO_SENSOR_POD_GATE GPIO_NUM_14
#define GPIO_DISPLAY_GATE    GPIO_NUM_15

static void configure_pin(gpio_num_t pin)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    // Default HIGH to keep P-channel MOSFETs off
    gpio_set_level(pin, 1);
}

esp_err_t power_manager_init(void)
{
    configure_pin(GPIO_SENSOR_POD_GATE);
    configure_pin(GPIO_DISPLAY_GATE);
    return ESP_OK;
}

static void set_gate(gpio_num_t pin, bool enabled)
{
    // For P-channel high-side switch: LOW = enable, HIGH = disable
    gpio_set_level(pin, enabled ? 0 : 1);
}

void power_manager_set(power_domain_t domain, bool enabled)
{
    switch (domain) {
        case POWER_DOMAIN_SENSOR_POD:
            set_gate(GPIO_SENSOR_POD_GATE, enabled);
            ESP_LOGD(TAG, "sensor pod %s", enabled ? "ON" : "OFF");
            break;
        case POWER_DOMAIN_DISPLAY:
            set_gate(GPIO_DISPLAY_GATE, enabled);
            ESP_LOGD(TAG, "display %s", enabled ? "ON" : "OFF");
            break;
        default:
            break;
    }
}
