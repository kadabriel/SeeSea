#include "display_manager.h"

#include "power_manager.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2c.h"
#include "esp_timer.h"
#include "lwip/inet.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#define TAG "display"

#define SSD1306_ADDR 0x3C
#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64
#define FONT_WIDTH 6
#define FONT_HEIGHT 8
#define MAX_LINES (DISPLAY_HEIGHT / FONT_HEIGHT)
#define I2C_PORT I2C_NUM_0
#define I2C_TIMEOUT_MS 1000
#define DISPLAY_WAKE_DELAY_MS 20
#define DISPLAY_SCREEN_COUNT 2

static measurement_config_t s_display_cfg;
static bool s_display_powered = false;
static esp_timer_handle_t s_sleep_timer;
static uint8_t s_framebuffer[DISPLAY_WIDTH * DISPLAY_HEIGHT / 8];
static sensor_snapshot_t s_last_snapshot;
static sensor_snapshot_t s_filtered_snapshot;
static bool s_have_filtered = false;
static bool s_have_pending = false;
static float s_pending_level = 0.0f;
static wifi_status_t s_last_wifi;
static bool s_have_snapshot = false;
static uint8_t s_active_screen = 0;

static const uint8_t g_font_6x8[][FONT_WIDTH - 1] = {
#include "font5x7.inc"
};

static void display_sleep_cb(void *arg)
{
    (void)arg;
    power_manager_set(POWER_DOMAIN_DISPLAY, false);
    s_display_powered = false;
}

static esp_err_t ssd1306_write_cmd(uint8_t cmd)
{
    uint8_t data[2] = {0x00, cmd};
    return i2c_master_write_to_device(I2C_PORT, SSD1306_ADDR, data, sizeof(data), pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

static esp_err_t ssd1306_write_data(const uint8_t *data, size_t len)
{
    if (len == 0) {
        return ESP_OK;
    }
    esp_err_t err = ESP_OK;
    size_t offset = 0;
    uint8_t buffer[17];
    buffer[0] = 0x40;
    while (offset < len) {
        size_t chunk = len - offset;
        if (chunk > 16) {
            chunk = 16;
        }
        memcpy(&buffer[1], data + offset, chunk);
        err = i2c_master_write_to_device(I2C_PORT, SSD1306_ADDR, buffer, chunk + 1, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
        if (err != ESP_OK) {
            return err;
        }
        offset += chunk;
    }
    return ESP_OK;
}

static esp_err_t ssd1306_hw_init(void)
{
    static const uint8_t init_cmds[] = {
        0xAE, // display off
        0xD5, 0x80,
        0xA8, 0x3F,
        0xD3, 0x00,
        0x40,
        0x8D, 0x14,
        0x20, 0x00,
        0xA1,
        0xC8,
        0xDA, 0x12,
        0x81, 0xCF,
        0xD9, 0xF1,
        0xDB, 0x40,
        0xA4,
        0xA6,
        0x2E,
        0xAF,
    };
    for (size_t i = 0; i < sizeof(init_cmds); ++i) {
        ESP_RETURN_ON_ERROR(ssd1306_write_cmd(init_cmds[i]), TAG, "init cmd");
    }
    return ESP_OK;
}

static void clear_buffer(void)
{
    memset(s_framebuffer, 0, sizeof(s_framebuffer));
}

static void draw_char(int x, int y, char c)
{
    if (c < 32 || c > 126) {
        c = '?';
    }
    const uint8_t *glyph = g_font_6x8[c - 32];
    for (int col = 0; col < FONT_WIDTH - 1; ++col) {
        int target_col = x + col;
        if (target_col < 0 || target_col >= DISPLAY_WIDTH) {
            continue;
        }
        uint8_t column_bits = glyph[col];
        for (int row = 0; row < FONT_HEIGHT; ++row) {
            int target_row = y + row;
            if (target_row < 0 || target_row >= DISPLAY_HEIGHT) {
                continue;
            }
            int byte_index = target_col + (target_row / 8) * DISPLAY_WIDTH;
            if (column_bits & (1 << row)) {
                s_framebuffer[byte_index] |= (1 << (target_row & 7));
            }
        }
    }
}

static void draw_text_line(int line, const char *text)
{
    if (line < 0 || line >= MAX_LINES) {
        return;
    }
    int x = 0;
    int y = line * FONT_HEIGHT;
    while (*text && x < DISPLAY_WIDTH - FONT_WIDTH) {
        draw_char(x, y, *text++);
        x += FONT_WIDTH;
    }
}

static void ip_to_string(const esp_ip4_addr_t *ip, char *out, size_t len)
{
    if (!ip || ip->addr == 0) {
        strlcpy(out, "-", len);
        return;
    }
    snprintf(out, len, IPSTR, IP2STR(ip));
}

static void ensure_powered(void)
{
    if (!s_display_powered) {
        power_manager_set(POWER_DOMAIN_DISPLAY, true);
        vTaskDelay(pdMS_TO_TICKS(DISPLAY_WAKE_DELAY_MS));
        if (ssd1306_hw_init() == ESP_OK) {
            s_display_powered = true;
        } else {
            ESP_LOGE(TAG, "SSD1306 init failed");
        }
    }
}

static void schedule_sleep(void)
{
    if (s_display_cfg.display_on_seconds == 0) {
        if (s_sleep_timer) {
            esp_timer_stop(s_sleep_timer);
        }
        return;
    }
    if (!s_sleep_timer) {
        const esp_timer_create_args_t args = {
            .callback = display_sleep_cb,
            .name = "display_sleep"
        };
        esp_timer_create(&args, &s_sleep_timer);
    }
    esp_timer_stop(s_sleep_timer);
    esp_timer_start_once(s_sleep_timer, (uint64_t)s_display_cfg.display_on_seconds * 1000000ULL);
}

static void format_line(screen_item_t item, const sensor_snapshot_t *snapshot, const wifi_status_t *wifi, char *out, size_t len)
{
    switch (item) {
        case SCREEN_ITEM_WATER_TEMP:
            snprintf(out, len, "Vanntemp %2.1fC", snapshot->water_temp_c);
            break;
        case SCREEN_ITEM_SEA_LEVEL:
            snprintf(out, len, "Sjoen %2.1fcm", snapshot->sea_level_cm);
            break;
        case SCREEN_ITEM_AIR_TEMP:
            snprintf(out, len, "Luft %2.1fC", snapshot->air_temp_c);
            break;
        case SCREEN_ITEM_HUMIDITY:
            snprintf(out, len, "Fukt %2.1f%%", snapshot->humidity_percent);
            break;
        case SCREEN_ITEM_PRESSURE:
            snprintf(out, len, "Trykk %4.0fhPa", snapshot->air_pressure_hpa);
            break;
        case SCREEN_ITEM_BATTERY_PERCENT:
            snprintf(out, len, "Batt %2.0f%%", snapshot->battery_percent);
            break;
        case SCREEN_ITEM_BATTERY_VOLTAGE:
            snprintf(out, len, "Batt %1.2fV", snapshot->battery_voltage);
            break;
        case SCREEN_ITEM_IP_ADDRESS: {
            char ip[32];
            ip_to_string(wifi->sta_connected ? &wifi->sta_ip : &wifi->ap_ip, ip, sizeof(ip));
            snprintf(out, len, "IP %s", ip);
            break;
        }
        default:
            strlcpy(out, "-", len);
            break;
    }
}

static void render_screen(uint8_t screen_index)
{
    clear_buffer();
    char line_buf[32];
    int line = 0;
    for (size_t bit = 0; bit < SCREEN_ITEM_COUNT && line < MAX_LINES; ++bit) {
        uint32_t mask = config_store_screen_item_bit(bit);
        if (mask == 0) {
            continue;
        }
        if (s_display_cfg.screen_items[screen_index] & mask) {
            format_line((screen_item_t)bit, &s_last_snapshot, &s_last_wifi, line_buf, sizeof(line_buf));
            draw_text_line(line++, line_buf);
        }
    }
    ssd1306_write_cmd(0x21);
    ssd1306_write_cmd(0);
    ssd1306_write_cmd(DISPLAY_WIDTH - 1);
    ssd1306_write_cmd(0x22);
    ssd1306_write_cmd(0);
    ssd1306_write_cmd((DISPLAY_HEIGHT / 8) - 1);
    ssd1306_write_data(s_framebuffer, sizeof(s_framebuffer));
}

static void filter_snapshot_display(const sensor_snapshot_t *incoming)
{
    const float jump_threshold_cm = 20.0f; // skill ut enkeltmålinger som hopper langt
    const float confirm_window_cm = 5.0f;  // to målinger som bekrefter hverandre må ligge innenfor dette

    if (!s_have_filtered) {
        s_filtered_snapshot = *incoming;
        s_have_filtered = true;
        s_last_snapshot = s_filtered_snapshot;
        return;
    }

    s_filtered_snapshot = *incoming;

    float prev = s_last_snapshot.sea_level_cm;
    float cand = incoming->sea_level_cm;
    float diff = cand - prev;

    // Avvis enkeltstående hopp > jump_threshold_cm med mindre de bekreftes av neste måling
    if (fabsf(diff) > jump_threshold_cm) {
        if (s_have_pending && fabsf(cand - s_pending_level) <= confirm_window_cm) {
            // andre gang vi ser omtrent samme hopp: aksepter
            s_have_pending = false;
        } else {
            // lagre som pending og behold forrige visningsverdi
            s_pending_level = cand;
            s_have_pending = true;
            cand = prev;
        }
    } else {
        // normalt sprang, nullstill pending
        s_have_pending = false;
    }

    // enkel lavpass for å glatte displayet
    s_filtered_snapshot.sea_level_cm = (prev * 0.7f) + (cand * 0.3f);

    s_last_snapshot = s_filtered_snapshot;
}

esp_err_t display_manager_init(const measurement_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    s_display_cfg = *config;
    power_manager_set(POWER_DOMAIN_DISPLAY, true);
    vTaskDelay(pdMS_TO_TICKS(DISPLAY_WAKE_DELAY_MS));
    ESP_RETURN_ON_ERROR(ssd1306_hw_init(), TAG, "ssd1306");
    s_display_powered = true;
    return ESP_OK;
}

void display_manager_update_config(const measurement_config_t *config)
{
    if (!config) {
        return;
    }
    s_display_cfg = *config;
}

void display_manager_show_snapshot(const sensor_snapshot_t *snapshot, const wifi_status_t *wifi_status)
{
    if (!snapshot || !wifi_status) {
        return;
    }
    filter_snapshot_display(snapshot);
    s_last_wifi = *wifi_status;
    s_have_snapshot = true;

    ensure_powered();
    schedule_sleep();
    if (!s_display_powered) {
        return;
    }
    render_screen(s_active_screen);
}

void display_manager_next_screen(void)
{
    if (!s_have_snapshot) {
        return;
    }
    s_active_screen = (s_active_screen + 1) % DISPLAY_SCREEN_COUNT;
    ensure_powered();
    schedule_sleep();
    if (!s_display_powered) {
        return;
    }
    render_screen(s_active_screen);
}
