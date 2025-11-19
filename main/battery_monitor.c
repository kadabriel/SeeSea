#include "battery_monitor.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "hal/adc_types.h"
#include "esp_log.h"
#include "esp_check.h"

#define TAG "battery"
#define BATTERY_ADC_UNIT ADC_UNIT_1
#define BATTERY_ADC_CHANNEL ADC_CHANNEL_4 // GPIO32
#define BATTERY_DIVIDER_R1 220000.0f
#define BATTERY_DIVIDER_R2 100000.0f
#define BATTERY_MIN_V 3.3f
#define BATTERY_MAX_V 4.2f

static adc_oneshot_unit_handle_t s_adc_handle;
static adc_cali_handle_t s_cali_handle;
static bool s_ready = false;

static float scale_voltage(float measured_v)
{
    const float ratio = (BATTERY_DIVIDER_R1 + BATTERY_DIVIDER_R2) / BATTERY_DIVIDER_R2;
    return measured_v * ratio;
}

static float voltage_to_percent(float vbatt)
{
    if (vbatt <= BATTERY_MIN_V) {
        return 0.0f;
    }
    if (vbatt >= BATTERY_MAX_V) {
        return 100.0f;
    }
    return (vbatt - BATTERY_MIN_V) / (BATTERY_MAX_V - BATTERY_MIN_V) * 100.0f;
}

esp_err_t battery_monitor_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = BATTERY_ADC_UNIT,
        .clk_src = ADC_DIGI_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit_cfg, &s_adc_handle), TAG, "adc unit");

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc_handle, BATTERY_ADC_CHANNEL, &chan_cfg), TAG, "adc chan");

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id = BATTERY_ADC_UNIT,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_line_fitting(&cali_cfg, &s_cali_handle) == ESP_OK) {
        ESP_LOGI(TAG, "ADC calibration ready");
    } else {
        ESP_LOGW(TAG, "ADC calibration unavailable, using raw readings");
        s_cali_handle = NULL;
    }
#else
    s_cali_handle = NULL;
#endif

    s_ready = true;
    return ESP_OK;
}

esp_err_t battery_monitor_read(float *voltage, float *percent)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    int raw = 0;
    esp_err_t err = adc_oneshot_read(s_adc_handle, BATTERY_ADC_CHANNEL, &raw);
    if (err != ESP_OK) {
        return err;
    }
    int mv = 0;
    if (s_cali_handle) {
        adc_cali_raw_to_voltage(s_cali_handle, raw, &mv);
    } else {
        // Approximate: default 12-bit, 3.3 V reference
        mv = (int)((float)raw / 4095.0f * 3300.0f);
    }
    float measured_v = mv / 1000.0f;
    float vbatt = scale_voltage(measured_v);
    if (voltage) {
        *voltage = vbatt;
    }
    if (percent) {
        *percent = voltage_to_percent(vbatt);
    }
    return ESP_OK;
}
