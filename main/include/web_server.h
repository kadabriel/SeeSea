#pragma once

#include "config_store.h"
#include "esp_err.h"

esp_err_t web_server_start(void);
void web_server_update_config(const measurement_config_t *config);

