#pragma once

#include "esp_err.h"

typedef struct {
    float water_temp_c;
    float sea_level_cm;
    float air_temp_c;
    float humidity_percent;
    float air_pressure_hpa;
    float battery_percent;
    float battery_voltage;
} sensor_snapshot_t;

esp_err_t sensor_manager_init(void);
void sensor_manager_trigger_sea_measurement(void);
void sensor_manager_trigger_air_measurement(void);
void sensor_manager_trigger_battery_measurement(void);

void sensor_manager_get_snapshot(sensor_snapshot_t *out);
