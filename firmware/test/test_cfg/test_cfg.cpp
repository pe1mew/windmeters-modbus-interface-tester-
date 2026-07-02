/**
 * LIB-NVS Configuration Persistence — unit tests (native build)
 *
 * Run with:  pio test -e native -f test_cfg
 */
#include <unity.h>
#include "cfg.h"
#include "cfg_keys.h"
#include "mock_cfg_backend.h"
#include <string.h>

void setUp(void)
{
    mock_cfg_reset();
    cfg_init(&mock_cfg_backend);
}

void tearDown(void) {}

void test_cfg_defaults_on_first_boot(void)
{
    /* Sample one key of each type from the real project's key list
     * (cfg_keys.h) rather than made-up test keys, so this also catches a
     * key/default drifting out of sync with itself. */
    TEST_ASSERT_EQUAL_UINT32(CFG_DEFAULT_MB_BAUD, cfg_get_u32(CFG_KEY_MB_BAUD, CFG_DEFAULT_MB_BAUD));
    TEST_ASSERT_EQUAL_UINT16(CFG_DEFAULT_MB_TIMEOUT_MS, cfg_get_u16(CFG_KEY_MB_TIMEOUT_MS, CFG_DEFAULT_MB_TIMEOUT_MS));
    TEST_ASSERT_EQUAL_UINT8(CFG_DEFAULT_MB_RETRIES, cfg_get_u8(CFG_KEY_MB_RETRIES, CFG_DEFAULT_MB_RETRIES));

    char out[32];
    cfg_get_str(CFG_KEY_NTP_SERVER, out, sizeof(out), CFG_DEFAULT_NTP_SERVER);
    TEST_ASSERT_EQUAL_STRING(CFG_DEFAULT_NTP_SERVER, out);
}

void test_cfg_round_trip_u32(void)
{
    cfg_set_u32(CFG_KEY_WIND_POLL_INTERVAL, 2500);
    TEST_ASSERT_EQUAL_UINT32(2500, cfg_get_u32(CFG_KEY_WIND_POLL_INTERVAL, CFG_DEFAULT_WIND_POLL_INTERVAL));
}

void test_cfg_round_trip_u16(void)
{
    cfg_set_u16(CFG_KEY_MB_TIMEOUT_MS, 350);
    TEST_ASSERT_EQUAL_UINT16(350, cfg_get_u16(CFG_KEY_MB_TIMEOUT_MS, CFG_DEFAULT_MB_TIMEOUT_MS));
}

void test_cfg_round_trip_u8(void)
{
    cfg_set_u8(CFG_KEY_WIND_SPEED_ADDR, 44);
    TEST_ASSERT_EQUAL_UINT8(44, cfg_get_u8(CFG_KEY_WIND_SPEED_ADDR, CFG_DEFAULT_WIND_SPEED_ADDR));
}

void test_cfg_round_trip_str(void)
{
    cfg_set_str(CFG_KEY_WIFI_SSID, "MyNetwork");
    char out[32];
    cfg_get_str(CFG_KEY_WIFI_SSID, out, sizeof(out), CFG_DEFAULT_WIFI_SSID);
    TEST_ASSERT_EQUAL_STRING("MyNetwork", out);
}

void test_cfg_get_str_truncates_to_buffer(void)
{
    cfg_set_str(CFG_KEY_WIFI_SSID, "ThisIsALongerNetworkNameThanTheBuffer");
    char out[8]; /* room for 7 chars + null */
    cfg_get_str(CFG_KEY_WIFI_SSID, out, sizeof(out), "");
    TEST_ASSERT_EQUAL_INT(7, (int)strlen(out));
    TEST_ASSERT_EQUAL_STRING("ThisIsA", out);
}

void test_cfg_reset_restores_defaults(void)
{
    cfg_set_u32(CFG_KEY_MB_BAUD, 19200);
    cfg_set_str(CFG_KEY_WIFI_SSID, "SomeNetwork");
    TEST_ASSERT_EQUAL_UINT32(19200, cfg_get_u32(CFG_KEY_MB_BAUD, CFG_DEFAULT_MB_BAUD));

    cfg_reset_defaults();

    TEST_ASSERT_EQUAL_UINT32(CFG_DEFAULT_MB_BAUD, cfg_get_u32(CFG_KEY_MB_BAUD, CFG_DEFAULT_MB_BAUD));
    char out[32];
    cfg_get_str(CFG_KEY_WIFI_SSID, out, sizeof(out), CFG_DEFAULT_WIFI_SSID);
    TEST_ASSERT_EQUAL_STRING(CFG_DEFAULT_WIFI_SSID, out);
}

void test_cfg_persists_across_reinit(void)
{
    /* Simulates a reboot: cfg_init() is called again against the SAME
     * backing store (the mock's static table isn't wiped), the way
     * Preferences.begin() re-opens the same flash-backed NVS namespace. */
    cfg_set_u8(CFG_KEY_SCAN_RANGE_START, 5);

    cfg_init(&mock_cfg_backend); /* re-init, no mock_cfg_reset() in between */

    TEST_ASSERT_EQUAL_UINT8(5, cfg_get_u8(CFG_KEY_SCAN_RANGE_START, CFG_DEFAULT_SCAN_RANGE_START));
}

void test_all_keys_fit_the_15_char_preferences_limit(void)
{
    /* mock_cfg_backend (every other test in this file) is an in-memory map
     * with no key-length constraint of its own, so it can't catch this —
     * only a real Preferences backend enforces it, and it does so by
     * silently failing the write rather than erroring (cfg_keys.h's own
     * comment). This test is the only thing standing between a too-long
     * key and a setting that quietly never persists on real hardware.
     * Found the hard way once already: CFG_KEY_SCAN_RANGE_START was 16
     * chars, CFG_KEY_WIND_POLL_INTERVAL was 21 — both passed every
     * mock-backed round-trip test above while silently not persisting on
     * the actual device. See memory/gotcha-log.md. */
    const char *keys[] = {
        CFG_KEY_WIFI_SSID, CFG_KEY_WIFI_PASS, CFG_KEY_NTP_SERVER,
        CFG_KEY_MB_BAUD, CFG_KEY_MB_TIMEOUT_MS, CFG_KEY_MB_RETRIES,
        CFG_KEY_SCAN_RANGE_START, CFG_KEY_SCAN_RANGE_END,
        CFG_KEY_WIND_SPEED_ADDR, CFG_KEY_WIND_DIR_ADDR, CFG_KEY_WIND_POLL_INTERVAL,
    };
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        TEST_ASSERT_TRUE_MESSAGE(strlen(keys[i]) <= 15, keys[i]);
    }
}

int main(int /*argc*/, char ** /*argv*/)
{
    UNITY_BEGIN();
    RUN_TEST(test_cfg_defaults_on_first_boot);
    RUN_TEST(test_cfg_round_trip_u32);
    RUN_TEST(test_cfg_round_trip_u16);
    RUN_TEST(test_cfg_round_trip_u8);
    RUN_TEST(test_cfg_round_trip_str);
    RUN_TEST(test_cfg_get_str_truncates_to_buffer);
    RUN_TEST(test_cfg_reset_restores_defaults);
    RUN_TEST(test_cfg_persists_across_reinit);
    RUN_TEST(test_all_keys_fit_the_15_char_preferences_limit);
    return UNITY_END();
}
