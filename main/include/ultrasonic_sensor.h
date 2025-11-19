#pragma once

#include "esp_err.h"
#include "driver/gpio.h"

esp_err_t ultrasonic_sensor_init(gpio_num_t trig_pin, gpio_num_t echo_pin);
esp_err_t ultrasonic_sensor_measure(float *distance_cm);
