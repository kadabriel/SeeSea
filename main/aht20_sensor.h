#pragma once
#include "esp_err.h"
#include "driver/i2c.h"

esp_err_t aht20_init(i2c_port_t port, gpio_num_t sda_pin, gpio_num_t scl_pin);
esp_err_t aht20_read(float *temperature_c, float *humidity_percent);
