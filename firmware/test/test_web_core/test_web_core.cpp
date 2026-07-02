/**
 * web_core JSON-building / Modicon-conversion core — unit tests (native build)
 *
 * Run with:  pio test -e native -f test_web_core
 */
#include <unity.h>
#include "web_core.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── Modicon conversion — scratchbook.md §5's own worked examples ── */

void test_modicon_30001_converts_to_0x0000(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x0000, web_core_modicon_to_raw(30001));
}

void test_modicon_40003_converts_to_0x0002(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x0002, web_core_modicon_to_raw(40003));
}

void test_modicon_30005_converts_to_0x0004(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x0004, web_core_modicon_to_raw(30005));
}

/* ── scan JSON ── */

void test_scan_json_idle_empty_found(void)
{
    bus_scan_status_t status;
    memset(&status, 0, sizeof(status));
    status.state = SCAN_IDLE;

    char buf[128];
    web_core_build_scan_json(buf, sizeof(buf), &status);
    TEST_ASSERT_EQUAL_STRING(
        "{\"type\":\"scan\",\"state\":\"idle\",\"current_addr\":0,\"range_end\":0,\"found\":[]}",
        buf);
}

void test_scan_json_running_with_found(void)
{
    bus_scan_status_t status;
    memset(&status, 0, sizeof(status));
    status.state = SCAN_RUNNING;
    status.current_addr = 37;
    status.range_end = 247;
    status.found[0] = 1;         status.found_fc[0] = 4;  status.found_round_trip_ms[0] = 380;
    status.found[1] = 31;        status.found_fc[1] = 4;  status.found_round_trip_ms[1] = 36;
    status.found[2] = 44;        status.found_fc[2] = 4;  status.found_round_trip_ms[2] = 41;
    status.found_count = 3;

    char buf[256];
    web_core_build_scan_json(buf, sizeof(buf), &status);
    TEST_ASSERT_EQUAL_STRING(
        "{\"type\":\"scan\",\"state\":\"running\",\"current_addr\":37,\"range_end\":247,\"found\":["
        "{\"slave\":1,\"functions_ok\":[4],\"round_trip_ms\":380},"
        "{\"slave\":31,\"functions_ok\":[4],\"round_trip_ms\":36},"
        "{\"slave\":44,\"functions_ok\":[4],\"round_trip_ms\":41}"
        "]}",
        buf);
}

void test_scan_json_complete_state(void)
{
    bus_scan_status_t status;
    memset(&status, 0, sizeof(status));
    status.state = SCAN_COMPLETE;

    char buf[128];
    web_core_build_scan_json(buf, sizeof(buf), &status);
    TEST_ASSERT_TRUE(strstr(buf, "\"state\":\"complete\"") != NULL);
}

/* ── wind JSON — split by sensor type (wind_poll.h) ── */

void test_wind_json_no_data_speed(void)
{
    wind_reading_t reading;
    memset(&reading, 0, sizeof(reading));

    char buf[256];
    web_core_build_wind_json(buf, sizeof(buf), WIND_SENSOR_SPEED, &reading, false, 0);
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"wind\",\"sensor_type\":\"speed\",\"has_data\":false}", buf);
}

void test_wind_json_no_data_direction(void)
{
    wind_reading_t reading;
    memset(&reading, 0, sizeof(reading));

    char buf[256];
    web_core_build_wind_json(buf, sizeof(buf), WIND_SENSOR_DIRECTION, &reading, false, 0);
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"wind\",\"sensor_type\":\"direction\",\"has_data\":false}", buf);
}

void test_wind_json_speed_with_data(void)
{
    /* Speed half of scratchbook.md §7's Wind Test mockup. */
    wind_reading_t reading;
    memset(&reading, 0, sizeof(reading));
    reading.speed_instant_ms = 4.2f;
    reading.speed_avg_ms     = 3.9f;
    reading.raw_pulses       = 27;

    char buf[256];
    web_core_build_wind_json(buf, sizeof(buf), WIND_SENSOR_SPEED, &reading, true, 420);
    TEST_ASSERT_EQUAL_STRING(
        "{\"type\":\"wind\",\"sensor_type\":\"speed\",\"has_data\":true,"
        "\"speed_instant_ms\":4.2,\"speed_avg_ms\":3.9,\"raw_pulses\":27,\"age_ms\":420}",
        buf);
}

void test_wind_json_direction_with_data(void)
{
    /* Direction half of scratchbook.md §7's Wind Test mockup. */
    wind_reading_t reading;
    memset(&reading, 0, sizeof(reading));
    reading.dir_instant_deg = 183.4f;
    reading.dir_avg_deg     = 181.0f;

    char buf[256];
    web_core_build_wind_json(buf, sizeof(buf), WIND_SENSOR_DIRECTION, &reading, true, 420);
    TEST_ASSERT_EQUAL_STRING(
        "{\"type\":\"wind\",\"sensor_type\":\"direction\",\"has_data\":true,"
        "\"dir_instant_deg\":183.4,\"dir_avg_deg\":181.0,\"age_ms\":420}",
        buf);
}

/* ── status JSON ── */

void test_status_json_no_exception(void)
{
    mb_bus_health_t health;
    memset(&health, 0, sizeof(health));
    health.crc_errors = 2;
    health.timeouts    = 5;
    health.has_exception = false;

    char buf[384];
    web_core_build_status_json(buf, sizeof(buf), "0.4.0", "STA", "test-network", "192.168.20.185", -45,
                                true, "2026-07-01T17:45:21", 12345, 200, 1, &health);

    TEST_ASSERT_TRUE(strstr(buf, "\"fw_version\":\"0.4.0\"") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "\"wifi_mode\":\"STA\"") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "\"wifi_rssi\":-45") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "\"ntp_synced\":true") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "\"mb_timeout_ms\":200") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "\"mb_retries\":1") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "\"crc_errors\":2") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "\"timeouts\":5") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "\"last_exception\":null") != NULL);
}

void test_status_json_with_exception(void)
{
    mb_bus_health_t health;
    memset(&health, 0, sizeof(health));
    health.has_exception = true;
    health.last_exception = 0x02;

    char buf[384];
    web_core_build_status_json(buf, sizeof(buf), "0.4.0", "AP", "", "192.168.4.1", 0,
                                false, "1970-01-01T00:00:00", 10, 200, 1, &health);
    TEST_ASSERT_TRUE(strstr(buf, "\"last_exception\":2") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "\"ntp_synced\":false") != NULL);
}

/* ── machine API (design/api.md) — function code resolution ── */

void test_valid_function_codes_accepted(void)
{
    TEST_ASSERT_TRUE(web_core_is_valid_function_code(3));
    TEST_ASSERT_TRUE(web_core_is_valid_function_code(4));
    TEST_ASSERT_TRUE(web_core_is_valid_function_code(6));
    TEST_ASSERT_TRUE(web_core_is_valid_function_code(16));
}

void test_invalid_function_codes_rejected(void)
{
    TEST_ASSERT_FALSE(web_core_is_valid_function_code(0));
    TEST_ASSERT_FALSE(web_core_is_valid_function_code(1));
    TEST_ASSERT_FALSE(web_core_is_valid_function_code(5));
    TEST_ASSERT_FALSE(web_core_is_valid_function_code(17));
    TEST_ASSERT_FALSE(web_core_is_valid_function_code(255));
}

void test_function_alias_resolves_all_four(void)
{
    uint8_t fc = 0;
    TEST_ASSERT_TRUE(web_core_resolve_function_alias("read_holding", &fc));
    TEST_ASSERT_EQUAL_UINT8(3, fc);
    TEST_ASSERT_TRUE(web_core_resolve_function_alias("read_input", &fc));
    TEST_ASSERT_EQUAL_UINT8(4, fc);
    TEST_ASSERT_TRUE(web_core_resolve_function_alias("write_single", &fc));
    TEST_ASSERT_EQUAL_UINT8(6, fc);
    TEST_ASSERT_TRUE(web_core_resolve_function_alias("write_multiple", &fc));
    TEST_ASSERT_EQUAL_UINT8(16, fc);
}

void test_function_alias_rejects_unknown_and_null(void)
{
    uint8_t fc = 99;
    TEST_ASSERT_FALSE(web_core_resolve_function_alias("read_coils", &fc));
    TEST_ASSERT_EQUAL_UINT8(99, fc); /* untouched on failure */
    TEST_ASSERT_FALSE(web_core_resolve_function_alias(NULL, &fc));
}

/* ── machine API — status / exception name lookups ── */

void test_api_status_name_covers_every_mb_status(void)
{
    TEST_ASSERT_EQUAL_STRING("ok",            web_core_api_status_name(MB_OK));
    TEST_ASSERT_EQUAL_STRING("timeout",       web_core_api_status_name(MB_ERR_TIMEOUT));
    TEST_ASSERT_EQUAL_STRING("crc_error",     web_core_api_status_name(MB_ERR_CRC));
    TEST_ASSERT_EQUAL_STRING("exception",     web_core_api_status_name(MB_ERR_EXCEPTION));
    TEST_ASSERT_EQUAL_STRING("framing_error", web_core_api_status_name(MB_ERR_FRAMING));
    TEST_ASSERT_EQUAL_STRING("param_error",   web_core_api_status_name(MB_ERR_PARAM));
}

void test_exception_name_known_and_unknown_codes(void)
{
    TEST_ASSERT_EQUAL_STRING("illegal_function",      web_core_exception_name(1));
    TEST_ASSERT_EQUAL_STRING("illegal_data_address",  web_core_exception_name(2));
    TEST_ASSERT_EQUAL_STRING("illegal_data_value",    web_core_exception_name(3));
    TEST_ASSERT_EQUAL_STRING("slave_device_failure",  web_core_exception_name(4));
    TEST_ASSERT_EQUAL_STRING("unknown",               web_core_exception_name(99));
}

/* ── machine API — POST /api/v1/modbus JSON, api.md §4.2/§4.3's own worked examples ── */

void test_api_modbus_ok_json_read(void)
{
    const uint16_t registers[] = {1834, 42, 1810, 39, 27};
    const uint8_t raw_tx[] = {0x1F, 0x04, 0x00, 0x00, 0x00, 0x05, 0x33, 0xF0};
    const uint8_t raw_rx[] = {0x1F, 0x04, 0x0A, 0x07, 0x2A, 0x00, 0x2A, 0x07,
                               0x12, 0x00, 0x27, 0x00, 0x1B, 0x61, 0x4C};

    char buf[512];
    web_core_build_api_modbus_ok_json(buf, sizeof(buf),
        31, 4, 0x0000, registers, 5,
        raw_tx, sizeof(raw_tx), raw_rx, sizeof(raw_rx),
        38, 1, "2026-07-02T14:30:45Z", false);

    TEST_ASSERT_EQUAL_STRING(
        "{\"ok\":true,\"status\":\"ok\",\"slave\":31,\"function\":4,\"register\":0,"
        "\"count\":5,\"registers\":[1834,42,1810,39,27],"
        "\"raw_tx\":\"1F 04 00 00 00 05 33 F0\","
        "\"raw_rx\":\"1F 04 0A 07 2A 00 2A 07 12 00 27 00 1B 61 4C\","
        "\"round_trip_ms\":38,\"attempts\":1,\"ts\":\"2026-07-02T14:30:45Z\"}",
        buf);
}

void test_api_modbus_ok_json_write(void)
{
    const uint16_t written[] = {150};
    const uint8_t raw_frame[] = {0x1F, 0x06, 0x00, 0x01, 0x00, 0x96, 0x5B, 0x94};

    char buf[512];
    web_core_build_api_modbus_ok_json(buf, sizeof(buf),
        31, 6, 0x0001, written, 1,
        raw_frame, sizeof(raw_frame), raw_frame, sizeof(raw_frame),
        22, 1, "2026-07-02T14:31:02Z", false);

    TEST_ASSERT_EQUAL_STRING(
        "{\"ok\":true,\"status\":\"ok\",\"slave\":31,\"function\":6,\"register\":1,"
        "\"written\":[150],"
        "\"raw_tx\":\"1F 06 00 01 00 96 5B 94\",\"raw_rx\":\"1F 06 00 01 00 96 5B 94\","
        "\"round_trip_ms\":22,\"attempts\":1,\"ts\":\"2026-07-02T14:31:02Z\"}",
        buf);
}

void test_api_modbus_ok_json_uptime_clock_tag(void)
{
    const uint16_t registers[] = {1};
    const uint8_t raw[] = {0x01};

    char buf[512];
    web_core_build_api_modbus_ok_json(buf, sizeof(buf),
        1, 4, 0, registers, 1, raw, 1, raw, 1, 10, 1, "12345", true);

    TEST_ASSERT_TRUE(strstr(buf, "\"ts\":\"12345\",\"clock\":\"uptime\"}") != NULL);
}

void test_api_modbus_error_json_timeout(void)
{
    const uint8_t raw_tx[] = {0x23, 0x04, 0x00, 0x00, 0x00, 0x05, 0x32, 0x8D};

    char buf[512];
    web_core_build_api_modbus_error_json(buf, sizeof(buf),
        MB_ERR_TIMEOUT, 35, 4, 0x0000, 0,
        raw_tx, sizeof(raw_tx), NULL, 0,
        2,
        "No response from slave 35 within 200 ms (2 attempts).",
        "Nothing answered at address 35. Run POST /api/v1/scan to list responding addresses; "
        "the wind-speed DUT variant defaults to 30 (or 35 jumpered), the direction variant to "
        "31 (or 36 jumpered). Also check A/B wiring polarity and that exactly the bus ends are terminated.");

    TEST_ASSERT_EQUAL_STRING(
        "{\"ok\":false,\"status\":\"timeout\",\"slave\":35,\"function\":4,\"register\":0,"
        "\"raw_tx\":\"23 04 00 00 00 05 32 8D\",\"attempts\":2,"
        "\"detail\":\"No response from slave 35 within 200 ms (2 attempts).\","
        "\"hint\":\"Nothing answered at address 35. Run POST /api/v1/scan to list responding addresses; "
        "the wind-speed DUT variant defaults to 30 (or 35 jumpered), the direction variant to "
        "31 (or 36 jumpered). Also check A/B wiring polarity and that exactly the bus ends are terminated.\"}",
        buf);

    /* No raw_rx and no exception fields on a pure timeout. */
    TEST_ASSERT_NULL(strstr(buf, "raw_rx"));
    TEST_ASSERT_NULL(strstr(buf, "exception"));
}

void test_api_modbus_error_json_exception(void)
{
    const uint8_t raw_tx[] = {0x1F, 0x03, 0x00, 0x04, 0x00, 0x01, 0xC4, 0x4C};
    const uint8_t raw_rx[] = {0x1F, 0x83, 0x02, 0x61, 0x30};

    char buf[512];
    web_core_build_api_modbus_error_json(buf, sizeof(buf),
        MB_ERR_EXCEPTION, 31, 3, 0x0004, 2,
        raw_tx, sizeof(raw_tx), raw_rx, sizeof(raw_rx),
        1,
        "Slave 31 answered function 3 with exception 2 (illegal data address).",
        "The slave is alive but says holding register 4 does not exist. The DUT's register "
        "40005 (low-speed cutoff) is planned but not yet implemented in its firmware "
        "\xe2\x80\x94 see the DUT's own scratchBook.md.");

    TEST_ASSERT_EQUAL_STRING(
        "{\"ok\":false,\"status\":\"exception\","
        "\"exception_code\":2,\"exception_name\":\"illegal_data_address\","
        "\"slave\":31,\"function\":3,\"register\":4,"
        "\"raw_tx\":\"1F 03 00 04 00 01 C4 4C\",\"raw_rx\":\"1F 83 02 61 30\",\"attempts\":1,"
        "\"detail\":\"Slave 31 answered function 3 with exception 2 (illegal data address).\","
        "\"hint\":\"The slave is alive but says holding register 4 does not exist. The DUT's register "
        "40005 (low-speed cutoff) is planned but not yet implemented in its firmware "
        "\xe2\x80\x94 see the DUT's own scratchBook.md.\"}",
        buf);
}

/* ── machine API — GET /api/v1/status JSON, api.md §5.2 ── */

void test_api_status_json_shape(void)
{
    mb_bus_health_t health;
    memset(&health, 0, sizeof(health));
    health.crc_errors = 2;
    health.timeouts = 5;

    char buf[512];
    web_core_build_api_status_json(buf, sizeof(buf), "0.4.0", 4021,
        "STA", "bench", "192.168.1.42", -61,
        true, 9600, 200, 1, &health, false, true);

    TEST_ASSERT_EQUAL_STRING(
        "{\"ok\":true,\"fw_version\":\"0.4.0\",\"uptime_s\":4021,"
        "\"wifi\":{\"mode\":\"STA\",\"ssid\":\"bench\",\"ip\":\"192.168.1.42\",\"rssi\":-61},"
        "\"ntp_synced\":true,"
        "\"modbus\":{\"baud\":9600,\"timeout_ms\":200,\"retries\":1,\"crc_errors\":2,\"timeouts\":5,\"last_exception\":null},"
        "\"busy\":{\"scan_running\":false,\"wind_poll_active\":true}}",
        buf);
}

void test_api_status_json_with_exception(void)
{
    mb_bus_health_t health;
    memset(&health, 0, sizeof(health));
    health.has_exception = true;
    health.last_exception = 2;

    char buf[512];
    web_core_build_api_status_json(buf, sizeof(buf), "0.4.0", 0, "AP", "", "192.168.4.1", 0,
        false, 9600, 200, 1, &health, true, false);

    TEST_ASSERT_TRUE(strstr(buf, "\"last_exception\":2") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "\"scan_running\":true") != NULL);
}

/* ── machine API — GET /api/v1/wind JSON, api.md §5.5 ── */

void test_api_wind_json_inactive_panel(void)
{
    wind_reading_t reading;
    memset(&reading, 0, sizeof(reading));

    char buf[256];
    web_core_build_api_wind_json(buf, sizeof(buf), 31, WIND_SENSOR_SPEED, false, &reading, false, 0);
    TEST_ASSERT_EQUAL_STRING("{\"ok\":true,\"has_data\":false}", buf);
}

void test_api_wind_json_active_no_data_yet(void)
{
    wind_reading_t reading;
    memset(&reading, 0, sizeof(reading));

    char buf[256];
    web_core_build_api_wind_json(buf, sizeof(buf), 31, WIND_SENSOR_SPEED, true, &reading, false, 0);
    TEST_ASSERT_EQUAL_STRING("{\"ok\":true,\"has_data\":false}", buf);
}

void test_api_wind_json_speed_with_data(void)
{
    /* Speed half of scratchbook.md §7's worked example. */
    wind_reading_t reading;
    memset(&reading, 0, sizeof(reading));
    reading.speed_instant_ms = 4.2f;
    reading.speed_avg_ms     = 3.9f;
    reading.raw_pulses       = 27;

    char buf[256];
    web_core_build_api_wind_json(buf, sizeof(buf), 30, WIND_SENSOR_SPEED, true, &reading, true, 420);
    TEST_ASSERT_EQUAL_STRING(
        "{\"ok\":true,\"has_data\":true,\"target\":30,\"sensor_type\":\"speed\","
        "\"speed_instant_ms\":4.2,\"speed_avg_ms\":3.9,"
        "\"raw_pulses\":27,\"age_ms\":420}",
        buf);
}

void test_api_wind_json_direction_with_data(void)
{
    /* Direction half of scratchbook.md §7's worked example. */
    wind_reading_t reading;
    memset(&reading, 0, sizeof(reading));
    reading.dir_instant_deg = 183.4f;
    reading.dir_avg_deg     = 181.0f;

    char buf[256];
    web_core_build_api_wind_json(buf, sizeof(buf), 31, WIND_SENSOR_DIRECTION, true, &reading, true, 420);
    TEST_ASSERT_EQUAL_STRING(
        "{\"ok\":true,\"has_data\":true,\"target\":31,\"sensor_type\":\"direction\","
        "\"dir_instant_deg\":183.4,\"dir_avg_deg\":181.0,\"age_ms\":420}",
        buf);
}

/* ── machine API — GET /api/v1/log JSON, api.md §5.4 ── */

void test_api_log_json_empty(void)
{
    char buf[128];
    web_core_build_api_log_json(buf, sizeof(buf), NULL, 0);
    TEST_ASSERT_EQUAL_STRING("{\"ok\":true,\"entries\":[]}", buf);
}

void test_api_log_json_reverses_newest_first_to_oldest_first(void)
{
    /* mblog_get_recent()'s convention: entries[] arrives newest-first. */
    mb_log_entry_t entries[2];
    memset(entries, 0, sizeof(entries));

    entries[0].timestamp_ms = 200; /* newest */
    entries[0].is_tx = false;
    entries[0].raw[0] = 0xAA; entries[0].raw_len = 1;
    strcpy(entries[0].summary, "second");

    entries[1].timestamp_ms = 100; /* oldest */
    entries[1].is_tx = true;
    entries[1].raw[0] = 0xBB; entries[1].raw_len = 1;
    strcpy(entries[1].summary, "first");

    char buf[512];
    web_core_build_api_log_json(buf, sizeof(buf), entries, 2);

    TEST_ASSERT_EQUAL_STRING(
        "{\"ok\":true,\"entries\":["
        "{\"ts\":\"100\",\"clock\":\"uptime\",\"dir\":\"TX\",\"hex\":\"BB\",\"summary\":\"first\"},"
        "{\"ts\":\"200\",\"clock\":\"uptime\",\"dir\":\"RX\",\"hex\":\"AA\",\"summary\":\"second\"}"
        "]}",
        buf);
}

/* ── uptime HH:MM:SS formatting (GUI Modbus Log display) ── */

void test_format_uptime_hhmmss_basic(void)
{
    char buf[16];
    /* 1h 02m 03s = 3723000 ms */
    web_core_format_uptime_hhmmss(buf, sizeof(buf), 3723000u);
    TEST_ASSERT_EQUAL_STRING("01:02:03", buf);
}

void test_format_uptime_hhmmss_hours_can_exceed_24(void)
{
    /* 30h 00m 00s = 108000000 ms — uptime, not a wall clock, so no wrap at 24. */
    char buf[16];
    web_core_format_uptime_hhmmss(buf, sizeof(buf), 108000000u);
    TEST_ASSERT_EQUAL_STRING("30:00:00", buf);
}

void test_format_uptime_hhmmss_zero(void)
{
    char buf[16];
    web_core_format_uptime_hhmmss(buf, sizeof(buf), 0u);
    TEST_ASSERT_EQUAL_STRING("00:00:00", buf);
}

/* ── machine API — POST/GET /api/v1/scan JSON, api.md §5.3 ── */

void test_api_scan_json_scanning_shape(void)
{
    bus_scan_status_t status;
    memset(&status, 0, sizeof(status));
    status.state = SCAN_RUNNING;
    status.current_addr = 87;
    status.range_start = 1;
    status.range_end = 247;

    char buf[256];
    web_core_build_api_scan_json(buf, sizeof(buf), &status);
    TEST_ASSERT_EQUAL_STRING(
        "{\"ok\":true,\"state\":\"scanning\",\"current\":87,\"range\":[1,247],\"found\":[]}",
        buf);
}

void test_api_scan_json_done_shape_with_found_devices(void)
{
    bus_scan_status_t status;
    memset(&status, 0, sizeof(status));
    status.state = SCAN_COMPLETE;
    status.range_start = 1;
    status.range_end = 247;
    status.found[0] = 31;
    status.found_fc[0] = 4;
    status.found_round_trip_ms[0] = 36;
    status.found_count = 1;
    status.duration_ms = 98400;

    char buf[256];
    web_core_build_api_scan_json(buf, sizeof(buf), &status);
    TEST_ASSERT_EQUAL_STRING(
        "{\"ok\":true,\"state\":\"done\",\"range\":[1,247],\"found\":["
        "{\"slave\":31,\"functions_ok\":[4],\"round_trip_ms\":36}"
        "],\"duration_ms\":98400}",
        buf);
}

void test_api_scan_json_idle_no_current_no_duration(void)
{
    bus_scan_status_t status;
    memset(&status, 0, sizeof(status));
    status.state = SCAN_IDLE;

    char buf[256];
    web_core_build_api_scan_json(buf, sizeof(buf), &status);
    TEST_ASSERT_EQUAL_STRING("{\"ok\":true,\"state\":\"idle\",\"range\":[0,0],\"found\":[]}", buf);
}

/* ── machine API — GET /api/v1/spec JSON ── */

void test_api_spec_json_shape(void)
{
    char buf[2048];
    web_core_build_api_spec_json(buf, sizeof(buf), "{\"input_registers\":[]}");

    TEST_ASSERT_TRUE(strstr(buf, "\"api\":\"windmeters-modbus-interface-tester\"") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "\"version\":\"1\"") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "\"path\":\"/api/v1/modbus\"") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "\"path\":\"/api/v1/wind\"") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "\"no_reply\":") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "\"dut_register_snapshot\":{\"input_registers\":[]}") != NULL);
}

void test_api_spec_json_null_snapshot_is_valid_json_null(void)
{
    char buf[2048];
    web_core_build_api_spec_json(buf, sizeof(buf), NULL);
    TEST_ASSERT_TRUE(strstr(buf, "\"dut_register_snapshot\":null") != NULL);
}

int main(int /*argc*/, char ** /*argv*/)
{
    UNITY_BEGIN();
    RUN_TEST(test_modicon_30001_converts_to_0x0000);
    RUN_TEST(test_modicon_40003_converts_to_0x0002);
    RUN_TEST(test_modicon_30005_converts_to_0x0004);
    RUN_TEST(test_scan_json_idle_empty_found);
    RUN_TEST(test_scan_json_running_with_found);
    RUN_TEST(test_scan_json_complete_state);
    RUN_TEST(test_wind_json_no_data_speed);
    RUN_TEST(test_wind_json_no_data_direction);
    RUN_TEST(test_wind_json_speed_with_data);
    RUN_TEST(test_wind_json_direction_with_data);
    RUN_TEST(test_status_json_no_exception);
    RUN_TEST(test_status_json_with_exception);
    RUN_TEST(test_valid_function_codes_accepted);
    RUN_TEST(test_invalid_function_codes_rejected);
    RUN_TEST(test_function_alias_resolves_all_four);
    RUN_TEST(test_function_alias_rejects_unknown_and_null);
    RUN_TEST(test_api_status_name_covers_every_mb_status);
    RUN_TEST(test_exception_name_known_and_unknown_codes);
    RUN_TEST(test_api_modbus_ok_json_read);
    RUN_TEST(test_api_modbus_ok_json_write);
    RUN_TEST(test_api_modbus_ok_json_uptime_clock_tag);
    RUN_TEST(test_api_modbus_error_json_timeout);
    RUN_TEST(test_api_modbus_error_json_exception);
    RUN_TEST(test_api_spec_json_shape);
    RUN_TEST(test_api_spec_json_null_snapshot_is_valid_json_null);
    RUN_TEST(test_api_status_json_shape);
    RUN_TEST(test_api_status_json_with_exception);
    RUN_TEST(test_api_wind_json_inactive_panel);
    RUN_TEST(test_api_wind_json_active_no_data_yet);
    RUN_TEST(test_api_wind_json_speed_with_data);
    RUN_TEST(test_api_wind_json_direction_with_data);
    RUN_TEST(test_api_log_json_empty);
    RUN_TEST(test_api_log_json_reverses_newest_first_to_oldest_first);
    RUN_TEST(test_api_scan_json_scanning_shape);
    RUN_TEST(test_api_scan_json_done_shape_with_found_devices);
    RUN_TEST(test_api_scan_json_idle_no_current_no_duration);
    RUN_TEST(test_format_uptime_hhmmss_basic);
    RUN_TEST(test_format_uptime_hhmmss_hours_can_exceed_24);
    RUN_TEST(test_format_uptime_hhmmss_zero);
    return UNITY_END();
}
