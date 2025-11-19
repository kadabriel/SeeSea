#include "bme280_sensor.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "bme280"
#define BME280_DEFAULT_ADDR 0x76
#define BME280_ALT_ADDR 0x77
#define I2C_TIMEOUT_MS 100

typedef struct {
    uint16_t dig_T1;
    int16_t dig_T2;
    int16_t dig_T3;
    uint16_t dig_P1;
    int16_t dig_P2;
    int16_t dig_P3;
    int16_t dig_P4;
    int16_t dig_P5;
    int16_t dig_P6;
    int16_t dig_P7;
    int16_t dig_P8;
    int16_t dig_P9;
    uint8_t dig_H1;
    int16_t dig_H2;
    uint8_t dig_H3;
    int16_t dig_H4;
    int16_t dig_H5;
    int8_t dig_H6;
} bme280_calib_data_t;

static bme280_calib_data_t s_calib;
static bool s_driver_ready = false;
static bool s_calib_ready = false;
static i2c_port_t s_port = I2C_NUM_0;
static uint8_t s_addr = BME280_ALT_ADDR; // prefer 0x77 (observed on module)
static bool s_is_bmp280 = false; // false = BME280 (temp+hum+press), true = BMP280 (temp+press only)

static esp_err_t i2c_bus_init(i2c_port_t port, gpio_num_t sda_pin, gpio_num_t scl_pin)
{
    static bool s_i2c_configured = false;
    if (s_i2c_configured) {
        return ESP_OK;
    }

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda_pin,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = scl_pin,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
        .clk_flags = 0,
    };

    ESP_RETURN_ON_ERROR(i2c_param_config(port, &conf), TAG, "i2c_param_config");
    esp_err_t err = i2c_driver_install(port, conf.mode, 0, 0, 0);
    if (err == ESP_ERR_INVALID_STATE) {
        s_i2c_configured = true;
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "i2c_driver_install");
    s_i2c_configured = true;
    return ESP_OK;
}

static esp_err_t i2c_write(uint8_t addr, uint8_t reg, const uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    if (len && data) {
        i2c_master_write(cmd, (uint8_t *)data, len, true);
    }
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(s_port, cmd, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    return err;
}

static esp_err_t i2c_read(uint8_t addr, uint8_t reg, uint8_t *data, size_t len)
{
    if (!len) {
        return ESP_OK;
    }
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);
    if (len > 1) {
        i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(s_port, cmd, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    return err;
}

static esp_err_t probe_address(uint8_t addr)
{
    uint8_t id = 0;
    esp_err_t err = i2c_read(addr, 0xD0, &id, 1);
    if (err != ESP_OK) {
        return err;
    }
    if (id == 0x60 || id == 0x58) { // BME280 or BMP280
        s_addr = addr;
        s_is_bmp280 = (id == 0x58);
        return ESP_OK;
    }
    return ESP_FAIL;
}

static esp_err_t read_calibration(void)
{
    uint8_t buf1[26];
    ESP_RETURN_ON_ERROR(i2c_read(s_addr, 0x88, buf1, sizeof(buf1)), TAG, "calib block1");

    s_calib.dig_T1 = (uint16_t)((buf1[1] << 8) | buf1[0]);
    s_calib.dig_T2 = (int16_t)((buf1[3] << 8) | buf1[2]);
    s_calib.dig_T3 = (int16_t)((buf1[5] << 8) | buf1[4]);

    s_calib.dig_P1 = (uint16_t)((buf1[7] << 8) | buf1[6]);
    s_calib.dig_P2 = (int16_t)((buf1[9] << 8) | buf1[8]);
    s_calib.dig_P3 = (int16_t)((buf1[11] << 8) | buf1[10]);
    s_calib.dig_P4 = (int16_t)((buf1[13] << 8) | buf1[12]);
    s_calib.dig_P5 = (int16_t)((buf1[15] << 8) | buf1[14]);
    s_calib.dig_P6 = (int16_t)((buf1[17] << 8) | buf1[16]);
    s_calib.dig_P7 = (int16_t)((buf1[19] << 8) | buf1[18]);
    s_calib.dig_P8 = (int16_t)((buf1[21] << 8) | buf1[20]);
    s_calib.dig_P9 = (int16_t)((buf1[23] << 8) | buf1[22]);
    if (!s_is_bmp280) {
        s_calib.dig_H1 = buf1[25];
        uint8_t buf2[7];
        ESP_RETURN_ON_ERROR(i2c_read(s_addr, 0xE1, buf2, sizeof(buf2)), TAG, "calib block2");
        s_calib.dig_H2 = (int16_t)((buf2[1] << 8) | buf2[0]);
        s_calib.dig_H3 = buf2[2];
        s_calib.dig_H4 = (int16_t)((buf2[3] << 4) | (buf2[4] & 0x0F));
        s_calib.dig_H5 = (int16_t)((buf2[5] << 4) | (buf2[4] >> 4));
        s_calib.dig_H6 = (int8_t)buf2[6];
    } else {
        s_calib.dig_H1 = 0;
        s_calib.dig_H2 = 0;
        s_calib.dig_H3 = 0;
        s_calib.dig_H4 = 0;
        s_calib.dig_H5 = 0;
        s_calib.dig_H6 = 0;
    }

    s_calib_ready = true;
    return ESP_OK;
}

static float compensate_temperature(int32_t adc_T, int32_t *t_fine)
{
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)s_calib.dig_T1 << 1))) * ((int32_t)s_calib.dig_T2)) >> 11;
    int32_t var2 = (((((adc_T >> 4) - ((int32_t)s_calib.dig_T1)) * ((adc_T >> 4) - ((int32_t)s_calib.dig_T1))) >> 12) *
                    ((int32_t)s_calib.dig_T3)) >> 14;
    *t_fine = var1 + var2;
    float T = (float)((*t_fine * 5 + 128) >> 8);
    return T / 100.0f;
}

static float compensate_pressure(int32_t adc_P, int32_t t_fine)
{
    int64_t var1 = ((int64_t)t_fine) - 128000;
    int64_t var2 = var1 * var1 * (int64_t)s_calib.dig_P6;
    var2 = var2 + ((var1 * (int64_t)s_calib.dig_P5) << 17);
    var2 = var2 + (((int64_t)s_calib.dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)s_calib.dig_P3) >> 8) + ((var1 * (int64_t)s_calib.dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1) * (int64_t)s_calib.dig_P1) >> 33;

    if (var1 == 0) {
        return 0.0f;
    }

    int64_t p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = ((int64_t)s_calib.dig_P9 * (p >> 13) * (p >> 13)) >> 25;
    var2 = ((int64_t)s_calib.dig_P8 * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)s_calib.dig_P7) << 4);

    return (float)p / 25600.0f; // hPa
}

static float compensate_humidity(int32_t adc_H, int32_t t_fine)
{
    if (s_is_bmp280) {
        return 0.0f;
    }
    int32_t v_x1_u32r = t_fine - 76800;
    v_x1_u32r = (((((adc_H << 14) - (((int32_t)s_calib.dig_H4) << 20) - (((int32_t)s_calib.dig_H5) * v_x1_u32r)) + 16384) >> 15) *
                 (((((((v_x1_u32r * ((int32_t)s_calib.dig_H6)) >> 10) * (((v_x1_u32r * ((int32_t)s_calib.dig_H3)) >> 11) + 32768)) >> 10) + 2097152) *
                    ((int32_t)s_calib.dig_H2) + 8192) >> 14));
    v_x1_u32r = v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) * ((int32_t)s_calib.dig_H1)) >> 4);
    v_x1_u32r = v_x1_u32r < 0 ? 0 : v_x1_u32r;
    v_x1_u32r = v_x1_u32r > 419430400 ? 419430400 : v_x1_u32r;
    float h = (float)(v_x1_u32r >> 12) / 1024.0f;
    if (h > 100.0f) {
        h = 100.0f;
    }
    return h;
}

static esp_err_t trigger_measurement(void)
{
    const uint8_t ctrl_meas = (0x02 << 5) |     // x2 oversampling temperature
                              (0x03 << 2) |     // x4 oversampling pressure
                              0x01;             // forced mode
    const uint8_t config = (0x02 << 2);         // IIR filter coeff = 4

    ESP_RETURN_ON_ERROR(i2c_write(s_addr, 0xF5, &config, 1), TAG, "config");
    if (!s_is_bmp280) {
        const uint8_t ctrl_hum = 0x01;          // x1 oversampling humidity
        ESP_RETURN_ON_ERROR(i2c_write(s_addr, 0xF2, &ctrl_hum, 1), TAG, "ctrl_hum");
    }
    ESP_RETURN_ON_ERROR(i2c_write(s_addr, 0xF4, &ctrl_meas, 1), TAG, "ctrl_meas");
    return ESP_OK;
}

static esp_err_t wait_for_measurement(void)
{
    for (int i = 0; i < 30; ++i) {
        uint8_t status = 0;
        ESP_RETURN_ON_ERROR(i2c_read(s_addr, 0xF3, &status, 1), TAG, "status");
        if ((status & 0x08) == 0) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t bme280_sensor_init(i2c_port_t port, gpio_num_t sda_pin, gpio_num_t scl_pin)
{
    s_port = port;

    ESP_RETURN_ON_ERROR(i2c_bus_init(port, sda_pin, scl_pin), TAG, "i2c init");

    esp_err_t p = probe_address(s_addr);
    if (p != ESP_OK) {
        s_addr = BME280_DEFAULT_ADDR;
        ESP_RETURN_ON_ERROR(probe_address(s_addr), TAG, "probe_default");
    }

    const uint8_t reset_cmd = 0xB6;
    ESP_RETURN_ON_ERROR(i2c_write(s_addr, 0xE0, &reset_cmd, 1), TAG, "soft reset");
    vTaskDelay(pdMS_TO_TICKS(5));

    ESP_RETURN_ON_ERROR(read_calibration(), TAG, "calib");
    s_driver_ready = true;
    ESP_LOGI(TAG, "Detected BME280 at 0x%02X", s_addr);
    return ESP_OK;
}

esp_err_t bme280_sensor_read(float *temperature_c, float *humidity_percent, float *pressure_hpa)
{
    if (!s_driver_ready || !s_calib_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(trigger_measurement(), TAG, "trigger");
    ESP_RETURN_ON_ERROR(wait_for_measurement(), TAG, "wait");

    uint8_t data[8];
    ESP_RETURN_ON_ERROR(i2c_read(s_addr, 0xF7, data, sizeof(data)), TAG, "read data");

    int32_t adc_P = ((int32_t)data[0] << 12) | ((int32_t)data[1] << 4) | (data[2] >> 4);
    int32_t adc_T = ((int32_t)data[3] << 12) | ((int32_t)data[4] << 4) | (data[5] >> 4);
    int32_t adc_H = ((int32_t)data[6] << 8) | data[7];

    int32_t t_fine = 0;
    float temp = compensate_temperature(adc_T, &t_fine);
    float pressure = compensate_pressure(adc_P, t_fine);
    float humidity = compensate_humidity(adc_H, t_fine);

    if (temperature_c) {
        *temperature_c = temp;
    }
    if (humidity_percent) {
        *humidity_percent = humidity;
    }
    if (pressure_hpa) {
        *pressure_hpa = pressure;
    }

    return ESP_OK;
}
