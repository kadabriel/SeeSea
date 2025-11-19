#include "ds18b20_sensor.h"

#include "esp_log.h"
#include "esp_check.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "ds18b20"

#define RESET_PULSE_US 500
#define PRESENCE_WAIT_US 70
#define PRESENCE_DURATION_US 410
#define WRITE_SLOT_US 60
#define READ_SAMPLE_US 9
#define READ_SLOT_US 55
#define CONVERSION_TIMEOUT_MS 750

static gpio_num_t s_pin = GPIO_NUM_NC;
static bool s_ready = false;

static void bus_drive_low(void)
{
    gpio_set_direction(s_pin, GPIO_MODE_OUTPUT);
    gpio_set_level(s_pin, 0);
}

static void bus_release(void)
{
    gpio_set_direction(s_pin, GPIO_MODE_INPUT);
}

static esp_err_t onewire_reset(void)
{
    bus_drive_low();
    esp_rom_delay_us(RESET_PULSE_US);
    bus_release();
    esp_rom_delay_us(PRESENCE_WAIT_US);
    int presence = gpio_get_level(s_pin);
    esp_rom_delay_us(PRESENCE_DURATION_US);
    return presence == 0 ? ESP_OK : ESP_FAIL;
}

static void onewire_write_bit(uint8_t bit)
{
    bus_drive_low();
    if (bit) {
        esp_rom_delay_us(6);
        bus_release();
        esp_rom_delay_us(WRITE_SLOT_US);
    } else {
        esp_rom_delay_us(WRITE_SLOT_US);
        bus_release();
        esp_rom_delay_us(10);
    }
}

static uint8_t onewire_read_bit(void)
{
    uint8_t bit;
    bus_drive_low();
    esp_rom_delay_us(3);
    bus_release();
    esp_rom_delay_us(READ_SAMPLE_US);
    bit = (uint8_t)gpio_get_level(s_pin);
    esp_rom_delay_us(READ_SLOT_US);
    return bit;
}

static void onewire_write_byte(uint8_t byte)
{
    for (int i = 0; i < 8; ++i) {
        onewire_write_bit((byte >> i) & 0x01);
    }
}

static uint8_t onewire_read_byte(void)
{
    uint8_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= (onewire_read_bit() << i);
    }
    return value;
}

static uint8_t ds18b20_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < len; ++i) {
        uint8_t inbyte = data[i];
        for (int j = 0; j < 8; ++j) {
            uint8_t mix = (crc ^ inbyte) & 0x01;
            crc >>= 1;
            if (mix) {
                crc ^= 0x8C;
            }
            inbyte >>= 1;
        }
    }
    return crc;
}

static esp_err_t ds18b20_configure_resolution(void)
{
    ESP_RETURN_ON_ERROR(onewire_reset(), TAG, "reset for cfg");
    onewire_write_byte(0xCC); // Skip ROM
    onewire_write_byte(0x4E); // Write scratchpad
    onewire_write_byte(0x4B); // TH register default
    onewire_write_byte(0x46); // TL register default
    onewire_write_byte(0x5F); // Config: 11-bit resolution (375ms)
    ESP_RETURN_ON_ERROR(onewire_reset(), TAG, "reset for copy");
    onewire_write_byte(0xCC);
    onewire_write_byte(0x48); // Copy scratchpad to EEPROM
    vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_OK;
}

esp_err_t ds18b20_sensor_init(gpio_num_t pin)
{
    if (pin == GPIO_NUM_NC) {
        return ESP_ERR_INVALID_ARG;
    }
    s_pin = pin;
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "gpio");
    if (onewire_reset() != ESP_OK) {
        ESP_LOGE(TAG, "DS18B20 not responding");
        return ESP_FAIL;
    }
    if (ds18b20_configure_resolution() != ESP_OK) {
        ESP_LOGW(TAG, "Failed to configure resolution, continuing");
    }
    s_ready = true;
    ESP_LOGI(TAG, "DS18B20 ready on GPIO%d", pin);
    return ESP_OK;
}

void ds18b20_sensor_deinit(void)
{
    s_ready = false;
    s_pin = GPIO_NUM_NC;
}

esp_err_t ds18b20_sensor_read(float *temperature_c)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (onewire_reset() != ESP_OK) {
        s_ready = false;
        ESP_LOGE(TAG, "Reset failed");
        return ESP_FAIL;
    }
    onewire_write_byte(0xCC);
    onewire_write_byte(0x44); // Start conversion

    int wait_ms = CONVERSION_TIMEOUT_MS;
    while (wait_ms > 0) {
        if (gpio_get_level(s_pin) == 1) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
        wait_ms -= 10;
    }
    if (wait_ms <= 0) {
        ESP_LOGW(TAG, "Conversion timeout");
        return ESP_ERR_TIMEOUT;
    }

    if (onewire_reset() != ESP_OK) {
        ESP_LOGE(TAG, "Reset after conversion failed");
        return ESP_FAIL;
    }
    onewire_write_byte(0xCC);
    onewire_write_byte(0xBE); // Read scratchpad

    uint8_t data[9];
    for (int i = 0; i < 9; ++i) {
        data[i] = onewire_read_byte();
    }
    uint8_t crc = ds18b20_crc8(data, 8);
    if (crc != data[8]) {
        ESP_LOGW(TAG, "CRC mismatch");
        return ESP_ERR_INVALID_RESPONSE;
    }

    int16_t raw = (int16_t)((data[1] << 8) | data[0]);
    float temp_c = raw / 16.0f; // 11/12-bit resolution
    if (temperature_c) {
        *temperature_c = temp_c;
    }
    return ESP_OK;
}
