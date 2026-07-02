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
#include "mb_log.h"

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

/**
 * @brief Build the `type:"wind"` WebSocket payload.
 *
 * Wind speed and wind direction are physically separate units
 * (wind_poll.h) — this emits only the fields @p type's firmware actually
 * has (speed_instant_ms/speed_avg_ms/raw_pulses, or dir_instant_deg/
 * dir_avg_deg), tagged with `"sensor_type"` so the GUI's two Wind tabs
 * know which one an update belongs to.
 */
int web_core_build_wind_json(char *out, size_t out_size, wind_sensor_type_t type,
                              const wind_reading_t *reading, bool has_data, uint32_t age_ms);

/**
 * @brief Build the `type:"status"` WebSocket payload.
 *
 * Includes the configured @p mb_timeout_ms / @p mb_retries (not just bus
 * health counters) so the GUI's System Settings tab can pre-populate its
 * Modbus Timeout/Retries fields from the live status stream instead of
 * loading empty — same values the API's /api/v1/status exposes, named the
 * same way, for the same reason.
 */
int web_core_build_status_json(char *out, size_t out_size,
                                const char *wifi_mode, const char *wifi_ssid,
                                const char *wifi_ip, int8_t wifi_rssi,
                                bool ntp_synced, const char *local_time_iso,
                                uint32_t uptime_s,
                                uint16_t mb_timeout_ms, uint8_t mb_retries,
                                const mb_bus_health_t *bus_health);

/* ---------------------------------------------------------------------------
 * Machine API (design/api.md) — POST /api/v1/modbus and GET /api/v1/spec.
 *
 * Request *parsing* (JSON field extraction) stays in web_server_task.cpp,
 * same division of labour as the rest of this file (see the top-of-file
 * comment) — what lives here is the alias/status/exception lookups and the
 * response JSON building, all pure and host-testable.
 * ------------------------------------------------------------------------- */

/** @brief True if fc is one of the four function codes design/api.md supports (3, 4, 6, 16). */
bool web_core_is_valid_function_code(uint8_t fc);

/**
 * @brief Resolve a `"function"` alias string to its FC byte (design/api.md §4.1).
 * @return true and sets *out_fc for a known alias ("read_holding"=3,
 *         "read_input"=4, "write_single"=6, "write_multiple"=16);
 *         false (out_fc untouched) for anything else, including NULL.
 */
bool web_core_resolve_function_alias(const char *alias, uint8_t *out_fc);

/** @brief Map an mb_status_t to the API's status vocabulary (design/api.md §7). Never NULL. */
const char *web_core_api_status_name(mb_status_t status);

/**
 * @brief Human-readable name for a standard Modbus exception code (design/api.md §7).
 * @return "unknown" for a code outside the standard 1/2/3/4/5/6/8/10/11 table.
 */
const char *web_core_exception_name(uint8_t exception_code);

/**
 * @brief Build the `POST /api/v1/modbus` success response (design/api.md §4.2).
 *
 * Emits `"registers"` + `"count"` for fc 3/4 (read), `"written"` for fc 6/16
 * (write) — same @p values / @p value_count either way, different JSON key
 * per the documented shape.
 *
 * @param raw_tx/raw_rx     Wire bytes, formatted as uppercase space-separated
 *                          hex (design/api.md §3).
 * @param ts                Pre-resolved timestamp value (§3) — caller decides
 *                          real ISO-8601 vs. an uptime-ms string.
 * @param ts_is_uptime      true adds `"clock":"uptime"` per §3's convention
 *                          for the not-yet-NTP-synced case.
 */
int web_core_build_api_modbus_ok_json(char *out, size_t out_size,
                                       uint8_t slave, uint8_t fc, uint16_t register_addr,
                                       const uint16_t *values, uint8_t value_count,
                                       const uint8_t *raw_tx, uint16_t raw_tx_len,
                                       const uint8_t *raw_rx, uint16_t raw_rx_len,
                                       uint32_t round_trip_ms, uint8_t attempts,
                                       const char *ts, bool ts_is_uptime);

/**
 * @brief Build the `POST /api/v1/modbus` failure response (design/api.md §4.3).
 *
 * @param status          Bus-level outcome; must not be MB_OK (use the
 *                         `_ok_` builder for that case).
 * @param exception_code  Only meaningful when status == MB_ERR_EXCEPTION;
 *                         ignored (and `exception_code`/`exception_name`
 *                         omitted from the JSON) otherwise.
 * @param raw_rx_len       0 for a pure timeout — omits `raw_rx` from the
 *                          JSON entirely, matching §4.2's field-presence rule.
 */
int web_core_build_api_modbus_error_json(char *out, size_t out_size,
                                          mb_status_t status,
                                          uint8_t slave, uint8_t fc, uint16_t register_addr,
                                          uint8_t exception_code,
                                          const uint8_t *raw_tx, uint16_t raw_tx_len,
                                          const uint8_t *raw_rx, uint16_t raw_rx_len,
                                          uint8_t attempts,
                                          const char *detail, const char *hint);

/**
 * @brief Build `GET /api/v1/spec`'s self-description JSON (design/api.md §5.1).
 *
 * `api`/`version`/`endpoints`/`statuses` are fixed firmware-build-time
 * content, built entirely inside this function. `dut_register_snapshot` is
 * the one genuinely dynamic part — accepted as a pre-built JSON object
 * fragment (e.g. `{"input_registers":[...],"holding_registers":[...]}`) so
 * this function doesn't hardcode DUT-specific register names, matching this
 * project's "register map is provisional" principle (scratchbook.md §6).
 */
int web_core_build_api_spec_json(char *out, size_t out_size, const char *dut_register_snapshot_json);

/** @brief Build `GET /api/v1/status`'s JSON (design/api.md §5.2) — a differently-shaped, machine-oriented sibling of the `type:"status"` WebSocket payload above. */
int web_core_build_api_status_json(char *out, size_t out_size,
                                    uint32_t uptime_s,
                                    const char *wifi_mode, const char *wifi_ssid,
                                    const char *wifi_ip, int8_t wifi_rssi,
                                    bool ntp_synced,
                                    uint32_t mb_baud, uint16_t mb_timeout_ms, uint8_t mb_retries,
                                    const mb_bus_health_t *bus_health,
                                    bool scan_running, bool wind_poll_active);

/**
 * @brief Build `GET /api/v1/wind`'s JSON (design/api.md §5.5).
 *
 * `target`/`sensor_type` are only meaningful (and only emitted) when
 * @p wind_active is true — matches the panel-inactive case being
 * indistinguishable from "active but nothing read yet" in the
 * has_data:false shape. Only emits the fields @p type's firmware actually
 * has, same reasoning as web_core_build_wind_json().
 */
int web_core_build_api_wind_json(char *out, size_t out_size, uint8_t target, wind_sensor_type_t type,
                                  bool wind_active, const wind_reading_t *reading, bool has_data, uint32_t age_ms);

/**
 * @brief Build `GET /api/v1/log`'s JSON (design/api.md §5.4).
 *
 * @param entries  As returned by mblog_get_recent() — newest first.
 * @param count    Number of valid entries in @p entries.
 *
 * Emits entries oldest-first ("newest last" per §5.4) by walking @p entries
 * in reverse. `ts` is the raw millis-since-boot value already stored in
 * each entry (modbus_master_task currently always passes millis(), never an
 * NTP-converted epoch, regardless of sync state — see design/whatsNext.md),
 * tagged `"clock":"uptime"` accordingly rather than presented as if it were
 * wall-clock time it isn't.
 */
int web_core_build_api_log_json(char *out, size_t out_size, const mb_log_entry_t *entries, size_t count);

/**
 * @brief Format an uptime-millis value as "HH:MM:SS" (hours can exceed 24).
 *
 * Used for the GUI's Modbus Log table — `/api/v1/log` (above) keeps its own
 * raw-ms + `"clock":"uptime"` contract for machine clients (design/api.md
 * §3); this is the human-display sibling, GUI-side only per that scoping.
 * @return Number of bytes written (excluding the null terminator, matching
 *         snprintf's convention); @p out is always exactly 8 chars + NUL
 *         when @p out_size >= 9.
 */
int web_core_format_uptime_hhmmss(char *out, size_t out_size, uint32_t uptime_ms);

/**
 * @brief Build `POST`/`GET /api/v1/scan`'s response JSON (design/api.md §5.3).
 *
 * One shape for both the completed-sweep result and the while-running poll
 * response — which of `current`/`duration_ms` appears is driven directly
 * by `status->state` (present only for SCAN_RUNNING / SCAN_COMPLETE
 * respectively). Uses api.md's own state vocabulary ("scanning"/"done"),
 * which is deliberately not the same as the WebSocket `type:"scan"`
 * payload's ("running"/"complete") — api.md documents its own strings.
 * `functions_ok` is always a one-element array today, since bus_scan only
 * ever probes FC04 per address — a real field rather than a hardcoded
 * assumption, for when/if multi-FC probing is added.
 */
int web_core_build_api_scan_json(char *out, size_t out_size, const bus_scan_status_t *status);
