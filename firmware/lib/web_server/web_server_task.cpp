#ifdef ARDUINO
#include "web_server_task.h"
#include <Arduino.h>
#include <SPIFFS.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <time.h>
#include <string.h>

#include "web_core.h"
#include "cfg.h"
#include "cfg_keys.h"
#include "mb_core.h"
#include "mb_master.h"
#include "modbus_master_task.h"
#include "mb_log.h"
#include "wifi_manager_task.h"
#include "ntp_task.h"
#include "bus_scan.h"
#include "scan_task.h"
#include "wind_poll_task.h"

#define STATUS_BROADCAST_INTERVAL_MS 1000u
#define EXPLORER_REPLY_TIMEOUT_MS    2000u

static AsyncWebServer s_server(80);
static AsyncWebSocket s_ws("/ws");

/* ---------------------------------------------------------------------------
 * Small helpers
 * --------------------------------------------------------------------------- */

static void send_ok(AsyncWebServerRequest *request, bool ok)
{
    request->send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

/** @brief Submit one request directly to modbus_master_task's queue and wait for the reply — used only by the Register Explorer, which is deliberately not driven by a background task (scratchbook.md §7). */
static bool explorer_transact(const mb_request_t *req, mb_result_t *out_result)
{
    QueueHandle_t reply_queue = xQueueCreate(1, sizeof(mb_result_t));
    mb_task_request_t task_req;
    memset(&task_req, 0, sizeof(task_req));
    task_req.request  = *req;
    task_req.reply_to = reply_queue;

    xQueueSend(modbus_master_get_queue(), &task_req, portMAX_DELAY);

    bool ok = false;
    if (xQueueReceive(reply_queue, out_result, pdMS_TO_TICKS(EXPLORER_REPLY_TIMEOUT_MS)) == pdTRUE) {
        ok = true;
    }
    vQueueDelete(reply_queue);
    return ok;
}

/* ---------------------------------------------------------------------------
 * Periodic WebSocket broadcast — status always, scan/wind while active
 * --------------------------------------------------------------------------- */

static void broadcast_status(void)
{
    wifi_status_t wifi = wifi_manager_get_status();

    char time_buf[32] = "1970-01-01T00:00:00";
    time_t now = time(0);
    if (ntp_is_synced()) {
        struct tm tm_now;
        gmtime_r(&now, &tm_now);
        snprintf(time_buf, sizeof(time_buf), "%04d-%02d-%02dT%02d:%02d:%02d",
                 tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
                 tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
    }

    char json[384];
    web_core_build_status_json(json, sizeof(json),
                                wifi.mode_str, wifi.ssid, wifi.ip, wifi.rssi,
                                ntp_is_synced(), time_buf,
                                (uint32_t)(millis() / 1000), mb_master_get_health());
    s_ws.textAll(json);
}

static void broadcast_scan_if_active(void)
{
    bus_scan_status_t status = bus_scan_get_status();
    if (status.state == SCAN_IDLE) {
        return;
    }
    char json[1600]; /* up to 247 found addresses */
    web_core_build_scan_json(json, sizeof(json), &status);
    s_ws.textAll(json);
}

static void broadcast_wind_if_active(void)
{
    if (!wind_poll_is_active()) {
        return;
    }
    wind_reading_t reading = wind_poll_get_latest();
    char json[256];
    web_core_build_wind_json(json, sizeof(json), &reading, wind_poll_has_data(), wind_poll_age_ms());
    s_ws.textAll(json);
}

static uint32_t s_last_broadcast_log_ts = 0xFFFFFFFFu;

static void broadcast_new_log_entry(void)
{
    mb_log_entry_t entry;
    if (mblog_get_recent(&entry, 1) == 0) {
        return;
    }
    if (entry.timestamp_ms == s_last_broadcast_log_ts) {
        return; /* nothing new since last tick */
    }
    s_last_broadcast_log_ts = entry.timestamp_ms;

    char hex[3 * 256] = {0};
    size_t hex_len = 0;
    for (uint8_t i = 0; i < entry.raw_len && hex_len + 3 < sizeof(hex); i++) {
        hex_len += (size_t)snprintf(hex + hex_len, sizeof(hex) - hex_len, "%02X ", entry.raw[i]);
    }

    char json[512];
    snprintf(json, sizeof(json),
             "{\"type\":\"log\",\"ts\":\"%u\",\"dir\":\"%s\",\"hex\":\"%s\",\"summary\":\"%s\"}",
             (unsigned)entry.timestamp_ms, entry.is_tx ? "TX" : "RX", hex, entry.summary);
    s_ws.textAll(json);
}

static void broadcast_task_fn(void * /*pvParameters*/)
{
    for (;;) {
        broadcast_status();
        broadcast_scan_if_active();
        broadcast_wind_if_active();
        broadcast_new_log_entry();
        vTaskDelay(pdMS_TO_TICKS(STATUS_BROADCAST_INTERVAL_MS));
    }
}

/* ---------------------------------------------------------------------------
 * REST endpoints
 * --------------------------------------------------------------------------- */

static void register_scan_endpoints(void)
{
    s_server.addHandler(new AsyncCallbackJsonWebHandler("/scan/start", [](AsyncWebServerRequest *request, JsonVariant &json) {
        JsonObject o = json.as<JsonObject>();
        uint8_t start = o["start"] | (uint8_t)cfg_get_u8(CFG_KEY_SCAN_RANGE_START, CFG_DEFAULT_SCAN_RANGE_START);
        uint8_t end   = o["end"]   | (uint8_t)cfg_get_u8(CFG_KEY_SCAN_RANGE_END, CFG_DEFAULT_SCAN_RANGE_END);
        cfg_set_u8(CFG_KEY_SCAN_RANGE_START, start);
        cfg_set_u8(CFG_KEY_SCAN_RANGE_END, end);
        scan_task_request_start(start, end);
        send_ok(request, true);
    }));

    s_server.on("/scan/stop", HTTP_POST, [](AsyncWebServerRequest *request) {
        scan_task_request_cancel();
        send_ok(request, true);
    });
}

static void register_wind_endpoints(void)
{
    s_server.addHandler(new AsyncCallbackJsonWebHandler("/wind/start", [](AsyncWebServerRequest *request, JsonVariant &json) {
        JsonObject o = json.as<JsonObject>();
        uint8_t addr        = o["addr"] | (uint8_t)cfg_get_u8(CFG_KEY_WIND_TEST_ADDR, CFG_DEFAULT_WIND_TEST_ADDR);
        uint32_t interval_ms = o["interval_ms"] | cfg_get_u32(CFG_KEY_WIND_POLL_INTERVAL, CFG_DEFAULT_WIND_POLL_INTERVAL);
        cfg_set_u8(CFG_KEY_WIND_TEST_ADDR, addr);
        cfg_set_u32(CFG_KEY_WIND_POLL_INTERVAL, interval_ms);
        wind_poll_set_active(addr, true);
        send_ok(request, true);
    }));

    s_server.on("/wind/stop", HTTP_POST, [](AsyncWebServerRequest *request) {
        wind_poll_set_active(0, false);
        send_ok(request, true);
    });

    s_server.addHandler(new AsyncCallbackJsonWebHandler("/wind/config/read", [](AsyncWebServerRequest *request, JsonVariant &json) {
        JsonObject o = json.as<JsonObject>();
        uint8_t addr = o["addr"] | 31;
        wind_config_t cfg_out;
        bool ok = wind_poll_read_config(addr, &cfg_out);

        char buf[256];
        if (ok) {
            snprintf(buf, sizeof(buf),
                     "{\"ok\":true,\"device_addr\":%u,\"dir_offset_deg\":%.1f,"
                     "\"measurement_window_ms\":%u,\"averaging_window_s\":%u}",
                     cfg_out.device_addr, (double)cfg_out.dir_offset_deg,
                     cfg_out.measurement_window_ms, cfg_out.averaging_window_s);
        } else {
            snprintf(buf, sizeof(buf), "{\"ok\":false}");
        }
        request->send(200, "application/json", buf);
    }));

    s_server.addHandler(new AsyncCallbackJsonWebHandler("/wind/config/write", [](AsyncWebServerRequest *request, JsonVariant &json) {
        JsonObject o = json.as<JsonObject>();
        uint8_t addr = o["addr"] | 31;
        const char *field_name = o["field"] | "";
        float value = o["value"] | 0.0f;

        wind_config_field_t field;
        bool known = true;
        if      (strcmp(field_name, "device_addr") == 0)         field = WIND_CFG_DEVICE_ADDR;
        else if (strcmp(field_name, "dir_offset") == 0)           field = WIND_CFG_DIR_OFFSET;
        else if (strcmp(field_name, "measurement_window") == 0)   field = WIND_CFG_MEASUREMENT_WINDOW;
        else if (strcmp(field_name, "averaging_window") == 0)     field = WIND_CFG_AVERAGING_WINDOW;
        else { known = false; field = WIND_CFG_DEVICE_ADDR; }

        bool ok = known && wind_poll_write_config_field(addr, field, value);
        send_ok(request, ok);
    }));
}

static void register_explorer_endpoint(void)
{
    s_server.addHandler(new AsyncCallbackJsonWebHandler("/explorer/query", [](AsyncWebServerRequest *request, JsonVariant &json) {
        JsonObject o = json.as<JsonObject>();
        uint8_t addr   = o["addr"] | 1;
        uint8_t fc     = o["fc"]   | 3;
        const char *fmt = o["format"] | "raw";
        const char *reg_str = o["register"] | "0";
        uint8_t count  = o["count"] | 1;

        uint16_t raw_register;
        if (strcmp(fmt, "modicon") == 0) {
            raw_register = web_core_modicon_to_raw((uint32_t)strtoul(reg_str, 0, 10));
        } else {
            raw_register = (uint16_t)strtoul(reg_str, 0, 0); /* base 0 auto-detects "0x.." */
        }

        mb_request_t req;
        memset(&req, 0, sizeof(req));
        req.addr  = addr;
        req.fc    = fc;
        req.start = raw_register;
        req.count = count;

        if (fc == 0x06 || fc == 0x10) {
            JsonArray values = o["values"].as<JsonArray>();
            uint8_t i = 0;
            for (JsonVariant v : values) {
                if (i >= 123) break;
                req.values[i++] = (uint16_t)(v.as<long>());
            }
            if (fc == 0x06 && i > 0) {
                /* FC06 encodes the value in mb_write_single_register()'s own
                 * "value" argument slot inside mb_master_process() — reuse
                 * values[0] for that, count is ignored for FC06. */
            }
        }

        mb_result_t result;
        bool got_reply = explorer_transact(&req, &result);

        char buf[512];
        if (!got_reply) {
            snprintf(buf, sizeof(buf), "{\"ok\":false,\"status\":\"no_reply\"}");
        } else if (result.status != MB_OK) {
            const char *status_str;
            switch (result.status) {
                case MB_ERR_TIMEOUT:   status_str = "timeout"; break;
                case MB_ERR_CRC:       status_str = "crc_error"; break;
                case MB_ERR_EXCEPTION: status_str = "exception"; break;
                case MB_ERR_FRAMING:   status_str = "framing_error"; break;
                case MB_ERR_PARAM:     status_str = "param_error"; break;
                default:               status_str = "unknown"; break;
            }
            snprintf(buf, sizeof(buf), "{\"ok\":false,\"status\":\"%s\",\"raw_register\":%u}", status_str, raw_register);
        } else {
            int n = snprintf(buf, sizeof(buf), "{\"ok\":true,\"raw_register\":%u,\"registers\":[", raw_register);
            for (uint8_t i = 0; i < count && i < 125 && (size_t)n < sizeof(buf); i++) {
                n += snprintf(buf + n, sizeof(buf) - (size_t)n, "%s%u", (i == 0) ? "" : ",", result.registers[i]);
            }
            if ((size_t)n < sizeof(buf)) {
                snprintf(buf + n, sizeof(buf) - (size_t)n, "]}");
            }
        }
        request->send(200, "application/json", buf);
    }));
}

static void register_settings_endpoints(void)
{
    s_server.addHandler(new AsyncCallbackJsonWebHandler("/config/wifi", [](AsyncWebServerRequest *request, JsonVariant &json) {
        JsonObject o = json.as<JsonObject>();
        const char *ssid = o["ssid"] | "";
        const char *pass = o["pass"] | "";
        cfg_set_str(CFG_KEY_WIFI_SSID, ssid);
        cfg_set_str(CFG_KEY_WIFI_PASS, pass);
        send_ok(request, true);
        /* Taking effect requires a reboot in this phase — wifi_manager_task
         * only evaluates credentials once, at boot (scratchbook.md TASK-WIFI). */
    }));

    s_server.addHandler(new AsyncCallbackJsonWebHandler("/config/ntp", [](AsyncWebServerRequest *request, JsonVariant &json) {
        JsonObject o = json.as<JsonObject>();
        const char *server_name = o["server"] | CFG_DEFAULT_NTP_SERVER;
        cfg_set_str(CFG_KEY_NTP_SERVER, server_name);
        send_ok(request, true);
    }));

    s_server.addHandler(new AsyncCallbackJsonWebHandler("/config/time", [](AsyncWebServerRequest *request, JsonVariant &json) {
        JsonObject o = json.as<JsonObject>();
        manual_time_t t;
        t.year   = o["year"]   | 2026;
        t.month  = o["month"]  | 1;
        t.day    = o["day"]    | 1;
        t.hour   = o["hour"]   | 0;
        t.minute = o["minute"] | 0;
        t.second = o["second"] | 0;
        send_ok(request, ntp_set_manual_time(&t));
    }));

    s_server.addHandler(new AsyncCallbackJsonWebHandler("/config/modbus", [](AsyncWebServerRequest *request, JsonVariant &json) {
        JsonObject o = json.as<JsonObject>();
        uint16_t timeout_ms = o["timeout_ms"] | CFG_DEFAULT_MB_TIMEOUT_MS;
        uint8_t  retries    = o["retries"]    | CFG_DEFAULT_MB_RETRIES;
        cfg_set_u16(CFG_KEY_MB_TIMEOUT_MS, timeout_ms);
        cfg_set_u8(CFG_KEY_MB_RETRIES, retries);
        send_ok(request, true);
    }));

    s_server.on("/log/clear", HTTP_POST, [](AsyncWebServerRequest *request) {
        mblog_clear();
        s_ws.textAll("{\"type\":\"log_clear\"}");
        send_ok(request, true);
    });
}

/* ---------------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------------- */

void web_server_task_start(void)
{
    if (!SPIFFS.begin(true)) {
        Serial.println("web_server_task: SPIFFS mount failed");
        return;
    }

    s_ws.onEvent([](AsyncWebSocket * /*server*/, AsyncWebSocketClient * /*client*/,
                     AwsEventType /*type*/, void * /*arg*/, uint8_t * /*data*/, size_t /*len*/) {
        /* No per-client state needed — everything is broadcast, not per-connection. */
    });
    s_server.addHandler(&s_ws);

    register_scan_endpoints();
    register_wind_endpoints();
    register_explorer_endpoint();
    register_settings_endpoints();

    s_server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

    s_server.begin();

    xTaskCreatePinnedToCore(broadcast_task_fn, "web_broadcast", 6144, NULL, 2, NULL, APP_CPU_NUM);
}

#endif /* ARDUINO */
