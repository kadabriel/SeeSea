#pragma once

#include "esp_err.h"

typedef enum {
    POWER_DOMAIN_SENSOR_POD,
    POWER_DOMAIN_DISPLAY,
} power_domain_t;

esp_err_t power_manager_init(void);
void power_manager_set(power_domain_t domain, bool enabled);

