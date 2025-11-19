#include "web_server.h"

#include "config_store.h"
#include "display_manager.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "scheduler.h"
#include "sensor_manager.h"
#include "wifi_manager.h"
#include "google_bridge.h"
#include "cJSON.h"
#include "esp_netif_ip_addr.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

#define TAG "web"
#define MAX_CONFIG_BODY_LEN 2048
#define GOOGLE_MAX_BODY_LEN 2048

static const char INDEX_HTML[] =
    "<!DOCTYPE html>\n"
    "<html lang=\"no\">\n"
    "<head>\n"
    "<meta charset=\"utf-8\"/>\n"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"/>\n"
    "<title>SeaMonitor</title>\n"
    "<style>\n"
    "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;margin:0;padding:20px;background:#0b1b2b;color:#f6f8fc;}\n"
    "h1{margin-top:0;}\n"
    "section,fieldset{background:#12263b;border:1px solid #1f3a56;border-radius:8px;padding:16px;margin-bottom:16px;}\n"
    "label{display:block;margin:8px 0 4px;}\n"
    "input,select,button,textarea{font-size:16px;padding:8px;border-radius:4px;border:1px solid#1f3a56;background:#0b1b2b;color:#f6f8fc;width:100%;box-sizing:border-box;}\n"
    "button{background:#1f7aec;border:none;cursor:pointer;}\n"
    ".dashboard{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:12px;margin-bottom:16px;}\n"
    ".metric-card{background:#12263b;border:1px solid #1f3a56;border-radius:8px;padding:12px;}\n"
    ".metric-card span{display:block;font-size:28px;margin-top:6px;font-weight:600;}\n"
    ".nav-stack{display:flex;flex-direction:column;gap:10px;margin-bottom:16px;}\n"
    ".nav-stack button{width:100%;padding:14px;font-size:18px;}\n"
    ".panel.hidden{display:none;}\n"
    ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:8px;}\n"
    ".screen-box{border:1px solid #1f3a56;border-radius:8px;padding:12px;}\n"
    ".screen-box h3{margin:0 0 8px;}\n"
    ".status{display:flex;gap:16px;flex-wrap:wrap;margin-bottom:12px;}\n"
    ".status div{flex:1 1 200px;}\n"
    "small{color:#9fb3c8;}\n"
    ".buttons{display:flex;gap:8px;flex-wrap:wrap;margin-top:12px;}\n"
    ".buttons button{flex:1;}\n"
    "#reboot-hint{margin-top:8px;color:#ffdf6b;}\n"
    ".inline-fields{display:flex;gap:6px;}\n"
    ".inline-fields input{width:100%;}\n"
    "textarea{min-height:120px;}\n"
    "</style>\n"
    "</head>\n"
    "<body>\n"
    "<h1>SeaMonitor kontrollpanel</h1>\n"
    "<section class=\"dashboard\">\n"
    "  <div class=\"metric-card\"><strong>Vanntemp</strong><span id=\"water-temp\">-</span></div>\n"
    "  <div class=\"metric-card\"><strong>Sjønivå</strong><span id=\"sea-level\">-</span></div>\n"
    "  <div class=\"metric-card\"><strong>Lufttemp</strong><span id=\"air-temp\">-</span></div>\n"
    "  <div class=\"metric-card\"><strong>Fuktighet</strong><span id=\"humidity\">-</span></div>\n"
    "  <div class=\"metric-card\"><strong>Trykk</strong><span id=\"pressure\">-</span></div>\n"
    "  <div class=\"metric-card\"><strong>Batteri</strong><span id=\"battery\">-</span></div>\n"
    "</section>\n"
    "<div id=\"metric-error\" style=\"color:#ff9d9d;margin-bottom:12px;display:none\">Fikk ikke hentet verdier</div>\n"
    "<section class=\"status\">\n"
    "  <div><strong>AP IP</strong><br/><span id=\"ap-ip\">-</span></div>\n"
    "  <div><strong>STA IP</strong><br/><span id=\"sta-ip\">-</span><br/><small id=\"sta-state\">Ikke tilkoblet</small></div>\n"
    "</section>\n"
    "<div class=\"nav-stack\">\n"
    "  <button type=\"button\" id=\"open-config\">Konfigurasjon</button>\n"
    "  <button type=\"button\" id=\"open-offset\">Offset</button>\n"
    "  <button type=\"button\" id=\"quick-restart\">Restart</button>\n"
    "  <button type=\"button\" id=\"quick-save-reboot\">Lagre og restart</button>\n"
    "</div>\n"
    "<section id=\"config-panel\" class=\"panel hidden\">\n"
    "<form id=\"config-form\">\n"
    "  <fieldset>\n"
    "    <legend>Navn og Wi-Fi</legend>\n"
    "    <label for=\"device-name\">Enhetsnavn</label>\n"
    "    <input id=\"device-name\" required maxlength=\"31\"/>\n"
    "    <label for=\"wifi-ssid\">Wi-Fi SSID</label>\n"
    "    <input id=\"wifi-ssid\" maxlength=\"31\" placeholder=\"(tom = kun SeaMonitor-AP)\"/>\n"
    "    <label for=\"wifi-pass\">Wi-Fi passord</label>\n"
    "    <input id=\"wifi-pass\" maxlength=\"63\" type=\"password\"/>\n"
    "  </fieldset>\n"
    "  <fieldset>\n"
    "    <legend>Måleintervaller (min/sek)</legend>\n"
    "    <div class=\"grid\" id=\"interval-grid\"></div>\n"
    "    <label for=\"display-seconds\">Skjerm på-tid (sekunder, 0=alltid)</label>\n"
    "    <input id=\"display-seconds\" type=\"number\" min=\"0\" max=\"3600\"/>\n"
    "  </fieldset>\n"
    "  <fieldset>\n"
    "    <legend>Skjermer</legend>\n"
    "    <div class=\"grid\">\n"
    "      <div class=\"screen-box\"><h3>Skjerm 1</h3><div id=\"screen1-options\"></div></div>\n"
    "      <div class=\"screen-box\"><h3>Skjerm 2</h3><div id=\"screen2-options\"></div><small>IP-adresse vises alltid her som fallback.</small></div>\n"
    "    </div>\n"
    "  </fieldset>\n"
    "  <fieldset id=\"offset-fieldset\">\n"
    "    <legend>Offsets</legend>\n"
    "    <p>Justér måleverdier dersom sensoren trenger kalibrering.</p>\n"
    "    <label for=\"offset-water\">Vanntemp (°C)</label>\n"
    "    <input id=\"offset-water\" type=\"number\" step=\"0.1\" min=\"-20\" max=\"20\"/>\n"
    "    <label for=\"offset-sea\">Sjønivå (cm)</label>\n"
    "    <input id=\"offset-sea\" type=\"number\" step=\"0.1\" min=\"-200\" max=\"200\"/>\n"
    "    <label for=\"offset-air\">Lufttemp (°C)</label>\n"
    "    <input id=\"offset-air\" type=\"number\" step=\"0.1\" min=\"-20\" max=\"20\"/>\n"
    "  </fieldset>\n"
    "  <div class=\"buttons\">\n"
    "    <button type=\"submit\" id=\"save-btn\">Lagre</button>\n"
    "    <button type=\"button\" id=\"save-reboot-btn\">Lagre og restart</button>\n"
    "    <button type=\"button\" id=\"reboot-btn\">Restart</button>\n"
    "  </div>\n"
    "  <div id=\"form-status\" aria-live=\"polite\"></div>\n"
    "  <div id=\"reboot-hint\" style=\"display:none\">Enheten restarter. Vent 10 sekunder og koble til på nytt.</div>\n"
    "</form>\n"
    "</section>\n"
    "<script>\n"
    "const form=document.getElementById('config-form');\n"
    "const statusEl=document.getElementById('form-status');\n"
    "const rebootHint=document.getElementById('reboot-hint');\n"
    "const configPanel=document.getElementById('config-panel');\n"
    "const panels=[configPanel];\n"
    "const intervals=[{key:'battery',label:'Batteri'},{key:'air',label:'Luft'},{key:'sea',label:'Sjø'},{key:'wifi',label:'Wi-Fi'},{key:'web_ui',label:'Web UI'}];\n"
    "const sensors=[\n"
    " {key:'water_temp',label:'Vanntemp'},\n"
    " {key:'sea_level',label:'Sjønivå'},\n"
    " {key:'air_temp',label:'Lufttemp'},\n"
    " {key:'humidity',label:'Fuktighet'},\n"
    " {key:'pressure',label:'Trykk'},\n"
    " {key:'battery_percent',label:'Batteri %'},\n"
    " {key:'battery_voltage',label:'Batteri V'},\n"
    " {key:'ip_address',label:'IP-adresse'}\n"
    "];\n"
    "const intervalGrid=document.getElementById('interval-grid');\n"
    "intervals.forEach(item=>{\n"
    "  const wrapper=document.createElement('div');\n"
    "  wrapper.innerHTML=`<label>${item.label}</label><div class=\"inline-fields\"><input type=number min=0 id=${item.key}-min placeholder=Minutter><input type=number min=0 max=59 id=${item.key}-sec placeholder=Sekunder></div>`;\n"
    "  intervalGrid.appendChild(wrapper);\n"
    "});\n"
    "function renderScreenOptions(targetId){const container=document.getElementById(targetId);container.innerHTML='';sensors.forEach(sensor=>{const id=`${targetId}-${sensor.key}`;const wrapper=document.createElement('label');wrapper.innerHTML=`<input type=\"checkbox\" id=${id} value=${sensor.key}> ${sensor.label}`;container.appendChild(wrapper);});}\n"
    "function showPanel(panel){panels.forEach(p=>p.classList.add('hidden'));if(panel){panel.classList.remove('hidden');panel.scrollIntoView({behavior:'smooth'});}}\n"
    "renderScreenOptions('screen1-options');\n"
    "renderScreenOptions('screen2-options');\n"
    "function setIntervalFields(prefix,data){document.getElementById(`${prefix}-min`).value=data?.minutes??0;document.getElementById(`${prefix}-sec`).value=data?.seconds??0;}\n"
    "function getIntervalFields(prefix){return{minutes:Number(document.getElementById(`${prefix}-min`).value)||0,seconds:Number(document.getElementById(`${prefix}-sec`).value)||0};}\n"
    "function setScreenSelections(targetId,values){sensors.forEach(sensor=>{const box=document.getElementById(`${targetId}-${sensor.key}`);if(box){box.checked=values.includes(sensor.key);}});}\n"
    "function collectScreenSelections(targetId){const result=[];sensors.forEach(sensor=>{const box=document.getElementById(`${targetId}-${sensor.key}`);if(box?.checked){result.push(sensor.key);}});return result;}\n"
    "async function loadStatus(){try{const res=await fetch('/api/status');const data=await res.json();document.getElementById('ap-ip').textContent=data.ap_ip||'-';document.getElementById('sta-ip').textContent=data.sta_ip||'-';document.getElementById('sta-state').textContent=data.sta_connected?'Tilkoblet':'Ikke tilkoblet';}catch(e){console.warn('status',e);}}\n"
    "function formatValue(val,suffix){if(val===undefined||val===null||Number.isNaN(val))return '-';return `${val}${suffix}`;}\n"
    "function formatNumber(val,suffix){if(val===undefined||val===null||Number.isNaN(val))return '-';const fixed=(Math.abs(val)<10)?val.toFixed(2):val.toFixed(1);return `${fixed}${suffix}`;}\n"
    "function renderMetrics(data){document.getElementById('water-temp').textContent=formatNumber(data.water_temp_c,'°C');document.getElementById('sea-level').textContent=formatNumber(data.sea_level_cm,' cm');document.getElementById('air-temp').textContent=formatNumber(data.air_temp_c,'°C');const humVal=typeof data.humidity_percent==='number'?data.humidity_percent.toFixed(1):null;document.getElementById('humidity').textContent=formatValue(humVal,'%');document.getElementById('pressure').textContent=formatNumber(data.air_pressure_hpa,' hPa');let batt='-';if(typeof data.battery_percent==='number'){const voltage=typeof data.battery_voltage==='number'?data.battery_voltage.toFixed(2)+'V':'';batt=`${data.battery_percent.toFixed(0)}% ${voltage?`(${voltage})`:''}`;}document.getElementById('battery').textContent=batt;}\n"
    "async function loadMetrics(){try{const res=await fetch('/api/metrics');const data=await res.json();renderMetrics(data);document.getElementById('metric-error').style.display='none';}catch(err){document.getElementById('metric-error').style.display='block';console.warn('metrics',err);}}\n"
    "async function loadConfig(){const res=await fetch('/api/config');const data=await res.json();setIntervalFields('battery',data.battery);setIntervalFields('air',data.air);setIntervalFields('sea',data.sea);setIntervalFields('wifi',data.wifi);setIntervalFields('web_ui',data.web_ui);document.getElementById('display-seconds').value=data.display_on_seconds;document.getElementById('device-name').value=data.device_name;document.getElementById('wifi-ssid').value=data.wifi_ssid||'';document.getElementById('wifi-pass').value=data.wifi_password||'';const screens=data.screens||{};setScreenSelections('screen1-options',screens.screen1||[]);setScreenSelections('screen2-options',screens.screen2||[]);const offsets=data.offsets||{};document.getElementById('offset-water').value=offsets.water_temp_c??0;document.getElementById('offset-sea').value=offsets.sea_level_cm??0;document.getElementById('offset-air').value=offsets.air_temp_c??0;}\n"
    "async function submitConfig(rebootAfter){const payload={battery:getIntervalFields('battery'),air:getIntervalFields('air'),sea:getIntervalFields('sea'),wifi:getIntervalFields('wifi'),web_ui:getIntervalFields('web_ui'),display_on_seconds:Number(document.getElementById('display-seconds').value)||0,device_name:document.getElementById('device-name').value.trim()||'sea',wifi_ssid:document.getElementById('wifi-ssid').value.trim(),wifi_password:document.getElementById('wifi-pass').value, screens:{screen1:collectScreenSelections('screen1-options'),screen2:collectScreenSelections('screen2-options')}, offsets:{water_temp_c:Number(document.getElementById('offset-water').value)||0,sea_level_cm:Number(document.getElementById('offset-sea').value)||0,air_temp_c:Number(document.getElementById('offset-air').value)||0}};statusEl.textContent='Lagrer...';rebootHint.style.display='none';try{const res=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});if(!res.ok) throw new Error('Feil '+res.status);statusEl.textContent='Lagret!';loadStatus();if(rebootAfter){await requestReboot();}}catch(err){statusEl.textContent='Feil: '+err.message;}setTimeout(()=>{if(statusEl.textContent==='Lagret!'){statusEl.textContent='';}},4000);}\n"
    "async function requestReboot(){statusEl.textContent='Restarter...';rebootHint.style.display='block';try{await fetch('/api/reboot',{method:'POST'});}catch(err){console.warn('reboot',err);}setTimeout(()=>{statusEl.textContent='Vent 10 sekunder mens enheten starter på nytt';},200);}\n"
    "form.addEventListener('submit',ev=>{ev.preventDefault();submitConfig(false);});\n"
    "document.getElementById('save-reboot-btn').addEventListener('click',()=>submitConfig(true));\n"
    "document.getElementById('reboot-btn').addEventListener('click',requestReboot);\n"
    "document.getElementById('open-config').addEventListener('click',()=>showPanel(configPanel));\n"
    "document.getElementById('open-offset').addEventListener('click',()=>{showPanel(configPanel);document.getElementById('offset-fieldset').scrollIntoView({behavior:'smooth'});});\n"
    "document.getElementById('quick-restart').addEventListener('click',requestReboot);\n"
    "document.getElementById('quick-save-reboot').addEventListener('click',()=>submitConfig(true));\n"
    "\n"
    "loadConfig();\n"
    "loadStatus();\n"
    "loadMetrics();\n"
    "setInterval(loadMetrics,5000);\n"
    "showPanel(configPanel);\n"
    "</script>\n"
    "</body></html>\n"
    "\n";

static httpd_handle_t s_server = NULL;
static measurement_config_t s_cached_config;

static void ip_to_string(const esp_ip4_addr_t *ip, char *out, size_t len)
{
    if (!out || len == 0) {
        return;
    }
    if (!ip || ip->addr == 0) {
        out[0] = '\0';
        return;
    }
    snprintf(out, len, IPSTR, IP2STR(ip));
}

static void reboot_task(void *param)
{
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
}

static void schedule_reboot(void)
{
    xTaskCreate(reboot_task, "reboot", 2048, NULL, 5, NULL);
}

static cJSON *interval_to_json(measurement_interval_t interval)
{
    cJSON *obj = cJSON_CreateObject();
    if (!obj) {
        return NULL;
    }
    cJSON_AddNumberToObject(obj, "minutes", interval.minutes);
    cJSON_AddNumberToObject(obj, "seconds", interval.seconds);
    return obj;
}

static esp_err_t handle_get_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handle_get_config(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddItemToObject(root, "battery", interval_to_json(s_cached_config.battery));
    cJSON_AddItemToObject(root, "air", interval_to_json(s_cached_config.air));
    cJSON_AddItemToObject(root, "sea", interval_to_json(s_cached_config.sea));
    cJSON_AddNumberToObject(root, "display_on_seconds", s_cached_config.display_on_seconds);
    cJSON_AddStringToObject(root, "device_name", s_cached_config.device_name);
    cJSON_AddItemToObject(root, "wifi", interval_to_json(s_cached_config.wifi));
    cJSON_AddItemToObject(root, "web_ui", interval_to_json(s_cached_config.web_ui));
    cJSON *screens = cJSON_CreateObject();
    if (screens) {
        cJSON *scr1 = cJSON_CreateArray();
        cJSON *scr2 = cJSON_CreateArray();
        if (scr1 && scr2) {
            for (size_t i = 0; i < config_store_screen_item_count(); ++i) {
                const char *name = config_store_screen_item_name(i);
                uint32_t bit = config_store_screen_item_bit(i);
                if ((s_cached_config.screen_items[0] & bit) && name) {
                    cJSON_AddItemToArray(scr1, cJSON_CreateString(name));
                }
                if ((s_cached_config.screen_items[1] & bit) && name) {
                    cJSON_AddItemToArray(scr2, cJSON_CreateString(name));
                }
            }
            cJSON_AddItemToObject(screens, "screen1", scr1);
            cJSON_AddItemToObject(screens, "screen2", scr2);
            cJSON_AddItemToObject(root, "screens", screens);
        } else {
            cJSON_Delete(scr1);
            cJSON_Delete(scr2);
            cJSON_Delete(screens);
        }
    }
    cJSON_AddStringToObject(root, "wifi_ssid", s_cached_config.wifi_ssid);
    cJSON_AddStringToObject(root, "wifi_password", s_cached_config.wifi_password);
    cJSON *offsets = cJSON_CreateObject();
    if (offsets) {
        cJSON_AddNumberToObject(offsets, "water_temp_c", s_cached_config.offsets.water_temp_c);
        cJSON_AddNumberToObject(offsets, "sea_level_cm", s_cached_config.offsets.sea_level_cm);
        cJSON_AddNumberToObject(offsets, "air_temp_c", s_cached_config.offsets.air_temp_c);
        cJSON_AddItemToObject(root, "offsets", offsets);
    }

    const char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);

    cJSON_free((void *)json);
    cJSON_Delete(root);
    return ESP_OK;
}

static bool json_to_interval(const cJSON *obj, measurement_interval_t *out)
{
    if (!cJSON_IsObject(obj) || !out) {
        return false;
    }
    const cJSON *min = cJSON_GetObjectItem(obj, "minutes");
    const cJSON *sec = cJSON_GetObjectItem(obj, "seconds");
    if (!cJSON_IsNumber(min) || !cJSON_IsNumber(sec)) {
        return false;
    }
    out->minutes = (uint16_t)cJSON_GetNumberValue(min);
    out->seconds = (uint8_t)cJSON_GetNumberValue(sec);
    config_store_normalize_interval(out);
    return true;
}

static esp_err_t handle_post_config(httpd_req_t *req)
{
    size_t total_len = req->content_len;
    if (total_len == 0 || total_len > MAX_CONFIG_BODY_LEN) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body too large");
        return ESP_FAIL;
    }

    char *buf = (char *)malloc(total_len + 1);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
        return ESP_ERR_NO_MEM;
    }

    size_t received = 0;
    while (received < total_len) {
        int r = httpd_req_recv(req, buf + received, total_len - received);
        if (r <= 0) {
            free(buf);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Incomplete body");
            return ESP_FAIL;
        }
        received += r;
    }
    buf[received] = '\0';

    cJSON *root = cJSON_ParseWithLength(buf, received);
    free(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    measurement_config_t new_cfg = s_cached_config;
    const cJSON *battery = cJSON_GetObjectItem(root, "battery");
    const cJSON *air = cJSON_GetObjectItem(root, "air");
    const cJSON *sea = cJSON_GetObjectItem(root, "sea");
    const cJSON *display = cJSON_GetObjectItem(root, "display_on_seconds");
    const cJSON *name = cJSON_GetObjectItem(root, "device_name");
    const cJSON *wifi = cJSON_GetObjectItem(root, "wifi");
    const cJSON *web_ui = cJSON_GetObjectItem(root, "web_ui");
    const cJSON *ssid = cJSON_GetObjectItem(root, "wifi_ssid");
    const cJSON *password = cJSON_GetObjectItem(root, "wifi_password");
    const cJSON *screens = cJSON_GetObjectItem(root, "screens");
    const cJSON *offsets = cJSON_GetObjectItem(root, "offsets");

    bool ok = true;
    ok &= json_to_interval(battery, &new_cfg.battery);
    ok &= json_to_interval(air, &new_cfg.air);
    ok &= json_to_interval(sea, &new_cfg.sea);
    if (cJSON_IsNumber(display)) {
        new_cfg.display_on_seconds = (uint16_t)cJSON_GetNumberValue(display);
    } else {
        ok = false;
    }
    ok &= json_to_interval(wifi, &new_cfg.wifi);
    ok &= json_to_interval(web_ui, &new_cfg.web_ui);
    if (cJSON_IsString(name)) {
        strlcpy(new_cfg.device_name, cJSON_GetStringValue(name), sizeof(new_cfg.device_name));
    } else {
        ok = false;
    }
    if (cJSON_IsString(ssid)) {
        strlcpy(new_cfg.wifi_ssid, cJSON_GetStringValue(ssid), sizeof(new_cfg.wifi_ssid));
    } else {
        ok = false;
    }
    if (cJSON_IsString(password)) {
        strlcpy(new_cfg.wifi_password, cJSON_GetStringValue(password), sizeof(new_cfg.wifi_password));
    } else {
        ok = false;
    }
    if (cJSON_IsObject(screens)) {
        const cJSON *screen1 = cJSON_GetObjectItem(screens, "screen1");
        const cJSON *screen2 = cJSON_GetObjectItem(screens, "screen2");
        uint32_t mask1 = 0;
        uint32_t mask2 = 0;
        if (cJSON_IsArray(screen1)) {
            cJSON *item = NULL;
            cJSON_ArrayForEach(item, screen1) {
                if (cJSON_IsString(item)) {
                    uint32_t bit = 0;
                    if (config_store_screen_name_to_bit(item->valuestring, &bit)) {
                        mask1 |= bit;
                    }
                }
            }
        }
        if (cJSON_IsArray(screen2)) {
            cJSON *item = NULL;
            cJSON_ArrayForEach(item, screen2) {
                if (cJSON_IsString(item)) {
                    uint32_t bit = 0;
                    if (config_store_screen_name_to_bit(item->valuestring, &bit)) {
                        mask2 |= bit;
                    }
                }
            }
        }
        new_cfg.screen_items[0] = mask1;
        new_cfg.screen_items[1] = mask2;
    } else {
        ok = false;
    }
    if (cJSON_IsObject(offsets)) {
        const cJSON *water = cJSON_GetObjectItem(offsets, "water_temp_c");
        const cJSON *sea = cJSON_GetObjectItem(offsets, "sea_level_cm");
        const cJSON *air = cJSON_GetObjectItem(offsets, "air_temp_c");
        if (cJSON_IsNumber(water) && cJSON_IsNumber(sea) && cJSON_IsNumber(air)) {
            new_cfg.offsets.water_temp_c = (float)cJSON_GetNumberValue(water);
            new_cfg.offsets.sea_level_cm = (float)cJSON_GetNumberValue(sea);
            new_cfg.offsets.air_temp_c = (float)cJSON_GetNumberValue(air);
        } else {
            ok = false;
        }
    } else {
        ok = false;
    }

    if (!ok) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing fields");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    esp_err_t err = config_store_set(&new_cfg);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Persist failed");
        cJSON_Delete(root);
        return err;
    }

    s_cached_config = new_cfg;
    google_bridge_update_config(&s_cached_config);
    scheduler_apply_config(&s_cached_config);
    wifi_manager_update_config(&s_cached_config);
    display_manager_update_config(&s_cached_config);

    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);

    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t handle_post_reboot(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    schedule_reboot();
    return ESP_OK;
}

static esp_err_t handle_get_status(httpd_req_t *req)
{
    wifi_status_t status = wifi_manager_get_status();
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    char ap_ip[16];
    char sta_ip[16];
    ip_to_string(&status.ap_ip, ap_ip, sizeof(ap_ip));
    ip_to_string(&status.sta_ip, sta_ip, sizeof(sta_ip));
    cJSON_AddStringToObject(root, "ap_ip", ap_ip);
    cJSON_AddStringToObject(root, "sta_ip", sta_ip);
    cJSON_AddBoolToObject(root, "sta_connected", status.sta_connected);
    const char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    cJSON_free((void *)json);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t handle_get_metrics(httpd_req_t *req)
{
    sensor_snapshot_t snapshot;
    sensor_manager_get_snapshot(&snapshot);

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddNumberToObject(root, "water_temp_c", snapshot.water_temp_c);
    cJSON_AddNumberToObject(root, "sea_level_cm", snapshot.sea_level_cm);
    cJSON_AddNumberToObject(root, "air_temp_c", snapshot.air_temp_c);
    cJSON_AddNumberToObject(root, "humidity_percent", snapshot.humidity_percent);
    cJSON_AddNumberToObject(root, "air_pressure_hpa", snapshot.air_pressure_hpa);
    cJSON_AddNumberToObject(root, "battery_percent", snapshot.battery_percent);
    cJSON_AddNumberToObject(root, "battery_voltage", snapshot.battery_voltage);

    const char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    cJSON_free((void *)json);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t handle_get_google_state(httpd_req_t *req)
{
    sensor_snapshot_t snapshot;
    int64_t ts_us = 0;
    bool from_cache = google_bridge_get_last_snapshot(&snapshot, &ts_us);
    if (!from_cache) {
        sensor_manager_get_snapshot(&snapshot);
        ts_us = esp_timer_get_time();
    }
    wifi_status_t status = wifi_manager_get_status();

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddBoolToObject(root, "cached", from_cache);
    if (ts_us > 0) {
        int64_t age_ms = (esp_timer_get_time() - ts_us) / 1000;
        if (age_ms < 0) age_ms = 0;
        cJSON_AddNumberToObject(root, "age_ms", (double)age_ms);
    }
    cJSON_AddNumberToObject(root, "water_temp_c", snapshot.water_temp_c);
    cJSON_AddNumberToObject(root, "sea_level_cm", snapshot.sea_level_cm);
    cJSON_AddNumberToObject(root, "air_temp_c", snapshot.air_temp_c);
    cJSON_AddNumberToObject(root, "humidity_percent", snapshot.humidity_percent);
    cJSON_AddNumberToObject(root, "air_pressure_hpa", snapshot.air_pressure_hpa);
    cJSON_AddNumberToObject(root, "battery_percent", snapshot.battery_percent);
    cJSON_AddNumberToObject(root, "battery_voltage", snapshot.battery_voltage);

    cJSON *wifi = cJSON_AddObjectToObject(root, "wifi");
    if (wifi) {
        char ap_ip[16];
        char sta_ip[16];
        ip_to_string(&status.ap_ip, ap_ip, sizeof(ap_ip));
        ip_to_string(&status.sta_ip, sta_ip, sizeof(sta_ip));
        cJSON_AddBoolToObject(wifi, "sta_connected", status.sta_connected);
        cJSON_AddStringToObject(wifi, "sta_ip", sta_ip);
        cJSON_AddStringToObject(wifi, "ap_ip", ap_ip);
    }

    const char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    cJSON_free((void *)json);
    cJSON_Delete(root);
    return ESP_OK;
}

static void google_add_supported_sensor(cJSON *array, const char *name, const char *unit)
{
    if (!array || !name) {
        return;
    }
    cJSON *entry = cJSON_CreateObject();
    if (!entry) {
        return;
    }
    cJSON_AddStringToObject(entry, "name", name);
    if (unit) {
        cJSON_AddStringToObject(entry, "unit", unit);
    }
    cJSON_AddItemToArray(array, entry);
}

static void google_fill_state(cJSON *state, const sensor_snapshot_t *snapshot, const wifi_status_t *wifi)
{
    if (!state || !snapshot) {
        return;
    }
    wifi_status_t wifi_status = wifi ? *wifi : wifi_manager_get_status();
    bool online = wifi_status.sta_connected || wifi_status.ap_ip.addr != 0;
    cJSON_AddBoolToObject(state, "online", online);
    cJSON_AddNumberToObject(state, "temperatureAmbientCelsius", snapshot->air_temp_c);
    cJSON_AddNumberToObject(state, "humidityAmbientPercent", snapshot->humidity_percent);
    cJSON_AddBoolToObject(state, "on", google_bridge_is_automation_enabled());
    cJSON *custom = cJSON_AddObjectToObject(state, "customState");
    if (custom) {
        cJSON_AddNumberToObject(custom, "waterTempC", snapshot->water_temp_c);
        cJSON_AddNumberToObject(custom, "waterLevelCm", snapshot->sea_level_cm);
        cJSON_AddNumberToObject(custom, "airTempC", snapshot->air_temp_c);
        cJSON_AddNumberToObject(custom, "airPressureHpa", snapshot->air_pressure_hpa);
        cJSON_AddNumberToObject(custom, "batteryPercent", snapshot->battery_percent);
        cJSON_AddNumberToObject(custom, "batteryVoltage", snapshot->battery_voltage);
    }
}

static void google_add_device_descriptor(cJSON *devices)
{
    if (!devices) {
        return;
    }
    cJSON *device = cJSON_CreateObject();
    if (!device) {
        return;
    }
    cJSON_AddStringToObject(device, "id", google_bridge_device_id());
    cJSON_AddStringToObject(device, "type", "action.devices.types.SENSOR");
    cJSON *traits = cJSON_AddArrayToObject(device, "traits");
    if (traits) {
        cJSON_AddItemToArray(traits, cJSON_CreateString("action.devices.traits.SensorState"));
        cJSON_AddItemToArray(traits, cJSON_CreateString("action.devices.traits.OnOff"));
    }
    cJSON *name = cJSON_AddObjectToObject(device, "name");
    if (name) {
        cJSON_AddStringToObject(name, "name", google_bridge_friendly_name());
    }
    cJSON_AddBoolToObject(device, "willReportState", false);
    cJSON *attributes = cJSON_AddObjectToObject(device, "attributes");
    if (attributes) {
        cJSON *supported = cJSON_AddArrayToObject(attributes, "sensorStatesSupported");
        google_add_supported_sensor(supported, "airTemperatureC", "degC");
        google_add_supported_sensor(supported, "humidityPercent", "pct");
        google_add_supported_sensor(supported, "waterTemperatureC", "degC");
        google_add_supported_sensor(supported, "waterLevelCm", "cm");
        google_add_supported_sensor(supported, "airPressureHpa", "hPa");
        google_add_supported_sensor(supported, "batteryPercent", "pct");
    }
    cJSON *device_info = cJSON_AddObjectToObject(device, "deviceInfo");
    if (device_info) {
        cJSON_AddStringToObject(device_info, "manufacturer", "SeaMonitor");
        cJSON_AddStringToObject(device_info, "model", "esp32-dock");
        cJSON_AddStringToObject(device_info, "hwVersion", "1");
        cJSON_AddStringToObject(device_info, "swVersion", "1.0");
    }
    cJSON *custom = cJSON_AddObjectToObject(device, "customData");
    if (custom) {
        cJSON_AddStringToObject(custom, "stateEndpoint", "/api/google/state");
        cJSON_AddStringToObject(custom, "homegraphEndpoint", "/api/google/homegraph");
    }
    cJSON_AddItemToArray(devices, device);
}

static cJSON *google_parse_body(httpd_req_t *req)
{
    size_t total_len = req->content_len;
    if (total_len == 0 || total_len > GOOGLE_MAX_BODY_LEN) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body too large");
        return NULL;
    }
    char *buf = (char *)malloc(total_len + 1);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
        return NULL;
    }
    size_t received = 0;
    while (received < total_len) {
        int r = httpd_req_recv(req, buf + received, total_len - received);
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            free(buf);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Incomplete body");
            return NULL;
        }
        received += r;
    }
    buf[received] = '\0';
    cJSON *root = cJSON_ParseWithLength(buf, received);
    free(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    }
    return root;
}

static esp_err_t google_handle_sync(httpd_req_t *req, cJSON *payload)
{
    if (!payload) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON_AddStringToObject(payload, "agentUserId", google_bridge_agent_user_id());
    cJSON *devices = cJSON_AddArrayToObject(payload, "devices");
    if (!devices) {
        return ESP_ERR_NO_MEM;
    }
    google_add_device_descriptor(devices);
    return ESP_OK;
}

static esp_err_t google_handle_query(httpd_req_t *req, cJSON *payload, const cJSON *input)
{
    if (!payload || !input) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *devices_obj = cJSON_AddObjectToObject(payload, "devices");
    if (!devices_obj) {
        return ESP_ERR_NO_MEM;
    }
    sensor_snapshot_t snapshot;
    sensor_manager_get_snapshot(&snapshot);
    wifi_status_t wifi = wifi_manager_get_status();

    const cJSON *input_payload = cJSON_GetObjectItem(input, "payload");
    const cJSON *devices = input_payload ? cJSON_GetObjectItem(input_payload, "devices") : NULL;
    if (!cJSON_IsArray(devices) || cJSON_GetArraySize(devices) == 0) {
        cJSON *state = cJSON_CreateObject();
        if (!state) {
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddStringToObject(state, "status", "SUCCESS");
        google_fill_state(state, &snapshot, &wifi);
        cJSON_AddItemToObject(devices_obj, google_bridge_device_id(), state);
        return ESP_OK;
    }
    cJSON *device = NULL;
    cJSON_ArrayForEach(device, devices) {
        const cJSON *id_obj = cJSON_GetObjectItem(device, "id");
        const char *dev_id = cJSON_IsString(id_obj) ? id_obj->valuestring : google_bridge_device_id();
        cJSON *state = cJSON_CreateObject();
        if (!state) {
            return ESP_ERR_NO_MEM;
        }
        if (strcmp(dev_id, google_bridge_device_id()) == 0) {
            cJSON_AddStringToObject(state, "status", "SUCCESS");
            google_fill_state(state, &snapshot, &wifi);
        } else {
            cJSON_AddStringToObject(state, "status", "ERROR");
            cJSON_AddStringToObject(state, "errorCode", "deviceNotFound");
        }
        cJSON_AddItemToObject(devices_obj, dev_id, state);
    }
    return ESP_OK;
}

static esp_err_t google_handle_execute(httpd_req_t *req, cJSON *payload, const cJSON *input)
{
    if (!payload || !input) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *commands_resp = cJSON_AddArrayToObject(payload, "commands");
    if (!commands_resp) {
        return ESP_ERR_NO_MEM;
    }
    sensor_snapshot_t snapshot;
    sensor_manager_get_snapshot(&snapshot);
    wifi_status_t wifi = wifi_manager_get_status();

    const cJSON *input_payload = cJSON_GetObjectItem(input, "payload");
    const cJSON *commands = input_payload ? cJSON_GetObjectItem(input_payload, "commands") : NULL;
    if (!cJSON_IsArray(commands)) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *cmd = NULL;
    cJSON_ArrayForEach(cmd, commands) {
        cJSON *result = cJSON_CreateObject();
        if (!result) {
            return ESP_ERR_NO_MEM;
        }
        cJSON *ids = cJSON_AddArrayToObject(result, "ids");
        bool targets_device = false;
        const cJSON *devices = cJSON_GetObjectItem(cmd, "devices");
        if (cJSON_IsArray(devices)) {
            cJSON *entry = NULL;
            cJSON_ArrayForEach(entry, devices) {
                const cJSON *id_obj = cJSON_GetObjectItem(entry, "id");
                const char *dev_id = cJSON_IsString(id_obj) ? id_obj->valuestring : google_bridge_device_id();
                cJSON_AddItemToArray(ids, cJSON_CreateString(dev_id));
                if (strcmp(dev_id, google_bridge_device_id()) == 0) {
                    targets_device = true;
                }
            }
        }
        if (!targets_device) {
            cJSON_AddStringToObject(result, "status", "ERROR");
            cJSON_AddStringToObject(result, "errorCode", "deviceNotFound");
            cJSON_AddItemToArray(commands_resp, result);
            continue;
        }
        bool supported = false;
        const cJSON *execution = cJSON_GetObjectItem(cmd, "execution");
        if (cJSON_IsArray(execution)) {
            cJSON *exec_item = NULL;
            cJSON_ArrayForEach(exec_item, execution) {
                const cJSON *cmd_obj = cJSON_GetObjectItem(exec_item, "command");
                const char *cmd_name = cJSON_IsString(cmd_obj) ? cmd_obj->valuestring : NULL;
                if (!cmd_name) {
                    continue;
                }
                if (strcmp(cmd_name, "action.devices.commands.OnOff") == 0) {
                    const cJSON *params = cJSON_GetObjectItem(exec_item, "params");
                    const cJSON *on = params ? cJSON_GetObjectItem(params, "on") : NULL;
                    bool enable = !cJSON_IsBool(on) ? true : cJSON_IsTrue(on);
                    google_bridge_set_automation_enabled(enable);
                    supported = true;
                } else if (strcmp(cmd_name, "action.devices.commands.Reboot") == 0) {
                    supported = true;
                    schedule_reboot();
                }
            }
        }
        if (!supported) {
            cJSON_AddStringToObject(result, "status", "ERROR");
            cJSON_AddStringToObject(result, "errorCode", "functionNotSupported");
            cJSON_AddItemToArray(commands_resp, result);
            continue;
        }
        cJSON_AddStringToObject(result, "status", "SUCCESS");
        cJSON *states = cJSON_AddObjectToObject(result, "states");
        if (states) {
            google_fill_state(states, &snapshot, &wifi);
        }
        cJSON_AddItemToArray(commands_resp, result);
    }
    return ESP_OK;
}

static esp_err_t handle_post_google_homegraph(httpd_req_t *req)
{
    cJSON *root = google_parse_body(req);
    if (!root) {
        return ESP_FAIL;
    }
    const cJSON *request_id = cJSON_GetObjectItem(root, "requestId");
    const char *req_id = cJSON_IsString(request_id) ? request_id->valuestring : "local";
    const cJSON *inputs = cJSON_GetObjectItem(root, "inputs");
    if (!cJSON_IsArray(inputs) || cJSON_GetArraySize(inputs) == 0) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing inputs");
        return ESP_FAIL;
    }
    const cJSON *first_input = cJSON_GetArrayItem(inputs, 0);
    const cJSON *intent_obj = cJSON_GetObjectItem(first_input, "intent");
    const char *intent = cJSON_IsString(intent_obj) ? intent_obj->valuestring : NULL;
    if (!intent) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing intent");
        return ESP_FAIL;
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "requestId", req_id);
    cJSON *payload = cJSON_AddObjectToObject(resp, "payload");
    esp_err_t err = ESP_ERR_NOT_SUPPORTED;

    if (strcmp(intent, "action.devices.SYNC") == 0) {
        err = google_handle_sync(req, payload);
    } else if (strcmp(intent, "action.devices.QUERY") == 0) {
        err = google_handle_query(req, payload, first_input);
    } else if (strcmp(intent, "action.devices.EXECUTE") == 0) {
        err = google_handle_execute(req, payload, first_input);
    } else {
        cJSON_AddStringToObject(payload, "errorCode", "intentNotSupported");
    }

    const char *json = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    cJSON_free((void *)json);
    cJSON_Delete(resp);
    cJSON_Delete(root);

    return (err == ESP_OK) ? ESP_OK : ESP_FAIL;
}

static const httpd_uri_t get_config_uri = {
    .uri = "/api/config",
    .method = HTTP_GET,
    .handler = handle_get_config,
};

static const httpd_uri_t post_config_uri = {
    .uri = "/api/config",
    .method = HTTP_POST,
    .handler = handle_post_config,
};

static const httpd_uri_t status_uri = {
    .uri = "/api/status",
    .method = HTTP_GET,
    .handler = handle_get_status,
};

static const httpd_uri_t metrics_uri = {
    .uri = "/api/metrics",
    .method = HTTP_GET,
    .handler = handle_get_metrics,
};

static const httpd_uri_t google_uri = {
    .uri = "/api/google/state",
    .method = HTTP_GET,
    .handler = handle_get_google_state,
};

static const httpd_uri_t google_homegraph_uri = {
    .uri = "/api/google/homegraph",
    .method = HTTP_POST,
    .handler = handle_post_google_homegraph,
};

static const httpd_uri_t root_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = handle_get_root,
};

static const httpd_uri_t reboot_uri = {
    .uri = "/api/reboot",
    .method = HTTP_POST,
    .handler = handle_post_reboot,
};

esp_err_t web_server_start(void)
{
    if (s_server) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 10;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return err;
    }

    s_cached_config = config_store_get();

    httpd_register_uri_handler(s_server, &get_config_uri);
    httpd_register_uri_handler(s_server, &post_config_uri);
    httpd_register_uri_handler(s_server, &status_uri);
    httpd_register_uri_handler(s_server, &metrics_uri);
    httpd_register_uri_handler(s_server, &google_uri);
    httpd_register_uri_handler(s_server, &google_homegraph_uri);
    httpd_register_uri_handler(s_server, &root_uri);
    httpd_register_uri_handler(s_server, &reboot_uri);

    ESP_LOGI(TAG, "Web server started");
    return ESP_OK;
}

void web_server_update_config(const measurement_config_t *config)
{
    if (config) {
        s_cached_config = *config;
        display_manager_update_config(config);
    }
}
