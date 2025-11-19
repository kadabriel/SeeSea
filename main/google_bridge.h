#pragma once

#include "sensor_manager.h"
#include "config_store.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t google_bridge_init(void);
void google_bridge_publish_snapshot(const sensor_snapshot_t *snapshot);
bool google_bridge_get_last_snapshot(sensor_snapshot_t *out, int64_t *timestamp_us);
void google_bridge_update_config(const measurement_config_t *cfg);
const char *google_bridge_device_id(void);
const char *google_bridge_friendly_name(void);
const char *google_bridge_agent_user_id(void);
void google_bridge_set_automation_enabled(bool enabled);
bool google_bridge_is_automation_enabled(void);

#ifdef __cplusplus
}
#endif
