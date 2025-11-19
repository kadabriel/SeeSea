#include "ultrasonic_sensor.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

#define TAG "ultrasonic"

typedef struct {
    const char *name;
    uint32_t trigger_high_us;
    uint32_t startup_delay_us;
    uint32_t wait_rising_timeout_us;
    uint32_t wait_falling_timeout_us;
} ultrasonic_profile_t;

static const ultrasonic_profile_t k_profiles[] = {
    // Single-wire profil: 30 µs puls, kort oppstart, og realistiske timeouts (<4 m -> ~23 ms)
    {"pulse30", 30, 400, 35000, 40000},
};
static size_t s_profile_cursor = 0;

static gpio_num_t s_trig_pin = GPIO_NUM_NC;
static gpio_num_t s_echo_pin = GPIO_NUM_NC;
static bool s_single_wire = false;
static bool s_ready = false;

static bool wait_for_level(gpio_num_t pin, int target_level, uint32_t timeout_us)
{
    uint64_t start = esp_timer_get_time();
    while ((esp_timer_get_time() - start) < timeout_us) {
        if (gpio_get_level(pin) == target_level) {
            return true;
        }
    }
    return false;
}

esp_err_t ultrasonic_sensor_init(gpio_num_t trig_pin, gpio_num_t echo_pin)
{
    if (trig_pin == GPIO_NUM_NC || echo_pin == GPIO_NUM_NC) {
        return ESP_ERR_INVALID_ARG;
    }
    s_trig_pin = trig_pin;
    s_echo_pin = echo_pin;
    s_single_wire = (trig_pin == echo_pin);

    // Configure TRIG pin as output (and later switch to input if single-wire)
    gpio_config_t trig_cfg = {
        .pin_bit_mask = 1ULL << trig_pin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&trig_cfg), TAG, "trig cfg");
    gpio_set_level(trig_pin, 0);

    if (!s_single_wire) {
        gpio_config_t echo_cfg = {
            .pin_bit_mask = 1ULL << echo_pin,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_RETURN_ON_ERROR(gpio_config(&echo_cfg), TAG, "echo cfg");
    }

    s_ready = true;
    ESP_LOGI(TAG, "Ultrasonic TRIG=GPIO%d ECHO=GPIO%d", trig_pin, echo_pin);
    return ESP_OK;
}

static esp_err_t measure_with_profile(const ultrasonic_profile_t *profile, float *distance_cm)
{
    esp_err_t status = ESP_OK;

    gpio_set_level(s_trig_pin, 0);
    esp_rom_delay_us(2);
    gpio_set_level(s_trig_pin, 1);
    esp_rom_delay_us(profile->trigger_high_us);
    gpio_set_level(s_trig_pin, 0);

    if (s_single_wire) {
        // Switch pin to input with pull-up to listen for echo on same line
        gpio_set_direction(s_trig_pin, GPIO_MODE_INPUT);
        gpio_set_pull_mode(s_trig_pin, GPIO_PULLUP_ONLY);
    }

    esp_rom_delay_us(profile->startup_delay_us);

    if (!wait_for_level(s_echo_pin, 1, profile->wait_rising_timeout_us)) {
        ESP_LOGW(TAG, "No echo rising edge (%s)", profile->name);
        status = ESP_ERR_TIMEOUT;
        goto cleanup;
    }
    uint64_t start = esp_timer_get_time();
    if (!wait_for_level(s_echo_pin, 0, profile->wait_falling_timeout_us)) {
        ESP_LOGW(TAG, "No echo falling edge (%s)", profile->name);
        status = ESP_ERR_TIMEOUT;
        goto cleanup;
    }
    uint64_t duration = esp_timer_get_time() - start;
    float cm = duration / 58.0f;
    if (distance_cm) {
        *distance_cm = cm;
    }

cleanup:
    if (s_single_wire) {
        gpio_set_direction(s_trig_pin, GPIO_MODE_OUTPUT);
        gpio_set_pull_mode(s_trig_pin, GPIO_FLOATING);
        gpio_set_level(s_trig_pin, 0);
    }
    return status;
}

esp_err_t ultrasonic_sensor_measure(float *distance_cm)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    const size_t samples = 5;
    float sum = 0.0f;
    size_t ok = 0;
    float anchor = -1.0f;

    size_t start_idx = s_profile_cursor;
    const ultrasonic_profile_t *profile = &k_profiles[start_idx];
    ESP_LOGI(TAG, "Profile %s: trig=%uus startup=%uus", profile->name, profile->trigger_high_us, profile->startup_delay_us);

    for (size_t i = 0; i < samples; ++i) {
        float reading = 0.0f;
        esp_err_t err = measure_with_profile(profile, &reading);
        if (err == ESP_OK) {
            if (anchor < 0.0f) {
                anchor = reading;
            }
            // godkjenn måling hvis innen +/-5 cm fra anchor
            if (fabsf(reading - anchor) <= 5.0f) {
                sum += reading;
                ok++;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    if (ok > 0) {
        if (distance_cm) {
            *distance_cm = sum / (float)ok;
        }
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}
