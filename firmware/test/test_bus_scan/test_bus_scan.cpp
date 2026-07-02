/**
 * bus_scan decision core — unit tests (native build)
 *
 * Covers completeRealisationPlan.md TASK-SCAN's 5 acceptance tests at the
 * core level — no queue/task/UART required.
 *
 * Run with:  pio test -e native -f test_bus_scan
 */
#include <unity.h>
#include "bus_scan.h"

void setUp(void) {}
void tearDown(void) {}

void test_did_respond_true_for_ok(void)
{
    TEST_ASSERT_TRUE(bus_scan_did_respond(MB_OK));
}

void test_did_respond_true_for_exception(void)
{
    /* The core acceptance test: an exception still means a real device is there. */
    TEST_ASSERT_TRUE(bus_scan_did_respond(MB_ERR_EXCEPTION));
}

void test_did_respond_false_for_silence_or_noise(void)
{
    TEST_ASSERT_FALSE(bus_scan_did_respond(MB_ERR_TIMEOUT));
    TEST_ASSERT_FALSE(bus_scan_did_respond(MB_ERR_CRC));
    TEST_ASSERT_FALSE(bus_scan_did_respond(MB_ERR_FRAMING));
    TEST_ASSERT_FALSE(bus_scan_did_respond(MB_ERR_PARAM));
}

void test_scan_finds_known_address(void)
{
    bus_scan_start(1, 5, 1000);
    bus_scan_record_probe_result(false, 0x04, 0, 1010); /* addr 1: nothing */
    bus_scan_record_probe_result(false, 0x04, 0, 1020); /* addr 2: nothing */
    bus_scan_record_probe_result(true, 0x04, 36, 1030);  /* addr 3: found! */
    bus_scan_record_probe_result(false, 0x04, 0, 1040); /* addr 4: nothing */
    bus_scan_record_probe_result(false, 0x04, 0, 1050); /* addr 5: nothing */

    bus_scan_status_t status = bus_scan_get_status();
    TEST_ASSERT_EQUAL_INT(SCAN_COMPLETE, status.state);
    TEST_ASSERT_EQUAL_UINT8(1, status.found_count);
    TEST_ASSERT_EQUAL_UINT8(3, status.found[0]);
}

void test_scan_reports_progress_incrementally(void)
{
    bus_scan_start(10, 13, 0); /* 4 addresses: 10, 11, 12, 13 */
    TEST_ASSERT_EQUAL_UINT8(10, bus_scan_get_status().current_addr);

    bus_scan_record_probe_result(false, 0x04, 0, 10); /* probed 10 */
    TEST_ASSERT_EQUAL_UINT8(11, bus_scan_get_status().current_addr);

    bus_scan_record_probe_result(false, 0x04, 0, 20); /* probed 11 */
    TEST_ASSERT_EQUAL_UINT8(12, bus_scan_get_status().current_addr);

    bus_scan_record_probe_result(false, 0x04, 0, 30); /* probed 12 */
    TEST_ASSERT_EQUAL_UINT8(13, bus_scan_get_status().current_addr);
    TEST_ASSERT_TRUE(bus_scan_is_active()); /* 13 itself hasn't been probed yet */

    bus_scan_record_probe_result(false, 0x04, 0, 40); /* probed 13 -> last address, now complete */
    TEST_ASSERT_EQUAL_INT(SCAN_COMPLETE, bus_scan_get_status().state);
}

void test_scan_cancellable_mid_sweep(void)
{
    bus_scan_start(1, 100, 0);
    bus_scan_record_probe_result(false, 0x04, 0, 10);
    bus_scan_record_probe_result(false, 0x04, 0, 20);
    TEST_ASSERT_TRUE(bus_scan_is_active());

    bus_scan_cancel();
    TEST_ASSERT_FALSE(bus_scan_is_active());
    TEST_ASSERT_EQUAL_INT(SCAN_CANCELLED, bus_scan_get_status().state);

    uint8_t addr_at_cancel = bus_scan_get_status().current_addr;

    /* A stray result from a probe already in flight when cancel() was
     * called must not resume advancing the sweep. */
    bus_scan_record_probe_result(true, 0x04, 0, 30);
    TEST_ASSERT_EQUAL_UINT8(addr_at_cancel, bus_scan_get_status().current_addr);
    TEST_ASSERT_EQUAL_UINT8(0, bus_scan_get_status().found_count);
}

void test_scan_empty_bus_completes_cleanly(void)
{
    bus_scan_start(1, 5, 0);
    for (int i = 0; i < 5; i++) {
        bus_scan_record_probe_result(false, 0x04, 0, (uint32_t)(i + 1) * 10);
    }
    bus_scan_status_t status = bus_scan_get_status();
    TEST_ASSERT_EQUAL_INT(SCAN_COMPLETE, status.state);
    TEST_ASSERT_EQUAL_UINT8(0, status.found_count);
}

void test_scan_ignores_result_after_complete(void)
{
    bus_scan_start(1, 1, 0);
    bus_scan_record_probe_result(false, 0x04, 0, 10);
    TEST_ASSERT_EQUAL_INT(SCAN_COMPLETE, bus_scan_get_status().state);

    bus_scan_record_probe_result(true, 0x04, 0, 20); /* must be ignored — scan already finished */
    TEST_ASSERT_EQUAL_UINT8(0, bus_scan_get_status().found_count);
}

void test_scan_records_fc_and_round_trip_ms_per_found_device(void)
{
    bus_scan_start(1, 2, 0);
    bus_scan_record_probe_result(true, 0x04, 36, 10);  /* addr 1: found, 36ms */
    bus_scan_record_probe_result(true, 0x04, 41, 20);  /* addr 2: found, 41ms */

    bus_scan_status_t status = bus_scan_get_status();
    TEST_ASSERT_EQUAL_UINT8(2, status.found_count);
    TEST_ASSERT_EQUAL_UINT8(0x04, status.found_fc[0]);
    TEST_ASSERT_EQUAL_UINT32(36, status.found_round_trip_ms[0]);
    TEST_ASSERT_EQUAL_UINT8(0x04, status.found_fc[1]);
    TEST_ASSERT_EQUAL_UINT32(41, status.found_round_trip_ms[1]);
}

void test_scan_duration_ms_set_on_completion_not_before(void)
{
    bus_scan_start(1, 3, 1000);
    TEST_ASSERT_EQUAL_UINT32(0, bus_scan_get_status().duration_ms);

    bus_scan_record_probe_result(false, 0x04, 0, 1010);
    TEST_ASSERT_EQUAL_UINT32(0, bus_scan_get_status().duration_ms); /* still running */

    bus_scan_record_probe_result(false, 0x04, 0, 1020);
    bus_scan_record_probe_result(false, 0x04, 0, 1500); /* last address -> completes here */

    bus_scan_status_t status = bus_scan_get_status();
    TEST_ASSERT_EQUAL_INT(SCAN_COMPLETE, status.state);
    TEST_ASSERT_EQUAL_UINT32(500, status.duration_ms); /* 1500 - 1000 */
}

int main(int /*argc*/, char ** /*argv*/)
{
    UNITY_BEGIN();
    RUN_TEST(test_did_respond_true_for_ok);
    RUN_TEST(test_did_respond_true_for_exception);
    RUN_TEST(test_did_respond_false_for_silence_or_noise);
    RUN_TEST(test_scan_finds_known_address);
    RUN_TEST(test_scan_reports_progress_incrementally);
    RUN_TEST(test_scan_cancellable_mid_sweep);
    RUN_TEST(test_scan_empty_bus_completes_cleanly);
    RUN_TEST(test_scan_ignores_result_after_complete);
    RUN_TEST(test_scan_records_fc_and_round_trip_ms_per_found_device);
    RUN_TEST(test_scan_duration_ms_set_on_completion_not_before);
    return UNITY_END();
}
