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
 * Preferences keys are limited to 15 characters — every key below fits.
 */
#pragma once

#define CFG_KEY_WIFI_SSID            "wifi_ssid"
#define CFG_KEY_WIFI_PASS            "wifi_pass"
#define CFG_KEY_NTP_SERVER           "ntp_server"
#define CFG_KEY_TZ_POSIX             "tz_posix"
#define CFG_KEY_MB_BAUD              "mb_baud"
#define CFG_KEY_MB_TIMEOUT_MS        "mb_timeout_ms"
#define CFG_KEY_MB_RETRIES           "mb_retries"
#define CFG_KEY_SCAN_RANGE_START     "scan_range_start"
#define CFG_KEY_SCAN_RANGE_END       "scan_range_end"
#define CFG_KEY_WIND_TEST_ADDR       "wind_test_addr"
#define CFG_KEY_WIND_POLL_INTERVAL   "wind_poll_interval_ms"

#define CFG_DEFAULT_WIFI_SSID          ""
#define CFG_DEFAULT_WIFI_PASS          ""
#define CFG_DEFAULT_NTP_SERVER         "pool.ntp.org"
#define CFG_DEFAULT_TZ_POSIX           ""
#define CFG_DEFAULT_MB_BAUD            9600u
#define CFG_DEFAULT_MB_TIMEOUT_MS      200u
#define CFG_DEFAULT_MB_RETRIES         1u
#define CFG_DEFAULT_SCAN_RANGE_START   1u
#define CFG_DEFAULT_SCAN_RANGE_END     247u
#define CFG_DEFAULT_WIND_TEST_ADDR     31u
#define CFG_DEFAULT_WIND_POLL_INTERVAL 1000u
