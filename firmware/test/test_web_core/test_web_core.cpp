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
    status.found[0] = 1;
    status.found[1] = 31;
    status.found[2] = 44;
    status.found_count = 3;

    char buf[128];
    web_core_build_scan_json(buf, sizeof(buf), &status);
    TEST_ASSERT_EQUAL_STRING(
        "{\"type\":\"scan\",\"state\":\"running\",\"current_addr\":37,\"range_end\":247,\"found\":[1,31,44]}",
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

/* ── wind JSON ── */

void test_wind_json_no_data(void)
{
    wind_reading_t reading;
    memset(&reading, 0, sizeof(reading));

    char buf[256];
    web_core_build_wind_json(buf, sizeof(buf), &reading, false, 0);
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"wind\",\"has_data\":false}", buf);
}

void test_wind_json_with_data(void)
{
    /* Same example as scratchbook.md §7's Wind Test mockup. */
    wind_reading_t reading;
    reading.dir_instant_deg  = 183.4f;
    reading.dir_avg_deg      = 181.0f;
    reading.speed_instant_ms = 4.2f;
    reading.speed_avg_ms     = 3.9f;
    reading.raw_pulses       = 27;

    char buf[256];
    web_core_build_wind_json(buf, sizeof(buf), &reading, true, 420);
    TEST_ASSERT_EQUAL_STRING(
        "{\"type\":\"wind\",\"has_data\":true,\"dir_instant_deg\":183.4,\"dir_avg_deg\":181.0,"
        "\"speed_instant_ms\":4.2,\"speed_avg_ms\":3.9,\"raw_pulses\":27,\"age_ms\":420}",
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

    char buf[256];
    web_core_build_status_json(buf, sizeof(buf), "STA", "test-network", "192.168.20.185", -45,
                                true, "2026-07-01T17:45:21", 12345, &health);

    TEST_ASSERT_TRUE(strstr(buf, "\"wifi_mode\":\"STA\"") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "\"wifi_rssi\":-45") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "\"ntp_synced\":true") != NULL);
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

    char buf[256];
    web_core_build_status_json(buf, sizeof(buf), "AP", "", "192.168.4.1", 0,
                                false, "1970-01-01T00:00:00", 10, &health);
    TEST_ASSERT_TRUE(strstr(buf, "\"last_exception\":2") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "\"ntp_synced\":false") != NULL);
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
    RUN_TEST(test_wind_json_no_data);
    RUN_TEST(test_wind_json_with_data);
    RUN_TEST(test_status_json_no_exception);
    RUN_TEST(test_status_json_with_exception);
    return UNITY_END();
}
