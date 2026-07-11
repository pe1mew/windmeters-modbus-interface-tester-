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
 * @param modicon_number Conventional Modicon-style register number
 *                        (30001-39999 input, 40001-49999 holding, etc).
 * @return Raw 0-based wire address to pass to mb_core's read/write calls.
 */
uint16_t web_core_modicon_to_raw(uint32_t modicon_number);

/**
 * @brief Build the `type:"scan"` WebSocket payload (design/scratchbook.md §7).
 * @param out       Destination buffer for the JSON text.
 * @param out_size  Capacity of @p out, including room for the null terminator.
 * @param status    Current sweep snapshot (design/scratchbook.md §7), copied
 *                   verbatim into the JSON — normally a bus_scan_get_status()
 *                   result taken by value so it can't change mid-build.
 * @return Number of bytes written (excluding the null terminator), matching snprintf's convention.
 */
int web_core_build_scan_json(char *out, size_t out_size, const bus_scan_status_t *status);

/**
 * @brief Build the `type:"wind"` WebSocket payload.
 *
 * Wind speed, wind direction, and combined are (up to) three physically
 * separate units (wind_poll.h) — this emits only the fields meaningful for
 * @p type, tagged with `"sensor_type"` so the GUI's three Wind tabs know
 * which one an update belongs to:
 *   - speed:     speed_instant_ms/speed_avg_ms/raw_pulses/gust_ms/seconds_since_pulse
 *   - direction: dir_instant_deg/dir_avg_deg/dir_fault/raw_adc
 *   - combined:  every field above, in one message — the combined build's
 *                register block carries both sensors' data (TDS §2.7), so
 *                its message is the union rather than a third distinct
 *                shape. Reuses the same key names ("raw_pulses"/"gust_ms"/
 *                "seconds_since_pulse" for the speed side, "raw_adc" for
 *                the direction side) so a client's field mapping stays
 *                uniform across all three sensor_type values.
 * All three branches decode from the same wind_reading_t — the split here
 * is purely about which of its fields are worth surfacing to a given tab,
 * not a wire-format difference.
 *
 * @param out       Destination buffer for the JSON text.
 * @param out_size  Capacity of @p out, including room for the null terminator.
 * @param type      Which Wind tab this update is for; selects both the
 *                   `"sensor_type"` tag and which reading fields are emitted.
 * @param reading   Decoded snapshot to report; ignored (may be NULL) when
 *                   @p has_data is false.
 * @param has_data  false emits only `{"type":"wind","sensor_type":...,
 *                   "has_data":false}` — no reading has come back yet for
 *                   this target, distinct from the target being inactive.
 * @param age_ms    Milliseconds since @p reading was captured; only emitted
 *                   when @p has_data is true.
 * @return Number of bytes written (excluding the null terminator), matching snprintf's convention.
 */
int web_core_build_wind_json(char *out, size_t out_size, wind_sensor_type_t type,
                              const wind_reading_t *reading, bool has_data, uint32_t age_ms);

/**
 * @brief Build the `type:"status"` WebSocket payload.
 *
 * Includes @p fw_version (the footer's version display reads this — GUI
 * restructure, 2026-07-02) and the configured @p mb_timeout_ms /
 * @p mb_retries (not just bus health counters) so the GUI's System
 * Settings tab can pre-populate its Modbus Timeout/Retries fields from the
 * live status stream instead of loading empty — same values the API's
 * /api/v1/status exposes, named the same way, for the same reason.
 *
 * @param out             Destination buffer for the JSON text.
 * @param out_size        Capacity of @p out, including room for the null terminator.
 * @param fw_version      Firmware version string, e.g. "1.17.0".
 * @param wifi_mode       "AP" or "STA" (or whatever the caller's WiFi layer
 *                         reports) — passed through verbatim.
 * @param wifi_ssid       Connected/hosted SSID.
 * @param wifi_ip         Current IP address as a dotted-quad string.
 * @param wifi_rssi       Signal strength in dBm.
 * @param ntp_synced      true once ntp_manager has a recorded sync reference
 *                         point; gates whether @p local_time_iso is wall-clock
 *                         or should be read cautiously by the GUI.
 * @param local_time_iso  Pre-formatted local time string; caller decides the
 *                         format/contents for both the synced and not-yet-
 *                         synced case.
 * @param uptime_s        Seconds since boot.
 * @param mb_timeout_ms   Currently configured mb_core response timeout
 *                         (mb_set_timeout()'s last value).
 * @param mb_retries      Currently configured mb_core retry count
 *                         (mb_set_retries()'s last value).
 * @param bus_health      Running bus health counters (mb_master_get_health());
 *                         `"last_exception"` is emitted as JSON null when
 *                         @p bus_health->has_exception is false.
 * @return Number of bytes written (excluding the null terminator), matching snprintf's convention.
 */
int web_core_build_status_json(char *out, size_t out_size,
                                const char *fw_version,
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

/**
 * @brief True if fc is one of the four function codes design/api.md supports (3, 4, 6, 16).
 * @param fc Function code byte to check, e.g. as decoded from the request JSON.
 * @return true if @p fc is 3, 4, 6, or 16; false for anything else.
 */
bool web_core_is_valid_function_code(uint8_t fc);

/**
 * @brief Resolve a `"function"` alias string to its FC byte (design/api.md §4.1).
 * @param alias   Alias string from the request JSON's `"function"` field, or
 *                 NULL.
 * @param out_fc  Set to the resolved FC byte on success; left untouched on
 *                 failure, so a caller must not read it unless this function
 *                 returned true.
 * @return true and sets *out_fc for a known alias ("read_holding"=3,
 *         "read_input"=4, "write_single"=6, "write_multiple"=16);
 *         false (out_fc untouched) for anything else, including NULL.
 */
bool web_core_resolve_function_alias(const char *alias, uint8_t *out_fc);

/**
 * @brief Map an mb_status_t to the API's status vocabulary (design/api.md §7). Never NULL.
 * @param status Outcome from an mb_core/mb_master transaction.
 * @return Lowercase snake_case status string per §7's table, e.g. "ok",
 *         "timeout", "crc_error"; "param_error" for any status value
 *         outside mb_status_t's known set (shouldn't happen, but the
 *         switch's default case guards against it).
 */
const char *web_core_api_status_name(mb_status_t status);

/**
 * @brief Human-readable name for a standard Modbus exception code (design/api.md §7).
 * @param exception_code Exception code byte from a Modbus exception response
 *                        (e.g. mb_last_exception_code()) — the standard
 *                        1/2/3/4/5/6/8/10/11 table, not necessarily the full
 *                        0-255 range a malformed slave could send.
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
 * @param out             Destination buffer for the JSON text.
 * @param out_size        Capacity of @p out, including room for the null terminator.
 * @param slave           Slave address the transaction targeted, 1-247.
 * @param fc              Function code actually used (3, 4, 6, or 16) —
 *                         determines whether `"registers"`/`"count"` or
 *                         `"written"` is emitted.
 * @param register_addr   Raw 0-based register address the transaction used.
 * @param values          For fc 3/4: the @p value_count decoded registers to
 *                         report as `"registers"`. For fc 6/16: the
 *                         @p value_count values echoed back as `"written"`.
 * @param value_count     Number of entries in @p values.
 * @param raw_tx/raw_rx   Wire bytes, formatted as uppercase space-separated
 *                          hex (design/api.md §3).
 * @param raw_tx_len      Number of valid bytes in @p raw_tx.
 * @param raw_rx_len      Number of valid bytes in @p raw_rx.
 * @param round_trip_ms   Wall-clock duration of the transaction.
 * @param attempts        1 + retries actually consumed (mb_get_last_attempts()).
 * @param ts                Pre-resolved timestamp value (§3) — caller decides
 *                          real ISO-8601 vs. an uptime-ms string.
 * @param ts_is_uptime      true adds `"clock":"uptime"` per §3's convention
 *                          for the not-yet-NTP-synced case.
 * @return Number of bytes written (excluding the null terminator), matching snprintf's convention.
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
 * @param out             Destination buffer for the JSON text.
 * @param out_size        Capacity of @p out, including room for the null terminator.
 * @param status          Bus-level outcome; must not be MB_OK (use the
 *                         `_ok_` builder for that case).
 * @param slave           Slave address the transaction targeted, 1-247.
 * @param fc              Function code that was attempted.
 * @param register_addr   Raw 0-based register address the transaction used.
 * @param exception_code  Only meaningful when status == MB_ERR_EXCEPTION;
 *                         ignored (and `exception_code`/`exception_name`
 *                         omitted from the JSON) otherwise.
 * @param raw_tx          Request wire bytes actually sent, formatted as
 *                         uppercase space-separated hex (design/api.md §3);
 *                         always emitted, even on failure.
 * @param raw_tx_len      Number of valid bytes in @p raw_tx.
 * @param raw_rx          Response wire bytes received, if any.
 * @param raw_rx_len       0 for a pure timeout — omits `raw_rx` from the
 *                          JSON entirely, matching §4.2's field-presence rule.
 * @param attempts        1 + retries actually consumed (0 if the request
 *                         never reached the transport, e.g. rejected by
 *                         parameter validation).
 * @param detail          Human-readable explanation string for the `"detail"` field.
 * @param hint            Human-readable suggestion string for the `"hint"` field.
 * @return Number of bytes written (excluding the null terminator), matching snprintf's convention.
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
 *
 * @param out                          Destination buffer for the JSON text.
 * @param out_size                     Capacity of @p out, including room for
 *                                      the null terminator.
 * @param dut_register_snapshot_json   Pre-built JSON object fragment (not a
 *                                      full document — no outer braces
 *                                      expected beyond its own) to splice in
 *                                      verbatim as the `"dut_register_snapshot"`
 *                                      value; NULL is treated as JSON `null`.
 * @return Number of bytes written (excluding the null terminator), matching snprintf's convention.
 */
int web_core_build_api_spec_json(char *out, size_t out_size, const char *dut_register_snapshot_json);

/**
 * @brief Build `GET /api/v1/status`'s JSON (design/api.md §5.2) — a differently-shaped, machine-oriented sibling of the `type:"status"` WebSocket payload above.
 *
 * @param out             Destination buffer for the JSON text.
 * @param out_size        Capacity of @p out, including room for the null terminator.
 * @param fw_version      Firmware version string, e.g. "1.17.0".
 * @param uptime_s        Seconds since boot.
 * @param wifi_mode       "AP" or "STA" (or whatever the caller's WiFi layer
 *                         reports) — passed through verbatim.
 * @param wifi_ssid       Connected/hosted SSID.
 * @param wifi_ip         Current IP address as a dotted-quad string.
 * @param wifi_rssi       Signal strength in dBm.
 * @param ntp_synced      true once ntp_manager has a recorded sync reference point.
 * @param mb_baud         Configured UART baud rate for the Modbus bus.
 * @param mb_timeout_ms   Currently configured mb_core response timeout
 *                         (mb_set_timeout()'s last value).
 * @param mb_retries      Currently configured mb_core retry count
 *                         (mb_set_retries()'s last value).
 * @param bus_health      Running bus health counters (mb_master_get_health());
 *                         `"last_exception"` is emitted as JSON null when
 *                         @p bus_health->has_exception is false.
 * @param scan_running    true if a bus_scan sweep is currently in progress —
 *                         surfaced so a machine client can avoid submitting
 *                         a Modbus transaction that would just get `"busy"`.
 * @param wind_poll_active true if the Wind Test poll loop is currently active
 *                         for the same "avoid a busy response" reason.
 * @return Number of bytes written (excluding the null terminator), matching snprintf's convention.
 */
int web_core_build_api_status_json(char *out, size_t out_size,
                                    const char *fw_version,
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
 *
 * @param out          Destination buffer for the JSON text.
 * @param out_size     Capacity of @p out, including room for the null terminator.
 * @param target       Slave address of the currently active Wind Test target;
 *                      only meaningful (and only emitted) when @p wind_active
 *                      is true.
 * @param type         Which sensor type @p target is (speed, direction, or
 *                      combined); selects which reading fields would be
 *                      emitted — see web_core_build_wind_json() for the
 *                      per-type field lists (combined is the union of both).
 * @param wind_active  false collapses straight to `{"ok":true,"has_data":
 *                      false}` regardless of @p reading/@p has_data — no
 *                      Wind Test target is configured at all.
 * @param reading      Decoded snapshot to report; ignored (may be NULL)
 *                      unless both @p wind_active and @p has_data are true.
 * @param has_data     false (with @p wind_active true) means a target is
 *                      configured but no poll has completed yet — still
 *                      reported as `has_data:false`, indistinguishable on
 *                      the wire from the wind_active:false case.
 * @param age_ms       Milliseconds since @p reading was captured; only
 *                      emitted when both @p wind_active and @p has_data
 *                      are true.
 * @return Number of bytes written (excluding the null terminator), matching snprintf's convention.
 */
int web_core_build_api_wind_json(char *out, size_t out_size, uint8_t target, wind_sensor_type_t type,
                                  bool wind_active, const wind_reading_t *reading, bool has_data, uint32_t age_ms);

/**
 * @brief Build `GET /api/v1/log`'s JSON (design/api.md §5.4).
 *
 * @param out      Destination buffer for the JSON text.
 * @param out_size Capacity of @p out, including room for the null terminator.
 * @param entries  As returned by mblog_get_recent() — newest first.
 * @param count    Number of valid entries in @p entries.
 *
 * Emits entries oldest-first ("newest last" per §5.4) by walking @p entries
 * in reverse. Each entry's `ts` is independently resolved via
 * ntp_manager_is_synced()/ntp_manager_millis_to_epoch() at build time (real
 * ISO-8601 once synced, else the raw millis-since-boot value tagged
 * `"clock":"uptime"`) — same §3 convention as the modbus endpoints, applied
 * per stored entry rather than to "now", since these are historical records.
 * @return Number of bytes written (excluding the null terminator), matching snprintf's convention.
 */
int web_core_build_api_log_json(char *out, size_t out_size, const mb_log_entry_t *entries, size_t count);

/**
 * @brief Format an uptime-millis value as "HH:MM:SS" (hours can exceed 24).
 *
 * Used for the GUI's Modbus Log table — `/api/v1/log` (above) keeps its own
 * raw-ms + `"clock":"uptime"` contract for machine clients (design/api.md
 * §3); this is the human-display sibling, GUI-side only per that scoping.
 * @param out       Destination buffer for the formatted string.
 * @param out_size  Capacity of @p out, including room for the null terminator.
 * @param uptime_ms Milliseconds since boot to format, e.g. an
 *                   mb_log_entry_t.timestamp_ms value.
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
 *
 * @param out       Destination buffer for the JSON text.
 * @param out_size  Capacity of @p out, including room for the null terminator.
 * @param status    Current sweep snapshot; @p status->state selects which
 *                   optional fields (`current`, `duration_ms`) are present,
 *                   see above.
 * @return Number of bytes written (excluding the null terminator), matching snprintf's convention.
 */
int web_core_build_api_scan_json(char *out, size_t out_size, const bus_scan_status_t *status);
