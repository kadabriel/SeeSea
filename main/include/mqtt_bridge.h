#pragma once

#include "sensor_manager.h"
#include "esp_err.h"

esp_err_t mqtt_bridge_init(void);
void mqtt_bridge_publish_snapshot(const sensor_snapshot_t *snapshot);

