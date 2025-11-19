#include "i2c_scan.h"

#include "driver/i2c.h"
#include "esp_log.h"

#define TAG "i2c_scan"

void i2c_scan_and_log(void)
{
    int found = 0;
    ESP_LOGI(TAG, "Scanning I2C bus (0x03..0x77) on I2C_NUM_0");
    for (int addr = 0x03; addr <= 0x77; ++addr) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        esp_err_t err = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(100));
        i2c_cmd_link_delete(cmd);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Found device at 0x%02X", addr);
            found++;
        }
    }
    if (!found) {
        ESP_LOGW(TAG, "No I2C devices found");
    }
}
