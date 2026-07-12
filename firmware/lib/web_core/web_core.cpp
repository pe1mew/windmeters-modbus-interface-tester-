/**
 * @file web_core.cpp
 * @brief Web server — the testable core, implementation (see web_core.h).
 *
 * Pure functions only (JSON building, table lookups, one time-format
 * conversion) — no I/O, no ESPAsyncWebServer/WiFi calls, fully exercised by
 * the test_web_core native test suite (`pio test -e native -f test_web_core`).
 * All snprintf() calls here follow the same truncation contract: writing
 * stops cleanly at @p out_size and the return value is still the number of
 * bytes that *would* have been written (snprintf's own convention), so a
 * caller can detect truncation by comparing the return value against the
 * buffer size it passed in.
 */
#include "web_core.h"
#include "ntp_manager.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

/**
 * @brief Portable UTC time breakdown: gmtime_r on target, gmtime_s on the
 *        native/MinGW test build.
 *
 * Same UTC-breakdown either way, but the native test build (MinGW on
 * Windows) doesn't expose POSIX gmtime_r — only Microsoft's gmtime_s
 * (reversed argument order, tm* first). The ESP32/Arduino target has no
 * _WIN32 define and takes the gmtime_r branch, matching web_server_task.cpp's
 * own use of it.
 * @param epoch Unix epoch seconds to break down.
 * @param out   Destination for the broken-down UTC time.
 */
static void gmtime_portable(time_t epoch, struct tm *out)
{
#if defined(_WIN32) || defined(_WIN64)
    gmtime_s(out, &epoch);
#else
    gmtime_r(&epoch, out);
#endif
}

uint16_t web_core_modicon_to_raw(uint32_t modicon_number)
{
    return (uint16_t)((modicon_number % 10000u) - 1u);
}

/**
 * @brief scan_state_t -> the WebSocket type:"scan" payload's state string ("idle"/"running"/"cancelled"/"complete").
 * @param state State to name.
 * @return Static string literal; never NULL.
 */
static const char *scan_state_name(scan_state_t state)
{
    switch (state) {
        case SCAN_IDLE:      return "idle";
        case SCAN_RUNNING:   return "running";
        case SCAN_CANCELLED: return "cancelled";
        case SCAN_COMPLETE:  return "complete";
        default:             return "idle";
    }
}

int web_core_build_scan_json(char *out, size_t out_size, const bus_scan_status_t *status)
{
    int n = snprintf(out, out_size,
        "{\"type\":\"scan\",\"state\":\"%s\",\"current_addr\":%u,\"range_end\":%u,\"found\":[",
        scan_state_name(status->state), status->current_addr, status->range_end);
    if (n < 0) {
        return n;
    }

    /* Same per-device shape as web_core_build_api_scan_json()'s found[]
     * (design/api.md §5.3) — the GUI and the machine API read the same
     * bus_scan_status_t, so they show the same information, not just the
     * same addresses. */
    for (uint8_t i = 0; i < status->found_count; i++) {
        if ((size_t)n >= out_size) {
            break;
        }
        int added = snprintf(out + n, out_size - (size_t)n,
            "%s{\"slave\":%u,\"functions_ok\":[%u],\"round_trip_ms\":%u}",
            (i == 0) ? "" : ",", status->found[i], status->found_fc[i],
            (unsigned)status->found_round_trip_ms[i]);
        if (added < 0) {
            return added;
        }
        n += added;
    }

    if ((size_t)n < out_size) {
        int added = snprintf(out + n, out_size - (size_t)n, "]}");
        if (added > 0) {
            n += added;
        }
    }
    return n;
}

/**
 * @brief wind_sensor_type_t -> its JSON "sensor_type" string ("speed"/"direction"/"combined").
 * @param type Sensor type to name.
 * @return Static string literal; never NULL.
 */
static const char *wind_sensor_type_name(wind_sensor_type_t type)
{
    if (type == WIND_SENSOR_SPEED)     return "speed";
    if (type == WIND_SENSOR_DIRECTION) return "direction";
    return "combined";
}

int web_core_build_wind_json(char *out, size_t out_size, wind_sensor_type_t type,
                              const wind_reading_t *reading, bool has_data, uint32_t age_ms)
{
    if (!has_data) {
        return snprintf(out, out_size, "{\"type\":\"wind\",\"sensor_type\":\"%s\",\"has_data\":false}",
                         wind_sensor_type_name(type));
    }
    if (type == WIND_SENSOR_SPEED) {
        return snprintf(out, out_size,
            "{\"type\":\"wind\",\"sensor_type\":\"speed\",\"has_data\":true,"
            "\"speed_instant_ms\":%.1f,\"speed_avg_ms\":%.1f,"
            "\"raw_pulses\":%u,\"gust_ms\":%.1f,\"seconds_since_pulse\":%u,"
            "\"age_ms\":%u}",
            (double)reading->speed_instant_ms, (double)reading->speed_avg_ms,
            (unsigned)reading->raw_diagnostic, (double)reading->gust_ms,
            (unsigned)reading->seconds_since_pulse, (unsigned)age_ms);
    }
    if (type == WIND_SENSOR_DIRECTION) {
        return snprintf(out, out_size,
            "{\"type\":\"wind\",\"sensor_type\":\"direction\",\"has_data\":true,"
            "\"dir_instant_deg\":%.1f,\"dir_avg_deg\":%.1f,\"dir_fault\":%s,"
            "\"raw_adc\":%u,\"age_ms\":%u}",
            (double)reading->dir_instant_deg, (double)reading->dir_avg_deg,
            reading->dir_fault ? "true" : "false",
            (unsigned)reading->raw_diagnostic, (unsigned)age_ms);
    }
    /* Combined: union of the speed and direction shapes above, same key
     * names — "raw_pulses"/"gust_ms"/"seconds_since_pulse" mean the same
     * thing they do in the speed message; "raw_adc" the same thing it
     * does in the direction message (it just comes off a different wire
     * register, 30013 instead of 30005, on this build — TDS §2.7). */
    return snprintf(out, out_size,
        "{\"type\":\"wind\",\"sensor_type\":\"combined\",\"has_data\":true,"
        "\"speed_instant_ms\":%.1f,\"speed_avg_ms\":%.1f,"
        "\"raw_pulses\":%u,\"gust_ms\":%.1f,\"seconds_since_pulse\":%u,"
        "\"dir_instant_deg\":%.1f,\"dir_avg_deg\":%.1f,\"dir_fault\":%s,"
        "\"raw_adc\":%u,\"age_ms\":%u}",
        (double)reading->speed_instant_ms, (double)reading->speed_avg_ms,
        (unsigned)reading->raw_diagnostic, (double)reading->gust_ms,
        (unsigned)reading->seconds_since_pulse,
        (double)reading->dir_instant_deg, (double)reading->dir_avg_deg,
        reading->dir_fault ? "true" : "false",
        (unsigned)reading->dir_raw_adc, (unsigned)age_ms);
}

int web_core_build_interface_json(char *out, size_t out_size, uint8_t addr,
                                   const wind_interface_status_t *st,
                                   bool has_data, uint32_t age_ms)
{
    if (!has_data) {
        return snprintf(out, out_size, "{\"type\":\"interface\",\"addr\":%u,\"has_data\":false}", addr);
    }
    return snprintf(out, out_size,
        "{\"type\":\"interface\",\"addr\":%u,"
        "\"build_type\":%u,\"build_name\":\"%s\","
        "\"fw_version\":%u,"
        "\"status_flags\":%u,"
        "\"status_measurement_incomplete\":%s,\"status_avg_not_filled\":%s,\"status_dir_fault\":%s,"
        "\"uptime_s\":%u,\"crc_error_count\":%u,\"served_request_count\":%u,"
        "\"has_data\":true,\"age_ms\":%u}",
        addr, st->build_type, wind_build_type_name(st->build_type), st->fw_version,
        (unsigned)st->status_flags,
        st->status_measurement_incomplete ? "true" : "false",
        st->status_avg_not_filled ? "true" : "false",
        st->status_dir_fault ? "true" : "false",
        (unsigned)st->uptime_s, (unsigned)st->crc_error_count, (unsigned)st->served_request_count,
        (unsigned)age_ms);
}

int web_core_build_status_json(char *out, size_t out_size,
                                const char *fw_version,
                                const char *wifi_mode, const char *wifi_ssid,
                                const char *wifi_ip, int8_t wifi_rssi,
                                bool ntp_synced, const char *local_time_iso,
                                uint32_t uptime_s,
                                uint16_t mb_timeout_ms, uint8_t mb_retries,
                                const mb_bus_health_t *bus_health)
{
    char exc_buf[8];
    if (bus_health->has_exception) {
        snprintf(exc_buf, sizeof(exc_buf), "%u", bus_health->last_exception);
    } else {
        snprintf(exc_buf, sizeof(exc_buf), "null");
    }

    return snprintf(out, out_size,
        "{\"type\":\"status\",\"fw_version\":\"%s\",\"wifi_mode\":\"%s\",\"wifi_ssid\":\"%s\",\"wifi_ip\":\"%s\","
        "\"wifi_rssi\":%d,\"ntp_synced\":%s,\"local_time\":\"%s\",\"uptime_s\":%u,"
        "\"mb_timeout_ms\":%u,\"mb_retries\":%u,"
        "\"bus\":{\"crc_errors\":%u,\"timeouts\":%u,\"last_exception\":%s}}",
        fw_version, wifi_mode, wifi_ssid, wifi_ip, (int)wifi_rssi, ntp_synced ? "true" : "false",
        local_time_iso, (unsigned)uptime_s,
        (unsigned)mb_timeout_ms, (unsigned)mb_retries,
        (unsigned)bus_health->crc_errors, (unsigned)bus_health->timeouts, exc_buf);
}

/* ---------------------------------------------------------------------------
 * Machine API (design/api.md)
 * ------------------------------------------------------------------------- */

/**
 * @brief Format @p count bytes as uppercase space-separated hex (design/api.md §3's raw_tx/raw_rx shape).
 *
 * Stops appending once @p out_size is reached rather than overrunning it —
 * on truncation the returned count no longer reflects "bytes that would
 * have been written" the way a single snprintf() call's return does,
 * since the loop simply breaks instead of continuing to tally; callers in
 * this file only ever pass buffers sized generously enough (raw frames
 * are MB_MAX_FRAME_LEN, buffers are 800 bytes) that this path isn't hit
 * in practice.
 *
 * @param out       Destination buffer for the hex text (no null terminator
 *                   added by this function — callers snprintf() this
 *                   result into a larger `"%s"` slot, which terminates it).
 * @param out_size  Capacity of @p out.
 * @param bytes     Bytes to format, @p count entries; NULL/0 yields an
 *                   empty (zero-length) result.
 * @param count     Number of entries in @p bytes.
 * @return Number of characters written into @p out.
 */
static int format_hex(char *out, size_t out_size, const uint8_t *bytes, uint16_t count)
{
    int n = 0;
    for (uint16_t i = 0; i < count; i++) {
        if ((size_t)n >= out_size) {
            break;
        }
        int added = snprintf(out + n, out_size - (size_t)n, "%s%02X", (i == 0) ? "" : " ", bytes[i]);
        if (added < 0) {
            return added;
        }
        n += added;
    }
    return n;
}

/**
 * @brief Format @p count register values as a comma-separated JSON array body (no brackets — caller wraps them).
 *
 * Same truncate-cleanly-at-@p out_size behaviour and caveat as
 * format_hex(): breaks the loop rather than overrunning, so the return
 * value stops reflecting "bytes that would have been written" once
 * truncation actually happens. In practice the 800-byte values_buf in
 * web_core_build_api_modbus_ok_json() is sized well past what 125
 * registers (`"%u"` values, comma-separated) can produce.
 *
 * @param out       Destination buffer for the array body text (no null
 *                   terminator added — see format_hex()'s same note).
 * @param out_size  Capacity of @p out.
 * @param values    Register values to format, @p count entries.
 * @param count     Number of entries in @p values.
 * @return Number of characters written into @p out.
 */
static int format_u16_array(char *out, size_t out_size, const uint16_t *values, uint8_t count)
{
    int n = 0;
    for (uint8_t i = 0; i < count; i++) {
        if ((size_t)n >= out_size) {
            break;
        }
        int added = snprintf(out + n, out_size - (size_t)n, "%s%u", (i == 0) ? "" : ",", values[i]);
        if (added < 0) {
            return added;
        }
        n += added;
    }
    return n;
}

bool web_core_is_valid_function_code(uint8_t fc)
{
    return fc == 3 || fc == 4 || fc == 6 || fc == 16;
}

bool web_core_resolve_function_alias(const char *alias, uint8_t *out_fc)
{
    if (!alias) {
        return false;
    }
    if (strcmp(alias, "read_holding") == 0)   { *out_fc = 3;  return true; }
    if (strcmp(alias, "read_input") == 0)     { *out_fc = 4;  return true; }
    if (strcmp(alias, "write_single") == 0)   { *out_fc = 6;  return true; }
    if (strcmp(alias, "write_multiple") == 0) { *out_fc = 16; return true; }
    return false;
}

const char *web_core_api_status_name(mb_status_t status)
{
    switch (status) {
        case MB_OK:            return "ok";
        case MB_ERR_TIMEOUT:   return "timeout";
        case MB_ERR_CRC:       return "crc_error";
        case MB_ERR_EXCEPTION: return "exception";
        case MB_ERR_FRAMING:   return "framing_error";
        case MB_ERR_PARAM:     return "param_error";
        default:               return "param_error";
    }
}

const char *web_core_exception_name(uint8_t exception_code)
{
    switch (exception_code) {
        case 1:  return "illegal_function";
        case 2:  return "illegal_data_address";
        case 3:  return "illegal_data_value";
        case 4:  return "slave_device_failure";
        case 5:  return "acknowledge";
        case 6:  return "slave_device_busy";
        case 8:  return "memory_parity_error";
        case 10: return "gateway_path_unavailable";
        case 11: return "gateway_target_device_failed_to_respond";
        default: return "unknown";
    }
}

int web_core_build_api_modbus_ok_json(char *out, size_t out_size,
                                       uint8_t slave, uint8_t fc, uint16_t register_addr,
                                       const uint16_t *values, uint8_t value_count,
                                       const uint8_t *raw_tx, uint16_t raw_tx_len,
                                       const uint8_t *raw_rx, uint16_t raw_rx_len,
                                       uint32_t round_trip_ms, uint8_t attempts,
                                       const char *ts, bool ts_is_uptime)
{
    bool is_read = (fc == 3 || fc == 4);

    char values_buf[800];
    int vn;
    if (is_read) {
        vn = snprintf(values_buf, sizeof(values_buf), "\"count\":%u,\"registers\":[", value_count);
    } else {
        vn = snprintf(values_buf, sizeof(values_buf), "\"written\":[");
    }
    if (vn > 0 && (size_t)vn < sizeof(values_buf)) {
        vn += format_u16_array(values_buf + vn, sizeof(values_buf) - (size_t)vn, values, value_count);
    }
    if (vn > 0 && (size_t)vn < sizeof(values_buf)) {
        snprintf(values_buf + vn, sizeof(values_buf) - (size_t)vn, "]");
    }

    char tx_buf[800];
    format_hex(tx_buf, sizeof(tx_buf), raw_tx, raw_tx_len);
    char rx_buf[800];
    format_hex(rx_buf, sizeof(rx_buf), raw_rx, raw_rx_len);

    return snprintf(out, out_size,
        "{\"ok\":true,\"status\":\"ok\",\"slave\":%u,\"function\":%u,\"register\":%u,%s,"
        "\"raw_tx\":\"%s\",\"raw_rx\":\"%s\",\"round_trip_ms\":%u,\"attempts\":%u,\"ts\":\"%s\"%s}",
        slave, fc, register_addr, values_buf,
        tx_buf, rx_buf, (unsigned)round_trip_ms, attempts, ts,
        ts_is_uptime ? ",\"clock\":\"uptime\"" : "");
}

int web_core_build_api_modbus_error_json(char *out, size_t out_size,
                                          mb_status_t status,
                                          uint8_t slave, uint8_t fc, uint16_t register_addr,
                                          uint8_t exception_code,
                                          const uint8_t *raw_tx, uint16_t raw_tx_len,
                                          const uint8_t *raw_rx, uint16_t raw_rx_len,
                                          uint8_t attempts,
                                          const char *detail, const char *hint)
{
    char exc_buf[64];
    if (status == MB_ERR_EXCEPTION) {
        snprintf(exc_buf, sizeof(exc_buf), "\"exception_code\":%u,\"exception_name\":\"%s\",",
                 exception_code, web_core_exception_name(exception_code));
    } else {
        exc_buf[0] = '\0';
    }

    char tx_buf[800];
    format_hex(tx_buf, sizeof(tx_buf), raw_tx, raw_tx_len);

    char rx_hex[800];
    char rx_buf[820];
    if (raw_rx_len > 0) {
        format_hex(rx_hex, sizeof(rx_hex), raw_rx, raw_rx_len);
        snprintf(rx_buf, sizeof(rx_buf), "\"raw_rx\":\"%s\",", rx_hex);
    } else {
        rx_buf[0] = '\0';
    }

    return snprintf(out, out_size,
        "{\"ok\":false,\"status\":\"%s\",%s\"slave\":%u,\"function\":%u,\"register\":%u,"
        "\"raw_tx\":\"%s\",%s\"attempts\":%u,\"detail\":\"%s\",\"hint\":\"%s\"}",
        web_core_api_status_name(status), exc_buf, slave, fc, register_addr,
        tx_buf, rx_buf, attempts, detail, hint);
}

int web_core_build_api_status_json(char *out, size_t out_size,
                                    const char *fw_version,
                                    uint32_t uptime_s,
                                    const char *wifi_mode, const char *wifi_ssid,
                                    const char *wifi_ip, int8_t wifi_rssi,
                                    bool ntp_synced,
                                    uint32_t mb_baud, uint16_t mb_timeout_ms, uint8_t mb_retries,
                                    const mb_bus_health_t *bus_health,
                                    bool scan_running, bool wind_poll_active)
{
    char exc_buf[8];
    if (bus_health->has_exception) {
        snprintf(exc_buf, sizeof(exc_buf), "%u", bus_health->last_exception);
    } else {
        snprintf(exc_buf, sizeof(exc_buf), "null");
    }

    return snprintf(out, out_size,
        "{\"ok\":true,\"fw_version\":\"%s\",\"uptime_s\":%u,"
        "\"wifi\":{\"mode\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\",\"rssi\":%d},"
        "\"ntp_synced\":%s,"
        "\"modbus\":{\"baud\":%u,\"timeout_ms\":%u,\"retries\":%u,\"crc_errors\":%u,\"timeouts\":%u,\"last_exception\":%s},"
        "\"busy\":{\"scan_running\":%s,\"wind_poll_active\":%s}}",
        fw_version, (unsigned)uptime_s, wifi_mode, wifi_ssid, wifi_ip, (int)wifi_rssi,
        ntp_synced ? "true" : "false",
        (unsigned)mb_baud, (unsigned)mb_timeout_ms, (unsigned)mb_retries,
        (unsigned)bus_health->crc_errors, (unsigned)bus_health->timeouts, exc_buf,
        scan_running ? "true" : "false", wind_poll_active ? "true" : "false");
}

int web_core_build_api_wind_json(char *out, size_t out_size, uint8_t target, wind_sensor_type_t type,
                                  bool wind_active, const wind_reading_t *reading, bool has_data, uint32_t age_ms)
{
    if (!wind_active || !has_data) {
        return snprintf(out, out_size, "{\"ok\":true,\"has_data\":false}");
    }
    if (type == WIND_SENSOR_SPEED) {
        return snprintf(out, out_size,
            "{\"ok\":true,\"has_data\":true,\"target\":%u,\"sensor_type\":\"speed\","
            "\"speed_instant_ms\":%.1f,\"speed_avg_ms\":%.1f,"
            "\"raw_pulses\":%u,\"gust_ms\":%.1f,\"seconds_since_pulse\":%u,"
            "\"age_ms\":%u}",
            target, (double)reading->speed_instant_ms, (double)reading->speed_avg_ms,
            (unsigned)reading->raw_diagnostic, (double)reading->gust_ms,
            (unsigned)reading->seconds_since_pulse, (unsigned)age_ms);
    }
    if (type == WIND_SENSOR_DIRECTION) {
        return snprintf(out, out_size,
            "{\"ok\":true,\"has_data\":true,\"target\":%u,\"sensor_type\":\"direction\","
            "\"dir_instant_deg\":%.1f,\"dir_avg_deg\":%.1f,\"dir_fault\":%s,"
            "\"raw_adc\":%u,\"age_ms\":%u}",
            target, (double)reading->dir_instant_deg, (double)reading->dir_avg_deg,
            reading->dir_fault ? "true" : "false",
            (unsigned)reading->raw_diagnostic, (unsigned)age_ms);
    }
    /* Combined: same union-of-fields convention as web_core_build_wind_json(). */
    return snprintf(out, out_size,
        "{\"ok\":true,\"has_data\":true,\"target\":%u,\"sensor_type\":\"combined\","
        "\"speed_instant_ms\":%.1f,\"speed_avg_ms\":%.1f,"
        "\"raw_pulses\":%u,\"gust_ms\":%.1f,\"seconds_since_pulse\":%u,"
        "\"dir_instant_deg\":%.1f,\"dir_avg_deg\":%.1f,\"dir_fault\":%s,"
        "\"raw_adc\":%u,\"age_ms\":%u}",
        target, (double)reading->speed_instant_ms, (double)reading->speed_avg_ms,
        (unsigned)reading->raw_diagnostic, (double)reading->gust_ms,
        (unsigned)reading->seconds_since_pulse,
        (double)reading->dir_instant_deg, (double)reading->dir_avg_deg,
        reading->dir_fault ? "true" : "false",
        (unsigned)reading->dir_raw_adc, (unsigned)age_ms);
}

int web_core_format_uptime_hhmmss(char *out, size_t out_size, uint32_t uptime_ms)
{
    uint32_t total_s = uptime_ms / 1000u;
    uint32_t h = total_s / 3600u;
    uint32_t m = (total_s % 3600u) / 60u;
    uint32_t s = total_s % 60u;
    return snprintf(out, out_size, "%02u:%02u:%02u", (unsigned)h, (unsigned)m, (unsigned)s);
}

int web_core_format_log_entry_timestamp(char *out, size_t out_size, uint32_t timestamp_ms)
{
    if (!ntp_manager_is_synced()) {
        return web_core_format_uptime_hhmmss(out, out_size, timestamp_ms);
    }
    /* Same per-entry millis-at-log-time -> epoch conversion as
     * web_core_build_api_log_json() (design/api.md §3) — this entry's own
     * stored timestamp, not "now". */
    time_t epoch = (time_t)ntp_manager_millis_to_epoch(timestamp_ms);
    struct tm tm_val;
    gmtime_portable(epoch, &tm_val);
    return snprintf(out, out_size, "%04d-%02d-%02dT%02d:%02d:%02dZ",
                     tm_val.tm_year + 1900, tm_val.tm_mon + 1, tm_val.tm_mday,
                     tm_val.tm_hour, tm_val.tm_min, tm_val.tm_sec);
}

int web_core_build_api_log_json(char *out, size_t out_size, const mb_log_entry_t *entries, size_t count)
{
    int n = snprintf(out, out_size, "{\"ok\":true,\"entries\":[");
    if (n < 0) {
        return n;
    }

    /* entries[] is newest-first (mblog_get_recent's convention) — walk it
     * backwards to emit oldest-first ("newest last", design/api.md §5.4). */
    bool synced = ntp_manager_is_synced();
    for (size_t i = 0; i < count; i++) {
        if ((size_t)n >= out_size) {
            break;
        }
        const mb_log_entry_t *e = &entries[count - 1 - i];
        char hex[800];
        format_hex(hex, sizeof(hex), e->raw, e->raw_len);

        /* Same "ISO-8601 UTC once synced, else uptime ms + clock:uptime"
         * contract as POST /api/v1/modbus's ts field (design/api.md §3) —
         * each entry converts its own stored timestamp_ms (millis-at-log-time)
         * via the recorded sync reference point, not "now", since these are
         * historical entries. */
        char ts_buf[32];
        const char *clock_field;
        if (synced) {
            time_t epoch = (time_t)ntp_manager_millis_to_epoch(e->timestamp_ms);
            struct tm tm_val;
            gmtime_portable(epoch, &tm_val);
            snprintf(ts_buf, sizeof(ts_buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                     tm_val.tm_year + 1900, tm_val.tm_mon + 1, tm_val.tm_mday,
                     tm_val.tm_hour, tm_val.tm_min, tm_val.tm_sec);
            clock_field = "";
        } else {
            snprintf(ts_buf, sizeof(ts_buf), "%u", (unsigned)e->timestamp_ms);
            clock_field = ",\"clock\":\"uptime\"";
        }

        int added = snprintf(out + n, out_size - (size_t)n,
            "%s{\"ts\":\"%s\"%s,\"dir\":\"%s\",\"hex\":\"%s\",\"summary\":\"%s\"}",
            (i == 0) ? "" : ",", ts_buf, clock_field, e->is_tx ? "TX" : "RX", hex, e->summary);
        if (added < 0) {
            return added;
        }
        n += added;
    }

    if ((size_t)n < out_size) {
        int added = snprintf(out + n, out_size - (size_t)n, "]}");
        if (added > 0) {
            n += added;
        }
    }
    return n;
}

/**
 * @brief scan_state_t -> design/api.md §5.3's own state vocabulary
 *        ("idle"/"scanning"/"cancelled"/"done").
 *
 * Deliberately not scan_state_name() above: api.md documents "scanning"/
 * "done" for the machine API while the WebSocket type:"scan" payload uses
 * "running"/"complete" — two separate string sets for the same enum, each
 * matching the wire contract its own consumer was promised.
 *
 * @param state State to name.
 * @return Static string literal; never NULL.
 */
static const char *api_scan_state_name(scan_state_t state)
{
    switch (state) {
        case SCAN_IDLE:      return "idle";
        case SCAN_RUNNING:   return "scanning";
        case SCAN_CANCELLED: return "cancelled";
        case SCAN_COMPLETE:  return "done";
        default:             return "idle";
    }
}

int web_core_build_api_scan_json(char *out, size_t out_size, const bus_scan_status_t *status)
{
    int n = snprintf(out, out_size, "{\"ok\":true,\"state\":\"%s\"", api_scan_state_name(status->state));
    if (n < 0) {
        return n;
    }

    if (status->state == SCAN_RUNNING && (size_t)n < out_size) {
        int added = snprintf(out + n, out_size - (size_t)n, ",\"current\":%u", status->current_addr);
        if (added < 0) {
            return added;
        }
        n += added;
    }

    if ((size_t)n < out_size) {
        int added = snprintf(out + n, out_size - (size_t)n, ",\"range\":[%u,%u],\"found\":[",
                              status->range_start, status->range_end);
        if (added < 0) {
            return added;
        }
        n += added;
    }

    for (uint8_t i = 0; i < status->found_count; i++) {
        if ((size_t)n >= out_size) {
            break;
        }
        int added = snprintf(out + n, out_size - (size_t)n,
            "%s{\"slave\":%u,\"functions_ok\":[%u],\"round_trip_ms\":%u}",
            (i == 0) ? "" : ",", status->found[i], status->found_fc[i],
            (unsigned)status->found_round_trip_ms[i]);
        if (added < 0) {
            return added;
        }
        n += added;
    }

    if ((size_t)n < out_size) {
        int added = snprintf(out + n, out_size - (size_t)n, "]");
        if (added > 0) {
            n += added;
        }
    }

    if (status->state == SCAN_COMPLETE && (size_t)n < out_size) {
        int added = snprintf(out + n, out_size - (size_t)n, ",\"duration_ms\":%u", (unsigned)status->duration_ms);
        if (added > 0) {
            n += added;
        }
    }

    if ((size_t)n < out_size) {
        int added = snprintf(out + n, out_size - (size_t)n, "}");
        if (added > 0) {
            n += added;
        }
    }

    return n;
}

int web_core_build_api_spec_json(char *out, size_t out_size, const char *dut_register_snapshot_json)
{
    if (!dut_register_snapshot_json) {
        dut_register_snapshot_json = "null";
    }
    return snprintf(out, out_size,
        "{\"api\":\"windmeters-modbus-interface-tester\",\"version\":\"1\","
        "\"endpoints\":["
        "{\"method\":\"POST\",\"path\":\"/api/v1/modbus\",\"summary\":\"Execute one Modbus transaction and return the outcome.\"},"
        "{\"method\":\"GET\",\"path\":\"/api/v1/spec\",\"summary\":\"Machine-readable description of this API.\"},"
        "{\"method\":\"GET\",\"path\":\"/api/v1/status\",\"summary\":\"Tester and bus health snapshot.\"},"
        "{\"method\":\"POST\",\"path\":\"/api/v1/scan\",\"summary\":\"Start (or run to completion if wait:true) a bus scan.\"},"
        "{\"method\":\"GET\",\"path\":\"/api/v1/scan\",\"summary\":\"Poll bus scan progress or results.\"},"
        "{\"method\":\"GET\",\"path\":\"/api/v1/log\",\"summary\":\"Recent Modbus traffic log entries.\"},"
        "{\"method\":\"GET\",\"path\":\"/api/v1/wind\",\"summary\":\"Cached wind reading from the active Wind Test target.\"}"
        "],"
        "\"statuses\":{"
        "\"ok\":\"Valid reply received.\","
        "\"timeout\":\"No reply within timeout_ms after all retries.\","
        "\"crc_error\":\"Reply received but CRC check failed.\","
        "\"exception\":\"Slave returned a Modbus exception - see exception_code/exception_name.\","
        "\"framing_error\":\"Malformed reply - wrong length, echo, or function.\","
        "\"param_error\":\"Request rejected before transmission - bad count, function, or missing values.\","
        "\"bad_request\":\"JSON malformed or a field failed validation at the HTTP layer.\","
        "\"busy\":\"wait:false and the bus or scanner is occupied.\","
        "\"no_reply\":\"Internal: master task did not answer within its own watchdog.\""
        "},"
        "\"dut_register_snapshot\":%s"
        "}",
        dut_register_snapshot_json);
}
