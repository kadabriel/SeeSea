#pragma once

#include "config_store.h"
#include "esp_err.h"
#include <stdbool.h>

typedef void (*scheduler_callback_t)(void *ctx);

typedef enum {
    SCHED_TASK_BATTERY,
    SCHED_TASK_AIR,
    SCHED_TASK_SEA,
} scheduler_task_id_t;

esp_err_t scheduler_init(const measurement_config_t *config);
void scheduler_register_task(scheduler_task_id_t task_id, scheduler_callback_t cb, void *ctx);
esp_err_t scheduler_apply_config(const measurement_config_t *config);
void scheduler_stop(void);

