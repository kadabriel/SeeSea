#include "config_store.h"

#include "esp_log.h"
#include "esp_check.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <math.h>

#define TAG "config_store"
#define NVS_NAMESPACE "config"
#define KEY_VERSION "ver"
#define KEY_BATT "int_b"
#define KEY_AIR "int_a"
#define KEY_SEA "int_s"
#define KEY_DISPLAY "disp_sec"
#define KEY_NAME "dev_name"
#define KEY_WIFI "int_wifi"
#define KEY_WEB "int_web"
#define KEY_WIFI_SSID "wifi_ssid"
#define KEY_WIFI_PASS "wifi_pass"
#define KEY_SCR1 "scr1"
#define KEY_SCR2 "scr2"
#define KEY_OFF_WATER "off_w"
#define KEY_OFF_SEA "off_s"
#define KEY_OFF_AIR "off_a"
#define CONFIG_VERSION 6
#define DISPLAY_ON_SECONDS_MAX 3600U

static measurement_config_t s_config;
static const char *const k_screen_item_names[SCREEN_ITEM_COUNT] = {
    [SCREEN_ITEM_WATER_TEMP] = "water_temp",
    [SCREEN_ITEM_SEA_LEVEL] = "sea_level",
    [SCREEN_ITEM_AIR_TEMP] = "air_temp",
    [SCREEN_ITEM_HUMIDITY] = "humidity",
    [SCREEN_ITEM_PRESSURE] = "pressure",
    [SCREEN_ITEM_BATTERY_PERCENT] = "battery_percent",
    [SCREEN_ITEM_BATTERY_VOLTAGE] = "battery_voltage",
    [SCREEN_ITEM_IP_ADDRESS] = "ip_address",
};

size_t config_store_screen_item_count(void)
{
    return SCREEN_ITEM_COUNT;
}

const char *config_store_screen_item_name(size_t index)
{
    if (index >= SCREEN_ITEM_COUNT) {
        return NULL;
    }
    return k_screen_item_names[index];
}

uint32_t config_store_screen_item_bit(size_t index)
{
    if (index >= SCREEN_ITEM_COUNT) {
        return 0;
    }
    return 1U << index;
}

bool config_store_screen_name_to_bit(const char *name, uint32_t *bit_out)
{
    if (!name || !bit_out) {
        return false;
    }
    for (size_t i = 0; i < SCREEN_ITEM_COUNT; ++i) {
        if (k_screen_item_names[i] && strcmp(k_screen_item_names[i], name) == 0) {
            *bit_out = 1U << i;
            return true;
        }
    }
    return false;
}

static measurement_interval_t default_fast_interval(void)
{
    return (measurement_interval_t){ .minutes = 0, .seconds = 1 };
}

static measurement_interval_t default_wifi_interval(void)
{
    return (measurement_interval_t){ .minutes = 0, .seconds = 0 };
}

static measurement_interval_t default_web_interval(void)
{
    return (measurement_interval_t){ .minutes = 0, .seconds = 0 };
}

static const char *default_wifi_ssid(void)
{
    return "ROG";
}

static const char *default_wifi_password(void)
{
    return "blomstervann";
}

static uint32_t default_screen_mask(size_t index)
{
    switch (index) {
        case 0:
            return (1U << SCREEN_ITEM_WATER_TEMP) |
                   (1U << SCREEN_ITEM_SEA_LEVEL) |
                   (1U << SCREEN_ITEM_AIR_TEMP) |
                   (1U << SCREEN_ITEM_HUMIDITY);
        case 1:
        default:
            return (1U << SCREEN_ITEM_IP_ADDRESS) |
                   (1U << SCREEN_ITEM_PRESSURE) |
                   (1U << SCREEN_ITEM_BATTERY_PERCENT) |
                   (1U << SCREEN_ITEM_BATTERY_VOLTAGE);
    }
}

static uint16_t default_display_on_seconds(void)
{
    return 30;
}

static const char *default_device_name(void)
{
    return "sea";
}

static void sanitize_device_name(char *name)
{
    if (!name) {
        return;
    }
    if (name[0] == '\0') {
        strlcpy(name, default_device_name(), CONFIG_STORE_MAX_NAME_LEN);
    }
    size_t len = strnlen(name, CONFIG_STORE_MAX_NAME_LEN);
    for (size_t i = 0; i < len; ++i) {
        char c = name[i];
        bool is_valid =
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            (c == '-') || (c == '_');
        if (!is_valid) {
            if (c >= 'A' && c <= 'Z') {
                name[i] = (char)(c + 32); // to lowercase
            } else {
                name[i] = '-';
            }
        }
    }
    name[CONFIG_STORE_MAX_NAME_LEN - 1] = '\0';
}

static uint16_t sanitize_display_on_seconds(uint16_t seconds)
{
    if (seconds == 0) {
        return 0;
    }
    if (seconds > DISPLAY_ON_SECONDS_MAX) {
        return DISPLAY_ON_SECONDS_MAX;
    }
    return seconds;
}

static uint32_t sanitize_screen_mask(uint32_t mask, size_t index)
{
    uint32_t valid_mask = (SCREEN_ITEM_COUNT >= 32) ? 0xFFFFFFFFU : ((1U << SCREEN_ITEM_COUNT) - 1U);
    mask &= valid_mask;
    if (index == 1) {
        mask |= (1U << SCREEN_ITEM_IP_ADDRESS);
    }
    if (mask == 0) {
        mask = default_screen_mask(index);
    }
    return mask;
}

static void load_defaults(void)
{
    s_config.battery = default_fast_interval();
    s_config.air = default_fast_interval();
    s_config.sea = default_fast_interval();
    s_config.wifi = default_wifi_interval();
    s_config.web_ui = default_web_interval();
    s_config.display_on_seconds = default_display_on_seconds();
    strlcpy(s_config.device_name, default_device_name(), sizeof(s_config.device_name));
    strlcpy(s_config.wifi_ssid, default_wifi_ssid(), sizeof(s_config.wifi_ssid));
    strlcpy(s_config.wifi_password, default_wifi_password(), sizeof(s_config.wifi_password));
    s_config.screen_items[0] = default_screen_mask(0);
    s_config.screen_items[1] = default_screen_mask(1);
    s_config.offsets = (measurement_offsets_t){0};
}

static esp_err_t ensure_nvs_ready(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

static float clampf_range(float value, float min, float max)
{
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

static void normalize_config(measurement_config_t *cfg)
{
    config_store_normalize_interval(&cfg->battery);
    config_store_normalize_interval(&cfg->air);
    config_store_normalize_interval(&cfg->sea);
    config_store_normalize_interval(&cfg->wifi);
    config_store_normalize_interval(&cfg->web_ui);
    cfg->display_on_seconds = sanitize_display_on_seconds(cfg->display_on_seconds);
    sanitize_device_name(cfg->device_name);
    cfg->wifi_ssid[CONFIG_STORE_MAX_WIFI_SSID_LEN - 1] = '\0';
    cfg->wifi_password[CONFIG_STORE_MAX_WIFI_PASS_LEN - 1] = '\0';
    cfg->screen_items[0] = sanitize_screen_mask(cfg->screen_items[0], 0);
    cfg->screen_items[1] = sanitize_screen_mask(cfg->screen_items[1], 1);
    cfg->offsets.water_temp_c = clampf_range(cfg->offsets.water_temp_c, -20.0f, 20.0f);
    cfg->offsets.sea_level_cm = clampf_range(cfg->offsets.sea_level_cm, -200.0f, 200.0f);
    cfg->offsets.air_temp_c = clampf_range(cfg->offsets.air_temp_c, -20.0f, 20.0f);
}

static esp_err_t load_from_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No stored configuration, using defaults");
        load_defaults();
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs_open");

    int32_t version = 0;
    err = nvs_get_i32(handle, KEY_VERSION, &version);
    if (err != ESP_OK || version != CONFIG_VERSION) {
        ESP_LOGW(TAG, "Config version mismatch (stored=%ld, expected=%d)", version, CONFIG_VERSION);
        nvs_close(handle);
        load_defaults();
        return ESP_OK;
    }

    size_t len = sizeof(measurement_interval_t);
    err = nvs_get_blob(handle, KEY_BATT, &s_config.battery, &len);
    if (err != ESP_OK || len != sizeof(measurement_interval_t)) {
        ESP_LOGW(TAG, "Failed to load battery interval, using default");
        s_config.battery = default_fast_interval();
    }

    len = sizeof(measurement_interval_t);
    err = nvs_get_blob(handle, KEY_AIR, &s_config.air, &len);
    if (err != ESP_OK || len != sizeof(measurement_interval_t)) {
        ESP_LOGW(TAG, "Failed to load air interval, using default");
        s_config.air = default_fast_interval();
    }

    len = sizeof(measurement_interval_t);
    err = nvs_get_blob(handle, KEY_SEA, &s_config.sea, &len);
    if (err != ESP_OK || len != sizeof(measurement_interval_t)) {
        ESP_LOGW(TAG, "Failed to load sea interval, using default");
        s_config.sea = default_fast_interval();
    }

    len = sizeof(measurement_interval_t);
    err = nvs_get_blob(handle, KEY_WIFI, &s_config.wifi, &len);
    if (err != ESP_OK || len != sizeof(measurement_interval_t)) {
        ESP_LOGW(TAG, "Failed to load Wi-Fi interval, using default");
        s_config.wifi = default_wifi_interval();
    }

    len = sizeof(measurement_interval_t);
    err = nvs_get_blob(handle, KEY_WEB, &s_config.web_ui, &len);
    if (err != ESP_OK || len != sizeof(measurement_interval_t)) {
        ESP_LOGW(TAG, "Failed to load Web UI interval, using default");
        s_config.web_ui = default_web_interval();
    }

    uint16_t display_seconds = default_display_on_seconds();
    err = nvs_get_u16(handle, KEY_DISPLAY, &display_seconds);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load display-on duration, using default");
    }
    s_config.display_on_seconds = sanitize_display_on_seconds(display_seconds);

    size_t name_len = sizeof(s_config.device_name);
    err = nvs_get_str(handle, KEY_NAME, s_config.device_name, &name_len);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load device name, using default");
        strlcpy(s_config.device_name, default_device_name(), sizeof(s_config.device_name));
    }
    sanitize_device_name(s_config.device_name);

    size_t ssid_len = sizeof(s_config.wifi_ssid);
    err = nvs_get_str(handle, KEY_WIFI_SSID, s_config.wifi_ssid, &ssid_len);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load Wi-Fi SSID, using default");
        strlcpy(s_config.wifi_ssid, default_wifi_ssid(), sizeof(s_config.wifi_ssid));
    }

    size_t pass_len = sizeof(s_config.wifi_password);
    err = nvs_get_str(handle, KEY_WIFI_PASS, s_config.wifi_password, &pass_len);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load Wi-Fi password, using default");
        strlcpy(s_config.wifi_password, default_wifi_password(), sizeof(s_config.wifi_password));
    }

    uint32_t screen_mask = 0;
    err = nvs_get_u32(handle, KEY_SCR1, &screen_mask);
    if (err == ESP_OK) {
        s_config.screen_items[0] = screen_mask;
    } else {
        s_config.screen_items[0] = default_screen_mask(0);
    }
    err = nvs_get_u32(handle, KEY_SCR2, &screen_mask);
    if (err == ESP_OK) {
        s_config.screen_items[1] = screen_mask;
    } else {
        s_config.screen_items[1] = default_screen_mask(1);
    }

    int32_t offset_raw = 0;
    if (nvs_get_i32(handle, KEY_OFF_WATER, &offset_raw) == ESP_OK) {
        s_config.offsets.water_temp_c = offset_raw / 1000.0f;
    } else {
        s_config.offsets.water_temp_c = 0.0f;
    }
    if (nvs_get_i32(handle, KEY_OFF_SEA, &offset_raw) == ESP_OK) {
        s_config.offsets.sea_level_cm = offset_raw / 1000.0f;
    } else {
        s_config.offsets.sea_level_cm = 0.0f;
    }
    if (nvs_get_i32(handle, KEY_OFF_AIR, &offset_raw) == ESP_OK) {
        s_config.offsets.air_temp_c = offset_raw / 1000.0f;
    } else {
        s_config.offsets.air_temp_c = 0.0f;
    }

    nvs_close(handle);
    normalize_config(&s_config);
    return ESP_OK;
}

esp_err_t config_store_init(void)
{
    ESP_RETURN_ON_ERROR(ensure_nvs_ready(), TAG, "nvs_flash_init");
    ESP_RETURN_ON_ERROR(load_from_nvs(), TAG, "load_from_nvs");
    return ESP_OK;
}

measurement_config_t config_store_get(void)
{
    return s_config;
}

static esp_err_t write_interval(nvs_handle_t handle, const char *key, const measurement_interval_t *interval)
{
    return nvs_set_blob(handle, key, interval, sizeof(*interval));
}

esp_err_t config_store_set(const measurement_config_t *cfg)
{
    measurement_config_t updated = *cfg;
    normalize_config(&updated);
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle), TAG, "nvs_open");

    esp_err_t ret = ESP_OK;
    ESP_GOTO_ON_ERROR(nvs_set_i32(handle, KEY_VERSION, CONFIG_VERSION), out, TAG, "set version");
    ESP_GOTO_ON_ERROR(write_interval(handle, KEY_BATT, &updated.battery), out, TAG, "set batt");
    ESP_GOTO_ON_ERROR(write_interval(handle, KEY_AIR, &updated.air), out, TAG, "set air");
    ESP_GOTO_ON_ERROR(write_interval(handle, KEY_SEA, &updated.sea), out, TAG, "set sea");
    ESP_GOTO_ON_ERROR(write_interval(handle, KEY_WIFI, &updated.wifi), out, TAG, "set wifi");
    ESP_GOTO_ON_ERROR(write_interval(handle, KEY_WEB, &updated.web_ui), out, TAG, "set web");
    ESP_GOTO_ON_ERROR(nvs_set_u16(handle, KEY_DISPLAY, updated.display_on_seconds), out, TAG, "set display");
    ESP_GOTO_ON_ERROR(nvs_set_str(handle, KEY_NAME, updated.device_name), out, TAG, "set name");
    ESP_GOTO_ON_ERROR(nvs_set_str(handle, KEY_WIFI_SSID, updated.wifi_ssid), out, TAG, "set wifi ssid");
    ESP_GOTO_ON_ERROR(nvs_set_str(handle, KEY_WIFI_PASS, updated.wifi_password), out, TAG, "set wifi pass");
    ESP_GOTO_ON_ERROR(nvs_set_u32(handle, KEY_SCR1, updated.screen_items[0]), out, TAG, "set scr1");
    ESP_GOTO_ON_ERROR(nvs_set_u32(handle, KEY_SCR2, updated.screen_items[1]), out, TAG, "set scr2");
    int32_t water_milli = (int32_t)lrintf(updated.offsets.water_temp_c * 1000.0f);
    int32_t sea_milli = (int32_t)lrintf(updated.offsets.sea_level_cm * 1000.0f);
    int32_t air_milli = (int32_t)lrintf(updated.offsets.air_temp_c * 1000.0f);
    ESP_GOTO_ON_ERROR(nvs_set_i32(handle, KEY_OFF_WATER, water_milli), out, TAG, "set off water");
    ESP_GOTO_ON_ERROR(nvs_set_i32(handle, KEY_OFF_SEA, sea_milli), out, TAG, "set off sea");
    ESP_GOTO_ON_ERROR(nvs_set_i32(handle, KEY_OFF_AIR, air_milli), out, TAG, "set off air");
    ESP_GOTO_ON_ERROR(nvs_commit(handle), out, TAG, "nvs_commit");

    s_config = updated;
    nvs_close(handle);
    return ESP_OK;

out:
    nvs_close(handle);
    return ret;
}

void config_store_reset_defaults(void)
{
    load_defaults();
    (void)config_store_set(&s_config);
}

uint32_t config_store_interval_to_seconds(measurement_interval_t interval)
{
    return (uint32_t)interval.minutes * 60U + interval.seconds;
}

void config_store_normalize_interval(measurement_interval_t *interval)
{
    if (!interval) {
        return;
    }
    uint32_t total_seconds = (uint32_t)interval->minutes * 60U + interval->seconds;
    interval->minutes = total_seconds / 60U;
    interval->seconds = total_seconds % 60U;
}
