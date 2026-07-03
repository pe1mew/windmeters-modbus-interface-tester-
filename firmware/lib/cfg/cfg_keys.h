/**
 * @file cfg_keys.h
 * @brief Named NVS keys + documented defaults (design/scratchbook.md §7).
 *
 * Single source of truth so Phase 2 call sites (wifi_manager_task,
 * modbus_master_task, scan_task, wind_poll_task, the web server) reference
 * the same key strings and defaults instead of retyping them ad hoc —
 * a typo'd key string would silently create a disconnected setting with
 * no error, so this is the one place they're spelled out.
 *
 * Preferences keys are limited to 15 characters (ESP-IDF NVS:
 * NVS_KEY_NAME_MAX_SIZE - 1). A key over that limit doesn't error or
 * crash — Preferences::put*() just silently fails (this file's own
 * cfg_backend_preferences.cpp discards its return value, matching every
 * other Preferences call site in this project) and every later get()
 * quietly falls back to the default forever. `test_cfg` has a native test
 * asserting every key below fits, specifically so this doesn't happen
 * again unnoticed — found the hard way once already (CFG_KEY_SCAN_RANGE_START
 * was 16 chars, CFG_KEY_WIND_POLL_INTERVAL was 21, both silently never
 * persisted; see memory/gotcha-log.md).
 */
#pragma once

#define CFG_KEY_WIFI_SSID            "wifi_ssid" /**< NVS key: STA-mode WiFi SSID to join. Empty default means AP-only until configured. */
#define CFG_KEY_WIFI_PASS            "wifi_pass" /**< NVS key: STA-mode WiFi password, paired with CFG_KEY_WIFI_SSID. */
#define CFG_KEY_NTP_SERVER           "ntp_server" /**< NVS key: NTP server hostname used by ntp_manager for time sync. */
#define CFG_KEY_MB_BAUD              "mb_baud" /**< NVS key: Modbus RTU baud rate. Configurable because DUT firmware baud is still moving. */
#define CFG_KEY_MB_TIMEOUT_MS        "mb_timeout_ms" /**< NVS key: mb_core response timeout per attempt, in ms (see mb_init()). */
#define CFG_KEY_MB_RETRIES           "mb_retries" /**< NVS key: mb_core additional attempts after the first before a transaction is marked failed. */
#define CFG_KEY_SCAN_RANGE_START     "scan_start" /**< NVS key: last-used Bus Scanner sweep start address. Shortened from "scan_range_start" (16 chars) — see file header. */
#define CFG_KEY_SCAN_RANGE_END       "scan_end" /**< NVS key: last-used Bus Scanner sweep end address, paired with CFG_KEY_SCAN_RANGE_START. */
/* Wind speed and wind direction are separate physical units with separate
 * addresses (wind_poll.h) — each tab remembers its own. Poll interval stays
 * shared: no reason a bench tool needs a different polling cadence per
 * sensor type. */
#define CFG_KEY_WIND_SPEED_ADDR      "wind_speed_addr" /**< NVS key: last-used Wind Speed tab target slave address. Split off the old shared "wind_test_addr" 2026-07-02. */
#define CFG_KEY_WIND_DIR_ADDR        "wind_dir_addr" /**< NVS key: last-used Wind Direction tab target slave address, independent of CFG_KEY_WIND_SPEED_ADDR. */
#define CFG_KEY_WIND_POLL_INTERVAL   "wind_poll_ms" /**< NVS key: shared poll cadence (ms) for both wind tabs. Shortened from "wind_poll_interval_ms" (21 chars) — see file header. */

#define CFG_DEFAULT_WIFI_SSID          "" /**< Default WiFi SSID — empty means AP-only until configured. */
#define CFG_DEFAULT_WIFI_PASS          "" /**< Default WiFi password — empty, paired with CFG_DEFAULT_WIFI_SSID. */
#define CFG_DEFAULT_NTP_SERVER         "pool.ntp.org" /**< Default NTP server — public pool, works out of the box on any network. */
#define CFG_DEFAULT_MB_BAUD            9600u /**< Default Modbus baud rate — standard RTU default, matches most DUT firmware out of the box. */
#define CFG_DEFAULT_MB_TIMEOUT_MS      200u /**< Default mb_core response timeout, ms — generous enough for a bench DUT over a short RS485 run. */
#define CFG_DEFAULT_MB_RETRIES         1u /**< Default mb_core retry count — one extra attempt before a transaction is marked failed. */
#define CFG_DEFAULT_SCAN_RANGE_START   1u /**< Default Bus Scanner sweep start — lowest valid Modbus unit address. */
#define CFG_DEFAULT_SCAN_RANGE_END     247u /**< Default Bus Scanner sweep end — highest valid Modbus unit address (0 is broadcast, excluded). */
#define CFG_DEFAULT_WIND_SPEED_ADDR    30u /**< Default Wind Speed tab target address. */
#define CFG_DEFAULT_WIND_DIR_ADDR      31u /**< Default Wind Direction tab target address. */
#define CFG_DEFAULT_WIND_POLL_INTERVAL 1000u /**< Default poll cadence, ms, shared by both wind tabs. */
