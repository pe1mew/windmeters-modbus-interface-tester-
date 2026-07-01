/**
 * wifi_manager decision core — unit tests (native build)
 *
 * Covers completeRealisationPlan.md TASK-WIFI's 4 acceptance tests at the
 * decision level (the parts that are genuinely decidable without a real
 * radio) — test_wifi_mdns_resolves_in_sta_mode has no native equivalent,
 * it's inherently a real-network check.
 *
 * Run with:  pio test -e native -f test_wifi_manager
 */
#include <unity.h>
#include "wifi_manager.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

void test_wifi_starts_ap_without_credentials(void)
{
    /* "" and NULL both mean "never configured" */
    TEST_ASSERT_FALSE(wifi_manager_should_attempt_sta(""));
    TEST_ASSERT_FALSE(wifi_manager_should_attempt_sta(0));
}

void test_wifi_attempts_sta_with_stored_credentials(void)
{
    TEST_ASSERT_TRUE(wifi_manager_should_attempt_sta("MyHomeNetwork"));
}

void test_wifi_stops_ap_on_sta_success(void)
{
    TEST_ASSERT_FALSE(wifi_manager_should_keep_ap_after_connect(true));
}

void test_wifi_falls_back_to_ap_on_sta_failure(void)
{
    TEST_ASSERT_TRUE(wifi_manager_should_keep_ap_after_connect(false));
}

void test_ap_ssid_format(void)
{
    char out[32];
    wifi_manager_format_ap_ssid(out, sizeof(out), 0xAB, 0x07);
    TEST_ASSERT_EQUAL_STRING("WindmeterTester-AB07", out);
}

void test_ap_ssid_format_pads_single_digit_hex(void)
{
    char out[32];
    wifi_manager_format_ap_ssid(out, sizeof(out), 0x00, 0x0F);
    TEST_ASSERT_EQUAL_STRING("WindmeterTester-000F", out);
}

int main(int /*argc*/, char ** /*argv*/)
{
    UNITY_BEGIN();
    RUN_TEST(test_wifi_starts_ap_without_credentials);
    RUN_TEST(test_wifi_attempts_sta_with_stored_credentials);
    RUN_TEST(test_wifi_stops_ap_on_sta_success);
    RUN_TEST(test_wifi_falls_back_to_ap_on_sta_failure);
    RUN_TEST(test_ap_ssid_format);
    RUN_TEST(test_ap_ssid_format_pads_single_digit_hex);
    return UNITY_END();
}
