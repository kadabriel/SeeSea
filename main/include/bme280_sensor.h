#pragma once

#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/i2c.h"

esp_err_t bme280_sensor_init(i2c_port_t port, gpio_num_t sda_pin, gpio_num_t scl_pin);
esp_err_t bme280_sensor_read(float *temperature_c, float *humidity_percent, float *pressure_hpa);

