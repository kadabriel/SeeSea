#pragma once

#include "esp_err.h"
#include "driver/gpio.h"

esp_err_t ds18b20_sensor_init(gpio_num_t pin);
esp_err_t ds18b20_sensor_read(float *temperature_c);
void ds18b20_sensor_deinit(void);
