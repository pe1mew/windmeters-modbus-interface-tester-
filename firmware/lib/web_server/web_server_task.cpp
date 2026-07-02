#ifdef ARDUINO
#include "web_server_task.h"
#include <Arduino.h>
#include <SPIFFS.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>

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

/* platformio.ini's [env:windmeterTester] build_flags sets this; fallback
 * so a build that somehow skips it still compiles instead of failing on
 * an undefined macro, and reports something visibly wrong rather than a
 * plausible-looking stale number. */
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "0.0.0-unversioned"
#endif

static AsyncWebServer s_server(80);
static AsyncWebSocket s_ws("/ws");

/* ---------------------------------------------------------------------------
 * Small helpers
 * --------------------------------------------------------------------------- */

static void send_ok(AsyncWebServerRequest *request, bool ok)
{
    request->send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

typedef enum {
    MB_TRANSACT_OK,        /**< Enqueued and a reply arrived within reply_wait_ms. */
    MB_TRANSACT_BUSY,      /**< block_on_enqueue == false and the queue was full — nothing was sent. */
    MB_TRANSACT_NO_REPLY,  /**< Enqueued, but no reply within reply_wait_ms (design/api.md §7's "no_reply" — should not happen in normal operation). */
} mb_transact_outcome_t;

/**
 * @brief Submit a request to modbus_master_task's queue and wait for the reply.
 *
 * Shared core behind both /explorer/query and POST /api/v1/modbus
 * (design/api.md §10 — converge on one core, keep both routes). Callers
 * fill in @p task_req's `request` (and, for the machine API, its
 * override_timing fields) — this function only sets `reply_to`.
 *
 * @param reply_wait_ms     How long to wait for modbus_master_task's reply.
 *                          The two routes need different budgets: a fixed
 *                          generous constant for /explorer/query, one
 *                          derived from the request's own timeout_ms/retries
 *                          for /api/v1/modbus (§4.4) — so this is a
 *                          parameter, not a shared constant.
 * @param block_on_enqueue  true: wait as long as it takes for queue space
 *                          (the only behaviour /explorer/query needs).
 *                          false: fail fast — design/api.md §4.1's
 *                          `"wait":false` — nothing is sent if the queue is
 *                          momentarily full.
 */
static mb_transact_outcome_t mb_queue_transact(mb_task_request_t *task_req, mb_result_t *out_result,
                                                uint32_t reply_wait_ms, bool block_on_enqueue)
{
    QueueHandle_t reply_queue = xQueueCreate(1, sizeof(mb_result_t));
    task_req->reply_to = reply_queue;

    TickType_t enqueue_wait = block_on_enqueue ? portMAX_DELAY : 0;
    if (xQueueSend(modbus_master_get_queue(), task_req, enqueue_wait) != pdTRUE) {
        vQueueDelete(reply_queue);
        return MB_TRANSACT_BUSY;
    }

    mb_transact_outcome_t outcome = MB_TRANSACT_NO_REPLY;
    if (xQueueReceive(reply_queue, out_result, pdMS_TO_TICKS(reply_wait_ms)) == pdTRUE) {
        outcome = MB_TRANSACT_OK;
    }
    vQueueDelete(reply_queue);
    return outcome;
}

static void send_bad_request(AsyncWebServerRequest *request, const char *detail)
{
    char buf[192];
    snprintf(buf, sizeof(buf), "{\"ok\":false,\"status\":\"bad_request\",\"detail\":\"%s\"}", detail);
    request->send(400, "application/json", buf);
}

/** @brief Fill in detail/hint prose for a bus-level POST /api/v1/modbus failure (design/api.md §4.3/§7). Best-effort wording — clients must branch on `status`, never this text. */
static void build_modbus_error_detail_hint(mb_status_t status, uint8_t slave, uint8_t fc, uint16_t register_addr,
                                            uint8_t exception_code, uint8_t attempts, uint16_t timeout_ms,
                                            char *detail, size_t detail_size, char *hint, size_t hint_size)
{
    (void)register_addr;
    switch (status) {
        case MB_ERR_TIMEOUT:
            snprintf(detail, detail_size, "No response from slave %u within %u ms (%u attempts).",
                     slave, timeout_ms, attempts);
            snprintf(hint, hint_size,
                     "Nothing answered at address %u. Run POST /api/v1/scan to list responding "
                     "addresses. Also check bus wiring/termination and that the slave is powered.",
                     slave);
            break;
        case MB_ERR_CRC:
            snprintf(detail, detail_size, "Slave %u replied but the response CRC did not match.", slave);
            snprintf(hint, hint_size,
                     "See raw_rx for the exact bytes received. Possible noise on the bus, a baud "
                     "mismatch, or two devices answering at once.");
            break;
        case MB_ERR_EXCEPTION:
            snprintf(detail, detail_size, "Slave %u answered function %u with exception %u (%s).",
                     slave, fc, exception_code, web_core_exception_name(exception_code));
            snprintf(hint, hint_size,
                     "The slave is alive but rejected this request. Check the register address/count "
                     "against the DUT's own register map documentation.");
            break;
        case MB_ERR_FRAMING:
            snprintf(detail, detail_size, "Slave %u's reply did not match this request.", slave);
            snprintf(hint, hint_size,
                     "Wrong length, function-code echo, or address echo - see raw_rx. Possible bus "
                     "contention or a different device answering.");
            break;
        case MB_ERR_PARAM:
        default:
            snprintf(detail, detail_size, "Request rejected before anything was sent on the bus.");
            snprintf(hint, hint_size,
                     "Check function/count/values against POST /api/v1/modbus's documented ranges.");
            break;
    }
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
                                FIRMWARE_VERSION,
                                wifi.mode_str, wifi.ssid, wifi.ip, wifi.rssi,
                                ntp_is_synced(), time_buf,
                                (uint32_t)(millis() / 1000),
                                cfg_get_u16(CFG_KEY_MB_TIMEOUT_MS, CFG_DEFAULT_MB_TIMEOUT_MS),
                                cfg_get_u8(CFG_KEY_MB_RETRIES, CFG_DEFAULT_MB_RETRIES),
                                mb_master_get_health());
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
    web_core_build_wind_json(json, sizeof(json), wind_poll_get_active_type(),
                              &reading, wind_poll_has_data(), wind_poll_age_ms());
    s_ws.textAll(json);
}

static void broadcast_one_log_entry(const mb_log_entry_t *entry)
{
    char hex[3 * 256] = {0};
    size_t hex_len = 0;
    for (uint8_t i = 0; i < entry->raw_len && hex_len + 3 < sizeof(hex); i++) {
        hex_len += (size_t)snprintf(hex + hex_len, sizeof(hex) - hex_len, "%02X ", entry->raw[i]);
    }

    /* HH:MM:SS for the GUI table — /api/v1/log keeps its own raw-ms
     * "clock":"uptime" contract for machine clients (design/api.md §3);
     * this is the human-display sibling, GUI-side only per that scoping. */
    char ts_buf[12];
    web_core_format_uptime_hhmmss(ts_buf, sizeof(ts_buf), entry->timestamp_ms);

    char json[512];
    snprintf(json, sizeof(json),
             "{\"type\":\"log\",\"ts\":\"%s\",\"dir\":\"%s\",\"hex\":\"%s\",\"summary\":\"%s\"}",
             ts_buf, entry->is_tx ? "TX" : "RX", hex, entry->summary);
    s_ws.textAll(json);
}

static uint32_t s_last_broadcast_total = 0;

static void broadcast_new_log_entries(void)
{
    /* mb_master_process() logs a TX entry immediately followed by an RX
     * entry sharing the same timestamp_ms — looking only at the single
     * newest entry (the old approach) silently skipped the TX whenever
     * both landed inside one broadcast tick, which is the common case for
     * a one-off request (e.g. Register Explorer). Diff the monotonic
     * total against what was last broadcast so every entry gets caught,
     * oldest to newest so the GUI's prepend-to-top ordering comes out
     * right. */
    uint32_t total = mblog_total_appended();
    if (total == s_last_broadcast_total) {
        return; /* nothing new since last tick */
    }
    uint32_t new_count = total - s_last_broadcast_total;
    if (new_count > MB_LOG_CAPACITY) {
        new_count = MB_LOG_CAPACITY; /* rest were overwritten before we could send them */
    }
    s_last_broadcast_total = total;

    /* Static: MB_LOG_CAPACITY entries (~16 KB) would blow this task's
     * 6144-byte stack (xTaskCreatePinnedToCore below) as a local array.
     * Single-owner (only broadcast_task_fn's one task ever calls this). */
    static mb_log_entry_t s_pending[MB_LOG_CAPACITY];
    size_t got = mblog_get_recent(s_pending, new_count); /* newest first */
    for (size_t i = got; i > 0; i--) {
        broadcast_one_log_entry(&s_pending[i - 1]);
    }
}

static void broadcast_task_fn(void * /*pvParameters*/)
{
    for (;;) {
        broadcast_status();
        broadcast_scan_if_active();
        broadcast_wind_if_active();
        broadcast_new_log_entries();
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

/** @brief JSON `"type"` field ("speed"/"direction") -> wind_sensor_type_t. Defaults to WIND_SENSOR_SPEED for a missing/unrecognised value — same fail-open-to-a-defined-default convention as the rest of this file's o["field"] | default parsing. */
static wind_sensor_type_t parse_wind_sensor_type(const char *type_str)
{
    return (type_str && strcmp(type_str, "direction") == 0) ? WIND_SENSOR_DIRECTION : WIND_SENSOR_SPEED;
}

static uint8_t wind_default_addr(wind_sensor_type_t type)
{
    return (type == WIND_SENSOR_DIRECTION)
        ? (uint8_t)cfg_get_u8(CFG_KEY_WIND_DIR_ADDR, CFG_DEFAULT_WIND_DIR_ADDR)
        : (uint8_t)cfg_get_u8(CFG_KEY_WIND_SPEED_ADDR, CFG_DEFAULT_WIND_SPEED_ADDR);
}

static void register_wind_endpoints(void)
{
    s_server.addHandler(new AsyncCallbackJsonWebHandler("/wind/start", [](AsyncWebServerRequest *request, JsonVariant &json) {
        JsonObject o = json.as<JsonObject>();
        wind_sensor_type_t type = parse_wind_sensor_type(o["type"] | "speed");
        uint8_t addr         = o["addr"] | wind_default_addr(type);
        uint32_t interval_ms = o["interval_ms"] | cfg_get_u32(CFG_KEY_WIND_POLL_INTERVAL, CFG_DEFAULT_WIND_POLL_INTERVAL);
        if (type == WIND_SENSOR_DIRECTION) {
            cfg_set_u8(CFG_KEY_WIND_DIR_ADDR, addr);
        } else {
            cfg_set_u8(CFG_KEY_WIND_SPEED_ADDR, addr);
        }
        cfg_set_u32(CFG_KEY_WIND_POLL_INTERVAL, interval_ms);
        wind_poll_set_active(addr, type, true);
        send_ok(request, true);
    }));

    s_server.on("/wind/stop", HTTP_POST, [](AsyncWebServerRequest *request) {
        wind_poll_set_active(0, wind_poll_get_active_type(), false);
        send_ok(request, true);
    });

    s_server.addHandler(new AsyncCallbackJsonWebHandler("/wind/config/read", [](AsyncWebServerRequest *request, JsonVariant &json) {
        JsonObject o = json.as<JsonObject>();
        wind_sensor_type_t type = parse_wind_sensor_type(o["type"] | "speed");
        uint8_t addr = o["addr"] | wind_default_addr(type);
        wind_config_t cfg_out;
        bool ok = wind_poll_read_config(addr, type, &cfg_out);

        char buf[256];
        if (ok && type == WIND_SENSOR_DIRECTION) {
            snprintf(buf, sizeof(buf),
                     "{\"ok\":true,\"sensor_type\":\"direction\",\"device_addr\":%u,"
                     "\"dir_offset_deg\":%.1f,\"averaging_window_s\":%u}",
                     cfg_out.device_addr, (double)cfg_out.dir_offset_deg, cfg_out.averaging_window_s);
        } else if (ok) {
            snprintf(buf, sizeof(buf),
                     "{\"ok\":true,\"sensor_type\":\"speed\",\"device_addr\":%u,"
                     "\"measurement_window_ms\":%u,\"averaging_window_s\":%u}",
                     cfg_out.device_addr, cfg_out.measurement_window_ms, cfg_out.averaging_window_s);
        } else {
            snprintf(buf, sizeof(buf), "{\"ok\":false}");
        }
        request->send(200, "application/json", buf);
    }));

    s_server.addHandler(new AsyncCallbackJsonWebHandler("/wind/config/write", [](AsyncWebServerRequest *request, JsonVariant &json) {
        JsonObject o = json.as<JsonObject>();
        wind_sensor_type_t type = parse_wind_sensor_type(o["type"] | "speed");
        uint8_t addr = o["addr"] | wind_default_addr(type);
        const char *field_name = o["field"] | "";
        float value = o["value"] | 0.0f;

        wind_config_field_t field;
        bool known = true;
        if      (strcmp(field_name, "device_addr") == 0)         field = WIND_CFG_DEVICE_ADDR;
        else if (strcmp(field_name, "dir_offset") == 0)           field = WIND_CFG_DIR_OFFSET;
        else if (strcmp(field_name, "measurement_window") == 0)   field = WIND_CFG_MEASUREMENT_WINDOW;
        else if (strcmp(field_name, "averaging_window") == 0)     field = WIND_CFG_AVERAGING_WINDOW;
        else { known = false; field = WIND_CFG_DEVICE_ADDR; }

        bool ok = known && wind_poll_write_config_field(addr, type, field, value);
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

        mb_task_request_t task_req;
        memset(&task_req, 0, sizeof(task_req));
        task_req.request = req;
        mb_result_t result;
        bool got_reply = mb_queue_transact(&task_req, &result, EXPLORER_REPLY_TIMEOUT_MS, true) == MB_TRANSACT_OK;

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

/** @brief design/api.md's machine-first API — currently just POST /api/v1/modbus (§4); GET /spec/status/log/wind and POST /scan follow. */
static void register_api_v1_endpoints(void)
{
    s_server.addHandler(new AsyncCallbackJsonWebHandler("/api/v1/modbus", [](AsyncWebServerRequest *request, JsonVariant &json) {
        JsonObject o = json.as<JsonObject>();

        uint8_t slave = o["slave"] | 0;
        if (slave < 1 || slave > 247) {
            send_bad_request(request, "slave must be 1-247");
            return;
        }

        uint8_t fc = 0;
        bool fc_valid;
        if (o["function"].is<const char*>()) {
            const char *fn_str = o["function"];
            fc_valid = web_core_resolve_function_alias(fn_str, &fc);
        } else {
            fc = (uint8_t)(o["function"] | (uint32_t)0);
            fc_valid = web_core_is_valid_function_code(fc);
        }
        if (!fc_valid) {
            send_bad_request(request, "function must be 3, 4, 6, 16, or a known alias");
            return;
        }

        uint32_t register_number;
        if (o["register"].is<const char*>()) {
            const char *reg_str = o["register"];
            register_number = strtoul(reg_str, 0, 0); /* base 0 auto-detects "0x.." */
        } else {
            register_number = o["register"] | (uint32_t)0;
        }
        const char *reg_fmt = o["register_format"] | "raw";
        uint16_t register_addr = (strcmp(reg_fmt, "modicon") == 0)
                                      ? web_core_modicon_to_raw(register_number)
                                      : (uint16_t)register_number;

        uint16_t values[123] = {0};
        uint8_t value_count = 0;
        if (o["values"].is<JsonArray>()) {
            for (JsonVariant v : o["values"].as<JsonArray>()) {
                if (value_count >= 123) break;
                values[value_count++] = (uint16_t)(v.as<long>());
            }
        }

        uint8_t count = o["count"] | 1;
        if (fc == 16) {
            count = value_count;
        }

        if ((fc == 3 || fc == 4) && (count < 1 || count > 125)) {
            send_bad_request(request, "count must be 1-125 for read functions");
            return;
        }
        if (fc == 6 && value_count < 1) {
            send_bad_request(request, "values must have exactly one element for write_single");
            return;
        }
        if (fc == 16 && (value_count < 1 || value_count > 123)) {
            send_bad_request(request, "values must have 1-123 elements for write_multiple");
            return;
        }

        uint16_t timeout_ms = o["timeout_ms"] | cfg_get_u16(CFG_KEY_MB_TIMEOUT_MS, CFG_DEFAULT_MB_TIMEOUT_MS);
        uint8_t  retries    = o["retries"]    | cfg_get_u8(CFG_KEY_MB_RETRIES, CFG_DEFAULT_MB_RETRIES);
        if (timeout_ms < 10 || timeout_ms > 5000) {
            send_bad_request(request, "timeout_ms must be 10-5000");
            return;
        }
        if (retries > 5) {
            send_bad_request(request, "retries must be 0-5");
            return;
        }

        bool wait = o["wait"] | true;

        mb_task_request_t task_req;
        memset(&task_req, 0, sizeof(task_req));
        task_req.request.addr  = slave;
        task_req.request.fc    = fc;
        task_req.request.start = register_addr;
        task_req.request.count = count;
        memcpy(task_req.request.values, values, sizeof(values));
        task_req.override_timing     = true;
        task_req.timeout_override_ms = timeout_ms;
        task_req.retries_override    = retries;

        /* §4.4: the reply-wait budget must scale with this request's own
         * timeout/retries, not reuse a fixed constant — worst case here is
         * ~30s (5000ms x 6 attempts), far beyond EXPLORER_REPLY_TIMEOUT_MS. */
        uint32_t reply_wait_ms = (uint32_t)timeout_ms * (retries + 1) + 300;

        uint32_t start_ms = millis();
        mb_result_t result;
        mb_transact_outcome_t outcome = mb_queue_transact(&task_req, &result, reply_wait_ms, wait);
        uint32_t round_trip_ms = millis() - start_ms;

        if (outcome == MB_TRANSACT_BUSY) {
            request->send(409, "application/json",
                "{\"ok\":false,\"status\":\"busy\","
                "\"detail\":\"wait is false and the bus is currently busy; nothing was sent.\"}");
            return;
        }
        if (outcome == MB_TRANSACT_NO_REPLY) {
            char buf[256];
            snprintf(buf, sizeof(buf),
                "{\"ok\":false,\"status\":\"no_reply\",\"slave\":%u,\"function\":%u,\"register\":%u,"
                "\"detail\":\"modbus_master_task did not reply within its own budget.\","
                "\"hint\":\"This should not happen in normal operation - report it as a tester bug.\"}",
                slave, fc, register_addr);
            request->send(200, "application/json", buf);
            return;
        }

        char time_buf[32];
        bool ts_is_uptime;
        if (ntp_is_synced()) {
            time_t now = time(0);
            struct tm tm_now;
            gmtime_r(&now, &tm_now);
            snprintf(time_buf, sizeof(time_buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                     tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
                     tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
            ts_is_uptime = false;
        } else {
            snprintf(time_buf, sizeof(time_buf), "%u", (unsigned)millis());
            ts_is_uptime = true;
        }

        char resp_json[2048];
        if (result.status == MB_OK) {
            bool is_read = (fc == 3 || fc == 4);
            const uint16_t *echo_values = is_read ? result.registers : values;
            uint8_t echo_count = is_read ? count : value_count;
            web_core_build_api_modbus_ok_json(resp_json, sizeof(resp_json),
                slave, fc, register_addr, echo_values, echo_count,
                result.raw_tx, result.raw_tx_len, result.raw_rx, result.raw_rx_len,
                round_trip_ms, result.attempts, time_buf, ts_is_uptime);
        } else {
            char detail[160];
            char hint[256];
            build_modbus_error_detail_hint(result.status, slave, fc, register_addr,
                                            result.exception_code, result.attempts, timeout_ms,
                                            detail, sizeof(detail), hint, sizeof(hint));
            web_core_build_api_modbus_error_json(resp_json, sizeof(resp_json),
                result.status, slave, fc, register_addr, result.exception_code,
                result.raw_tx, result.raw_tx_len, result.raw_rx, result.raw_rx_len,
                result.attempts, detail, hint);
        }
        request->send(200, "application/json", resp_json);
    }));

    /* Hand-curated snapshot of scratchbook.md §5's current register map —
     * not derived from wind_poll.h, since that map is explicitly
     * provisional ("still moving") and this is meant to help a client
     * bootstrap, not be the source of truth. Update by hand if §5 changes.
     *
     * Wind speed and wind direction are separate physical units at
     * separate addresses (wind_poll.h) — this was a single combined
     * 5-input/4-holding map before that split and went stale when the
     * split landed (2026-07-02), since nothing re-generates this constant
     * automatically. Keyed by sensor_type ("speed"/"direction") to match
     * the same tag used throughout the WS/API wind payloads. */
    static const char DUT_REGISTER_SNAPSHOT_JSON[] =
        "{\"wind_speed\":{\"input_registers\":["
        "{\"addr\":0,\"name\":\"speed_instant\",\"unit\":\"0.1 m/s\"},"
        "{\"addr\":1,\"name\":\"speed_avg\",\"unit\":\"0.1 m/s\"},"
        "{\"addr\":2,\"name\":\"raw_pulses\",\"unit\":\"pulses/interval\"}"
        "],\"holding_registers\":["
        "{\"addr\":0,\"name\":\"device_addr\",\"range\":[1,247]},"
        "{\"addr\":1,\"name\":\"measurement_window_ms\"},"
        "{\"addr\":2,\"name\":\"averaging_window_s\"}"
        "]},"
        "\"wind_direction\":{\"input_registers\":["
        "{\"addr\":0,\"name\":\"dir_instant\",\"unit\":\"0.1 deg\"},"
        "{\"addr\":1,\"name\":\"dir_avg\",\"unit\":\"0.1 deg\"}"
        "],\"holding_registers\":["
        "{\"addr\":0,\"name\":\"device_addr\",\"range\":[1,247]},"
        "{\"addr\":1,\"name\":\"dir_offset\",\"unit\":\"0.1 deg\"},"
        "{\"addr\":2,\"name\":\"averaging_window_s\"}"
        "]}}";

    s_server.on("/api/v1/spec", HTTP_GET, [](AsyncWebServerRequest *request) {
        char buf[2048];
        web_core_build_api_spec_json(buf, sizeof(buf), DUT_REGISTER_SNAPSHOT_JSON);
        request->send(200, "application/json", buf);
    });

    s_server.on("/api/v1/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        wifi_status_t wifi = wifi_manager_get_status();
        char buf[512];
        web_core_build_api_status_json(buf, sizeof(buf),
            FIRMWARE_VERSION,
            (uint32_t)(millis() / 1000),
            wifi.mode_str, wifi.ssid, wifi.ip, wifi.rssi,
            ntp_is_synced(),
            cfg_get_u32(CFG_KEY_MB_BAUD, CFG_DEFAULT_MB_BAUD),
            cfg_get_u16(CFG_KEY_MB_TIMEOUT_MS, CFG_DEFAULT_MB_TIMEOUT_MS),
            cfg_get_u8(CFG_KEY_MB_RETRIES, CFG_DEFAULT_MB_RETRIES),
            mb_master_get_health(),
            bus_scan_get_status().state == SCAN_RUNNING,
            wind_poll_is_active());
        request->send(200, "application/json", buf);
    });

    s_server.on("/api/v1/wind", HTTP_GET, [](AsyncWebServerRequest *request) {
        wind_sensor_type_t type = WIND_SENSOR_SPEED;
        if (request->hasParam("type") && request->getParam("type")->value() == "direction") {
            type = WIND_SENSOR_DIRECTION;
        }
        bool this_type_active = wind_poll_is_active() && wind_poll_get_active_type() == type;
        wind_reading_t reading = wind_poll_get_latest();
        char buf[512];
        web_core_build_api_wind_json(buf, sizeof(buf), wind_default_addr(type), type,
            this_type_active, &reading, wind_poll_has_data(), wind_poll_age_ms());
        request->send(200, "application/json", buf);
    });

    s_server.on("/api/v1/log", HTTP_GET, [](AsyncWebServerRequest *request) {
        size_t n = 20;
        if (request->hasParam("n")) {
            long parsed = request->getParam("n")->value().toInt();
            if (parsed > 0) {
                n = (size_t)parsed;
            }
        }
        if (n > MB_LOG_CAPACITY) {
            n = MB_LOG_CAPACITY; /* ring capacity, design/api.md §5.4 */
        }

        /* Heap, not stack — MB_LOG_CAPACITY entries x sizeof(mb_log_entry_t)
         * (~16KB at 50) plus a worst-case-sized output buffer (~46KB, 50 x
         * up to-256-byte raw frames as hex) is too large to risk on whatever
         * stack size AsyncWebServer's request-handling task happens to have. */
        mb_log_entry_t *entries = (mb_log_entry_t *)malloc(sizeof(mb_log_entry_t) * MB_LOG_CAPACITY);
        if (!entries) {
            request->send(500, "application/json", "{\"ok\":false,\"detail\":\"out of memory\"}");
            return;
        }
        size_t got = mblog_get_recent(entries, n);

        const size_t buf_size = 65536; /* generous worst-case margin, see above */
        char *buf = (char *)malloc(buf_size);
        if (!buf) {
            free(entries);
            request->send(500, "application/json", "{\"ok\":false,\"detail\":\"out of memory\"}");
            return;
        }
        web_core_build_api_log_json(buf, buf_size, entries, got);
        request->send(200, "application/json", buf);
        free(entries);
        free(buf);
    });

    /* AsyncCallbackJsonWebHandler defaults to matching GET|POST|PUT|PATCH on
     * its URI — since GET /api/v1/scan is a separate, differently-behaved
     * route registered below on the same path, this one must be pinned to
     * POST only, or it would shadow the GET handler (or worse, a bare GET
     * with no body would fall through to this handler's defaults and
     * accidentally start a new scan). */
    AsyncCallbackJsonWebHandler *scan_post_handler = new AsyncCallbackJsonWebHandler("/api/v1/scan",
            [](AsyncWebServerRequest *request, JsonVariant &json) {
        JsonObject o = json.as<JsonObject>();
        uint8_t start = o["start"] | (uint8_t)cfg_get_u8(CFG_KEY_SCAN_RANGE_START, CFG_DEFAULT_SCAN_RANGE_START);
        uint8_t end   = o["end"]   | (uint8_t)cfg_get_u8(CFG_KEY_SCAN_RANGE_END, CFG_DEFAULT_SCAN_RANGE_END);
        bool    wait  = o["wait"] | true;

        if (bus_scan_get_status().state == SCAN_RUNNING) {
            request->send(409, "application/json",
                "{\"ok\":false,\"status\":\"busy\",\"detail\":\"a scan is already running\"}");
            return;
        }

        cfg_set_u8(CFG_KEY_SCAN_RANGE_START, start);
        cfg_set_u8(CFG_KEY_SCAN_RANGE_END, end);
        scan_task_request_start(start, end);

        if (!wait) {
            request->send(202, "application/json", "{\"ok\":true,\"state\":\"scanning\"}");
            return;
        }

        /* Yielding poll loop (design/api.md §5.3/§4.4's implementation
         * note) — not a single monolithic block, so a full ~100s sweep
         * doesn't starve other AsyncWebServer requests or the WebSocket
         * broadcast task for its whole duration. Safety-net ceiling well
         * above the documented full-sweep estimate, scaled off scan_task's
         * actual fixed 2000ms-per-address probe timeout (not mb_timeout_ms
         * — scan doesn't use the per-request override mechanism), in case
         * the sweep never completes for some other reason. */
        uint32_t range_len   = (uint32_t)end - (uint32_t)start + 1;
        uint32_t max_wait_ms = range_len * 2200u + 5000u;
        uint32_t waited_ms   = 0;
        /* scan_task_request_start() only enqueues the command — scan_task
         * hasn't necessarily dequeued it and called bus_scan_start() yet,
         * so bus_scan_get_status() can still be reporting a PRIOR scan's
         * state (including a stale SCAN_COMPLETE/SCAN_CANCELLED from the
         * *previous* sweep, not "nothing to wait for"). The only reliable
         * signal that THIS request's scan has actually finished is its own
         * range showing up on a COMPLETE/CANCELLED status — range_start/
         * range_end only take on these values once bus_scan_start() has
         * actually run for this request. */
        for (;;) {
            bus_scan_status_t s = bus_scan_get_status();
            bool this_scan_done = s.range_start == start && s.range_end == end
                                   && (s.state == SCAN_COMPLETE || s.state == SCAN_CANCELLED);
            if (this_scan_done || waited_ms >= max_wait_ms) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(200));
            waited_ms += 200;
        }

        const size_t buf_size = 16384; /* heap, not stack — up to 247 found devices worst case */
        char *buf = (char *)malloc(buf_size);
        if (!buf) {
            request->send(500, "application/json", "{\"ok\":false,\"detail\":\"out of memory\"}");
            return;
        }
        bus_scan_status_t status = bus_scan_get_status();
        web_core_build_api_scan_json(buf, buf_size, &status);
        request->send(200, "application/json", buf);
        free(buf);
    });
    scan_post_handler->setMethod(HTTP_POST);
    s_server.addHandler(scan_post_handler);

    s_server.on("/api/v1/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
        const size_t buf_size = 16384;
        char *buf = (char *)malloc(buf_size);
        if (!buf) {
            request->send(500, "application/json", "{\"ok\":false,\"detail\":\"out of memory\"}");
            return;
        }
        bus_scan_status_t status = bus_scan_get_status();
        web_core_build_api_scan_json(buf, buf_size, &status);
        request->send(200, "application/json", buf);
        free(buf);
    });
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
        /* wifi_manager_task only evaluates credentials once, at boot
         * (scratchbook.md TASK-WIFI) — reboot here rather than leaving the
         * new credentials sitting unused in NVS until something else
         * happens to restart the device. Found via INT-02
         * (design/whatsNext.md): without this, the documented "submit
         * credentials -> device reconnects" flow just doesn't happen, the
         * AP stays up and 192.168.4.1 stays the only way to reach it. Small
         * delay first so the "ok" response actually reaches the client
         * before the connection drops. */
        delay(500);
        ESP.restart();
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
    register_api_v1_endpoints();
    register_settings_endpoints();

    s_server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

    s_server.begin();

    xTaskCreatePinnedToCore(broadcast_task_fn, "web_broadcast", 6144, NULL, 2, NULL, APP_CPU_NUM);
}

#endif /* ARDUINO */
