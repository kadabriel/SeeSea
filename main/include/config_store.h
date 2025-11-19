#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t minutes;
    uint8_t seconds;
} measurement_interval_t;

#define CONFIG_STORE_MAX_NAME_LEN 32
#define CONFIG_STORE_MAX_WIFI_SSID_LEN 32
#define CONFIG_STORE_MAX_WIFI_PASS_LEN 64

typedef enum {
    SCREEN_ITEM_WATER_TEMP = 0,
    SCREEN_ITEM_SEA_LEVEL,
    SCREEN_ITEM_AIR_TEMP,
    SCREEN_ITEM_HUMIDITY,
    SCREEN_ITEM_PRESSURE,
    SCREEN_ITEM_BATTERY_PERCENT,
    SCREEN_ITEM_BATTERY_VOLTAGE,
    SCREEN_ITEM_IP_ADDRESS,
    SCREEN_ITEM_COUNT
} screen_item_t;

typedef struct {
    float water_temp_c;
    float sea_level_cm;
    float air_temp_c;
} measurement_offsets_t;

typedef struct {
    measurement_interval_t battery;
    measurement_interval_t air;
    measurement_interval_t sea;
    measurement_interval_t wifi;
    measurement_interval_t web_ui;
    uint16_t display_on_seconds;
    char device_name[CONFIG_STORE_MAX_NAME_LEN];
    char wifi_ssid[CONFIG_STORE_MAX_WIFI_SSID_LEN];
    char wifi_password[CONFIG_STORE_MAX_WIFI_PASS_LEN];
    uint32_t screen_items[2];
    measurement_offsets_t offsets;
} measurement_config_t;

esp_err_t config_store_init(void);
measurement_config_t config_store_get(void);
esp_err_t config_store_set(const measurement_config_t *cfg);

void config_store_reset_defaults(void);

uint32_t config_store_interval_to_seconds(measurement_interval_t interval);
void config_store_normalize_interval(measurement_interval_t *interval);

size_t config_store_screen_item_count(void);
const char *config_store_screen_item_name(size_t index);
uint32_t config_store_screen_item_bit(size_t index);
bool config_store_screen_name_to_bit(const char *name, uint32_t *bit_out);
