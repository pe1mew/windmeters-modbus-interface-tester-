/**
 * @file web_core.h
 * @brief Web server — the testable core (TASK-WEB, design/completeRealisationPlan.md).
 *
 * Pure JSON-building and the Modicon/raw register conversion — no
 * ESPAsyncWebServer, no WiFi, no networking calls, so it's host-testable.
 * Manual snprintf-based building rather than a JSON library: these
 * payloads are small and fixed-shape (design/scratchbook.md §7), so a
 * library dependency wasn't worth adding just for this.
 *
 * Request *parsing* (incoming POST bodies) lives in web_server_task.cpp
 * instead — same division of labour as everywhere else in this project:
 * the Arduino-only orchestration layer has no tests of its own.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "bus_scan.h"
#include "wind_poll.h"
#include "mb_master.h"

/**
 * @brief Modicon register number (e.g. 40003) -> raw 0-based wire address.
 *
 * design/scratchbook.md §5's formula: wire_address = (modicon_number % 10000) - 1.
 * 30001 -> 0x0000, 40003 -> 0x0002 (the doc's own worked examples).
 */
uint16_t web_core_modicon_to_raw(uint32_t modicon_number);

/**
 * @brief Build the `type:"scan"` WebSocket payload (design/scratchbook.md §7).
 * @return Number of bytes written (excluding the null terminator), matching snprintf's convention.
 */
int web_core_build_scan_json(char *out, size_t out_size, const bus_scan_status_t *status);

/** @brief Build the `type:"wind"` WebSocket payload. */
int web_core_build_wind_json(char *out, size_t out_size, const wind_reading_t *reading,
                              bool has_data, uint32_t age_ms);

/** @brief Build the `type:"status"` WebSocket payload. */
int web_core_build_status_json(char *out, size_t out_size,
                                const char *wifi_mode, const char *wifi_ssid,
                                const char *wifi_ip, int8_t wifi_rssi,
                                bool ntp_synced, const char *local_time_iso,
                                uint32_t uptime_s, const mb_bus_health_t *bus_health);
