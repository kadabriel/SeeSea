#include "aht20_sensor.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "aht20"
#define AHT20_ADDR 0x38

static bool s_ready = false;
static i2c_port_t s_port = I2C_NUM_0;
static gpio_num_t s_sda;
static gpio_num_t s_scl;

static esp_err_t aht20_write(const uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (AHT20_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, data, len, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(s_port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return err;
}

static esp_err_t aht20_read_bytes(uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (AHT20_ADDR << 1) | I2C_MASTER_READ, true);
    if (len > 1) {
        i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(s_port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return err;
}

esp_err_t aht20_init(i2c_port_t port, gpio_num_t sda_pin, gpio_num_t scl_pin)
{
    s_port = port;
    s_sda = sda_pin;
    s_scl = scl_pin;

    // Ensure bus configured (driver already installed elsewhere)
    i2c_cmd_handle_t ping = i2c_cmd_link_create();
    i2c_master_start(ping);
    i2c_master_write_byte(ping, (AHT20_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(ping);
    i2c_master_cmd_begin(s_port, ping, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(ping);

    // Send soft reset
    uint8_t reset_cmd = 0xBA;
    ESP_RETURN_ON_ERROR(aht20_write(&reset_cmd, 1), TAG, "reset");
    vTaskDelay(pdMS_TO_TICKS(20));

    // Init command: 0xBE, 0x08, 0x00
    uint8_t init_cmd[] = {0xBE, 0x08, 0x00};
    ESP_RETURN_ON_ERROR(aht20_write(init_cmd, sizeof(init_cmd)), TAG, "init");
    vTaskDelay(pdMS_TO_TICKS(10));

    s_ready = true;
    ESP_LOGI(TAG, "AHT20 ready at 0x%02X", AHT20_ADDR);
    return ESP_OK;
}

esp_err_t aht20_read(float *temperature_c, float *humidity_percent)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    // Trigger measurement: 0xAC, 0x33, 0x00
    uint8_t measure_cmd[] = {0xAC, 0x33, 0x00};
    ESP_RETURN_ON_ERROR(aht20_write(measure_cmd, sizeof(measure_cmd)), TAG, "measure");
    vTaskDelay(pdMS_TO_TICKS(80));

    uint8_t raw[6];
    ESP_RETURN_ON_ERROR(aht20_read_bytes(raw, sizeof(raw)), TAG, "read");

    uint32_t hum_raw = ((uint32_t)raw[1] << 12) | ((uint32_t)raw[2] << 4) | (raw[3] >> 4);
    uint32_t temp_raw = (((uint32_t)raw[3] & 0x0F) << 16) | ((uint32_t)raw[4] << 8) | raw[5];

    float hum = (hum_raw / 1048576.0f) * 100.0f;
    float temp = ((temp_raw / 1048576.0f) * 200.0f) - 50.0f;

    if (humidity_percent) {
        if (hum < 0.0f) hum = 0.0f;
        if (hum > 100.0f) hum = 100.0f;
        *humidity_percent = hum;
    }
    if (temperature_c) {
        *temperature_c = temp;
    }
    return ESP_OK;
}
