#pragma once

#include "config_store.h"
#include "esp_err.h"
#include "esp_netif_ip_addr.h"

typedef struct {
    bool sta_connected;
    esp_ip4_addr_t sta_ip;
    esp_ip4_addr_t ap_ip;
} wifi_status_t;

esp_err_t wifi_manager_init(const measurement_config_t *config);
void wifi_manager_update_config(const measurement_config_t *config);
wifi_status_t wifi_manager_get_status(void);
