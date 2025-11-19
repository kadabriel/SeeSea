#pragma once

#include "config_store.h"
#include "sensor_manager.h"
#include "wifi_manager.h"
#include "esp_err.h"

esp_err_t display_manager_init(const measurement_config_t *config);
void display_manager_update_config(const measurement_config_t *config);
void display_manager_show_snapshot(const sensor_snapshot_t *snapshot, const wifi_status_t *wifi_status);
void display_manager_next_screen(void);
