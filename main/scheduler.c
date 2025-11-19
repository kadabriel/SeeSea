#include "scheduler.h"

#include "esp_timer.h"
#include "esp_log.h"
#include "esp_check.h"

#define TAG "scheduler"

typedef struct {
    scheduler_callback_t cb;
    void *ctx;
    esp_timer_handle_t timer;
    measurement_interval_t interval;
    scheduler_task_id_t id;
} scheduler_entry_t;

static scheduler_entry_t s_tasks[SCHED_TASK_SEA + 1];
static measurement_config_t s_config;

static void timer_callback(void *arg)
{
    scheduler_entry_t *entry = (scheduler_entry_t *)arg;
    if (entry->cb) {
        entry->cb(entry->ctx);
    }
}

static esp_err_t start_timer_for_entry(scheduler_entry_t *entry)
{
    const uint32_t period_seconds = config_store_interval_to_seconds(entry->interval);
    if (period_seconds == 0) {
        ESP_LOGW(TAG, "Task %d interval 0 => disabled", entry->id);
        if (entry->timer) {
            ESP_ERROR_CHECK(esp_timer_stop(entry->timer));
        }
        return ESP_OK;
    }

    const uint64_t period_us = (uint64_t)period_seconds * 1000000ULL;

    if (!entry->timer) {
        const esp_timer_create_args_t args = {
            .callback = timer_callback,
            .arg = entry,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "measure"
        };
        ESP_RETURN_ON_ERROR(esp_timer_create(&args, &entry->timer), TAG, "create timer");
    } else {
        ESP_ERROR_CHECK(esp_timer_stop(entry->timer));
    }

    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(entry->timer, period_us), TAG, "start timer");
    ESP_LOGI(TAG, "Task %d scheduled every %lus", entry->id, (unsigned long)period_seconds);
    return ESP_OK;
}

static void apply_intervals(void)
{
    s_tasks[SCHED_TASK_BATTERY].interval = s_config.battery;
    s_tasks[SCHED_TASK_AIR].interval = s_config.air;
    s_tasks[SCHED_TASK_SEA].interval = s_config.sea;

    for (size_t i = 0; i < sizeof(s_tasks) / sizeof(s_tasks[0]); ++i) {
        scheduler_entry_t *entry = &s_tasks[i];
        if (entry->cb) {
        esp_err_t err = start_timer_for_entry(entry);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to schedule task %d: %s", entry->id, esp_err_to_name(err));
        }
        }
    }
}

esp_err_t scheduler_init(const measurement_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    s_config = *config;

    for (size_t i = 0; i < sizeof(s_tasks) / sizeof(s_tasks[0]); ++i) {
        s_tasks[i].cb = NULL;
        s_tasks[i].ctx = NULL;
        s_tasks[i].timer = NULL;
        s_tasks[i].id = (scheduler_task_id_t)i;
    }

    apply_intervals();
    return ESP_OK;
}

void scheduler_register_task(scheduler_task_id_t task_id, scheduler_callback_t cb, void *ctx)
{
    if (task_id > SCHED_TASK_SEA) {
        return;
    }
    s_tasks[task_id].cb = cb;
    s_tasks[task_id].ctx = ctx;

    if (config_store_interval_to_seconds(s_tasks[task_id].interval) > 0) {
        esp_err_t err = start_timer_for_entry(&s_tasks[task_id]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start task %d: %s", task_id, esp_err_to_name(err));
        }
    }
}

esp_err_t scheduler_apply_config(const measurement_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    s_config = *config;
    apply_intervals();
    return ESP_OK;
}

void scheduler_stop(void)
{
    for (size_t i = 0; i < sizeof(s_tasks) / sizeof(s_tasks[0]); ++i) {
        if (s_tasks[i].timer) {
            esp_timer_stop(s_tasks[i].timer);
            esp_timer_delete(s_tasks[i].timer);
            s_tasks[i].timer = NULL;
        }
        s_tasks[i].cb = NULL;
    }
}
