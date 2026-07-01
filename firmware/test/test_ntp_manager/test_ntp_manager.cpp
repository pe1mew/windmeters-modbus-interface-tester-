/**
 * ntp_manager decision/validation core — unit tests (native build)
 *
 * Covers completeRealisationPlan.md TASK-NTP's acceptance tests at the
 * decision/validation level: test_ntp_syncs_on_sta_connect becomes
 * "should_sync returns true at the right moments" (no real radio/RTC
 * needed to test the decision itself), test_ntp_manual_time_set_via_api
 * becomes the manual_time validator, and test_log_timestamps_use_synced_time
 * becomes the millis-to-epoch conversion.
 *
 * Run with:  pio test -e native -f test_ntp_manager
 */
#include <unity.h>
#include "ntp_manager.h"

void setUp(void)
{
    ntp_manager_reset();
}

void tearDown(void) {}

void test_should_sync_when_connected_and_not_yet_synced(void)
{
    TEST_ASSERT_TRUE(ntp_manager_should_sync(true, false));
}

void test_should_not_sync_when_not_connected(void)
{
    TEST_ASSERT_FALSE(ntp_manager_should_sync(false, false));
}

void test_should_not_sync_again_once_already_synced(void)
{
    TEST_ASSERT_FALSE(ntp_manager_should_sync(true, true));
}

void test_validate_manual_time_accepts_valid_date(void)
{
    manual_time_t t = {2026, 7, 1, 14, 30, 45};
    TEST_ASSERT_TRUE(ntp_manager_validate_manual_time(&t));
}

void test_validate_manual_time_rejects_invalid_month(void)
{
    manual_time_t t = {2026, 13, 1, 0, 0, 0};
    TEST_ASSERT_FALSE(ntp_manager_validate_manual_time(&t));

    manual_time_t t0 = {2026, 0, 1, 0, 0, 0};
    TEST_ASSERT_FALSE(ntp_manager_validate_manual_time(&t0));
}

void test_validate_manual_time_rejects_invalid_hour_minute_second(void)
{
    manual_time_t bad_hour = {2026, 1, 1, 24, 0, 0};
    TEST_ASSERT_FALSE(ntp_manager_validate_manual_time(&bad_hour));

    manual_time_t bad_min = {2026, 1, 1, 0, 60, 0};
    TEST_ASSERT_FALSE(ntp_manager_validate_manual_time(&bad_min));

    manual_time_t bad_sec = {2026, 1, 1, 0, 0, 60};
    TEST_ASSERT_FALSE(ntp_manager_validate_manual_time(&bad_sec));
}

void test_validate_manual_time_rejects_day_out_of_range_for_month(void)
{
    manual_time_t april_31 = {2026, 4, 31, 0, 0, 0}; /* April has 30 days */
    TEST_ASSERT_FALSE(ntp_manager_validate_manual_time(&april_31));

    manual_time_t april_30 = {2026, 4, 30, 0, 0, 0};
    TEST_ASSERT_TRUE(ntp_manager_validate_manual_time(&april_30));
}

void test_validate_manual_time_leap_year_feb29_accepted(void)
{
    manual_time_t feb29_2028 = {2028, 2, 29, 0, 0, 0}; /* 2028 is a leap year */
    TEST_ASSERT_TRUE(ntp_manager_validate_manual_time(&feb29_2028));
}

void test_validate_manual_time_non_leap_year_feb29_rejected(void)
{
    manual_time_t feb29_2026 = {2026, 2, 29, 0, 0, 0}; /* 2026 is not a leap year */
    TEST_ASSERT_FALSE(ntp_manager_validate_manual_time(&feb29_2026));

    manual_time_t feb29_2100 = {2100, 2, 29, 0, 0, 0}; /* divisible by 100, not 400 -> not leap */
    TEST_ASSERT_FALSE(ntp_manager_validate_manual_time(&feb29_2100));

    manual_time_t feb29_2000 = {2000, 2, 29, 0, 0, 0}; /* divisible by 400 -> leap */
    TEST_ASSERT_TRUE(ntp_manager_validate_manual_time(&feb29_2000));
}

void test_is_synced_reflects_record_sync(void)
{
    TEST_ASSERT_FALSE(ntp_manager_is_synced());
    ntp_manager_record_sync(1000, 1700000000);
    TEST_ASSERT_TRUE(ntp_manager_is_synced());
}

void test_millis_to_epoch_before_sync_returns_zero(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, ntp_manager_millis_to_epoch(5000));
}

void test_millis_to_epoch_after_sync_converts_correctly(void)
{
    /* Synced at uptime 10_000 ms == epoch 1_700_000_000 s.
     * A log entry at uptime 15_000 ms is 5 s later. */
    ntp_manager_record_sync(10000, 1700000000);
    TEST_ASSERT_EQUAL_UINT32(1700000005, ntp_manager_millis_to_epoch(15000));
}

int main(int /*argc*/, char ** /*argv*/)
{
    UNITY_BEGIN();
    RUN_TEST(test_should_sync_when_connected_and_not_yet_synced);
    RUN_TEST(test_should_not_sync_when_not_connected);
    RUN_TEST(test_should_not_sync_again_once_already_synced);
    RUN_TEST(test_validate_manual_time_accepts_valid_date);
    RUN_TEST(test_validate_manual_time_rejects_invalid_month);
    RUN_TEST(test_validate_manual_time_rejects_invalid_hour_minute_second);
    RUN_TEST(test_validate_manual_time_rejects_day_out_of_range_for_month);
    RUN_TEST(test_validate_manual_time_leap_year_feb29_accepted);
    RUN_TEST(test_validate_manual_time_non_leap_year_feb29_rejected);
    RUN_TEST(test_is_synced_reflects_record_sync);
    RUN_TEST(test_millis_to_epoch_before_sync_returns_zero);
    RUN_TEST(test_millis_to_epoch_after_sync_converts_correctly);
    return UNITY_END();
}
