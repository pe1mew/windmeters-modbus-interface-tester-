#include "web_core.h"
#include <stdio.h>

uint16_t web_core_modicon_to_raw(uint32_t modicon_number)
{
    return (uint16_t)((modicon_number % 10000u) - 1u);
}

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

    for (uint8_t i = 0; i < status->found_count; i++) {
        if ((size_t)n >= out_size) {
            break;
        }
        int added = snprintf(out + n, out_size - (size_t)n, "%s%u", (i == 0) ? "" : ",", status->found[i]);
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

int web_core_build_wind_json(char *out, size_t out_size, const wind_reading_t *reading,
                              bool has_data, uint32_t age_ms)
{
    if (!has_data) {
        return snprintf(out, out_size, "{\"type\":\"wind\",\"has_data\":false}");
    }
    return snprintf(out, out_size,
        "{\"type\":\"wind\",\"has_data\":true,"
        "\"dir_instant_deg\":%.1f,\"dir_avg_deg\":%.1f,"
        "\"speed_instant_ms\":%.1f,\"speed_avg_ms\":%.1f,"
        "\"raw_pulses\":%u,\"age_ms\":%u}",
        (double)reading->dir_instant_deg, (double)reading->dir_avg_deg,
        (double)reading->speed_instant_ms, (double)reading->speed_avg_ms,
        (unsigned)reading->raw_pulses, (unsigned)age_ms);
}

int web_core_build_status_json(char *out, size_t out_size,
                                const char *wifi_mode, const char *wifi_ssid,
                                const char *wifi_ip, int8_t wifi_rssi,
                                bool ntp_synced, const char *local_time_iso,
                                uint32_t uptime_s, const mb_bus_health_t *bus_health)
{
    char exc_buf[8];
    if (bus_health->has_exception) {
        snprintf(exc_buf, sizeof(exc_buf), "%u", bus_health->last_exception);
    } else {
        snprintf(exc_buf, sizeof(exc_buf), "null");
    }

    return snprintf(out, out_size,
        "{\"type\":\"status\",\"wifi_mode\":\"%s\",\"wifi_ssid\":\"%s\",\"wifi_ip\":\"%s\","
        "\"wifi_rssi\":%d,\"ntp_synced\":%s,\"local_time\":\"%s\",\"uptime_s\":%u,"
        "\"bus\":{\"crc_errors\":%u,\"timeouts\":%u,\"last_exception\":%s}}",
        wifi_mode, wifi_ssid, wifi_ip, (int)wifi_rssi, ntp_synced ? "true" : "false",
        local_time_iso, (unsigned)uptime_s,
        (unsigned)bus_health->crc_errors, (unsigned)bus_health->timeouts, exc_buf);
}
