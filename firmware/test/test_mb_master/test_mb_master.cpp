/**
 * mb_master request-processing core — unit tests (native build)
 *
 * Covers design/realisationPlan.md's traffic-log and bus-health
 * acceptance tests at the mb_master_process() level — no FreeRTOS
 * required, since that plumbing is a thin shim over this already-tested
 * core (see mb_master.h).
 *
 * Run with:  pio test -e native -f test_mb_master
 */
#include <unity.h>
#include "mb_master.h"
#include "mb_log.h"
#include "led_status.h"
#include "mock_transport.h"
#include "mock_led_backend.h"
#include <string.h>

/** Standard Modbus CRC16, independent of mb_crc16() under test elsewhere. */
static uint16_t ref_crc16(const uint8_t *buf, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)buf[i];
        for (uint8_t b = 0; b < 8; b++) {
            crc = (crc & 0x0001) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

static void ref_append_crc(uint8_t *buf, uint16_t len)
{
    uint16_t crc = ref_crc16(buf, len);
    buf[len]     = (uint8_t)(crc & 0xFF);
    buf[len + 1] = (uint8_t)(crc >> 8);
}

static uint16_t build_read_response(uint8_t *frame, uint8_t addr, uint8_t fc,
                                     const uint16_t *values, uint8_t count)
{
    frame[0] = addr;
    frame[1] = fc;
    frame[2] = (uint8_t)(count * 2);
    for (uint8_t i = 0; i < count; i++) {
        frame[3 + i * 2]     = (uint8_t)(values[i] >> 8);
        frame[3 + i * 2 + 1] = (uint8_t)(values[i] & 0xFF);
    }
    uint16_t payload_len = (uint16_t)(3 + count * 2);
    ref_append_crc(frame, payload_len);
    return (uint16_t)(payload_len + 2);
}

void setUp(void)
{
    mock_transport_reset();
    mb_init(&mock_transport, 200, 0); /* 0 retries: keep it deterministic, one attempt */
    mblog_init(8);
    led_init(&mock_led_backend);
    mock_led_reset(); /* discard the init-time colour set — see test_led_status.cpp for the same fix */
    mb_master_init();
}

void tearDown(void) {}

void test_process_read_success_logs_both_frames_and_pulses_valid(void)
{
    const uint16_t vals[2] = {0x00AA, 0x00BB};
    uint8_t frame[16];
    uint16_t len = build_read_response(frame, 31, 0x03, vals, 2);
    mock_transport_queue_response(frame, len);

    mb_request_t req = {31, 0x03, 0x0000, 2, {0}};
    mb_result_t result = mb_master_process(&req, 12345);

    TEST_ASSERT_EQUAL_INT(MB_OK, result.status);
    TEST_ASSERT_EQUAL_HEX16(0x00AA, result.registers[0]);
    TEST_ASSERT_EQUAL_HEX16(0x00BB, result.registers[1]);

    /* One TX entry, one RX entry. */
    TEST_ASSERT_EQUAL_UINT32(2, mblog_count());
    mb_log_entry_t out[2];
    TEST_ASSERT_EQUAL_UINT32(2, mblog_get_recent(out, 2));
    /* newest first: RX was logged after TX within the same call */
    TEST_ASSERT_FALSE(out[0].is_tx);
    TEST_ASSERT_TRUE(out[1].is_tx);
    TEST_ASSERT_EQUAL_UINT32(12345, out[0].timestamp_ms);

    /* led_pulse_valid(): one colour change to green, one delay, one back to base. */
    TEST_ASSERT_EQUAL_INT(2, mock_led_history_count());
    TEST_ASSERT_EQUAL_UINT32(1, mock_led_delay_call_count());

    const mb_bus_health_t *health = mb_master_get_health();
    TEST_ASSERT_EQUAL_UINT32(0, health->crc_errors);
    TEST_ASSERT_EQUAL_UINT32(0, health->timeouts);
}

void test_process_timeout_logs_only_tx_and_increments_counter(void)
{
    mock_transport_queue_timeout();

    mb_request_t req = {31, 0x03, 0x0000, 2, {0}};
    mb_result_t result = mb_master_process(&req, 1000);

    TEST_ASSERT_EQUAL_INT(MB_ERR_TIMEOUT, result.status);

    /* The request WAS transmitted (mb_core writes before waiting for a
     * reply) — only the (nonexistent) response is missing. Exactly one
     * log entry, not zero, not two. */
    TEST_ASSERT_EQUAL_UINT32(1, mblog_count());
    mb_log_entry_t out[1];
    mblog_get_recent(out, 1);
    TEST_ASSERT_TRUE(out[0].is_tx);

    TEST_ASSERT_EQUAL_INT(2, mock_led_history_count()); /* pulse_error: 2 colour changes */

    const mb_bus_health_t *health = mb_master_get_health();
    TEST_ASSERT_EQUAL_UINT32(1, health->timeouts);
    TEST_ASSERT_EQUAL_UINT32(0, health->crc_errors);
}

void test_process_crc_error_logs_both_frames_and_increments_counter(void)
{
    const uint16_t vals[2] = {0x1234, 0x5678};
    uint8_t frame[16];
    uint16_t len = build_read_response(frame, 31, 0x03, vals, 2);
    frame[len - 2] ^= 0xFF; /* corrupt CRC low byte — a response still arrives, just a bad one */
    mock_transport_queue_response(frame, len);

    mb_request_t req = {31, 0x03, 0x0000, 2, {0}};
    mb_result_t result = mb_master_process(&req, 2000);

    TEST_ASSERT_EQUAL_INT(MB_ERR_CRC, result.status);
    TEST_ASSERT_EQUAL_UINT32(2, mblog_count()); /* a (bad) response DID arrive — both logged */

    const mb_bus_health_t *health = mb_master_get_health();
    TEST_ASSERT_EQUAL_UINT32(1, health->crc_errors);
}

void test_process_exception_records_code_and_health(void)
{
    uint8_t frame[5];
    frame[0] = 31;
    frame[1] = 0x03 | 0x80;
    frame[2] = 0x02; /* Illegal Data Address */
    ref_append_crc(frame, 3);
    mock_transport_queue_response(frame, 5);

    mb_request_t req = {31, 0x03, 0x0000, 2, {0}};
    mb_result_t result = mb_master_process(&req, 3000);

    TEST_ASSERT_EQUAL_INT(MB_ERR_EXCEPTION, result.status);
    TEST_ASSERT_EQUAL_HEX8(0x02, result.exception_code);

    const mb_bus_health_t *health = mb_master_get_health();
    TEST_ASSERT_TRUE(health->has_exception);
    TEST_ASSERT_EQUAL_HEX8(0x02, health->last_exception);
}

void test_process_param_error_touches_neither_wire_nor_led(void)
{
    mb_request_t req = {31, 0x03, 0x0000, 0 /* count=0, rejected */, {0}};
    mb_result_t result = mb_master_process(&req, 4000);

    TEST_ASSERT_EQUAL_INT(MB_ERR_PARAM, result.status);
    TEST_ASSERT_EQUAL_UINT32(0, mblog_count());
    TEST_ASSERT_EQUAL_INT(0, mock_led_history_count());
    TEST_ASSERT_EQUAL_UINT32(0, mock_led_delay_call_count());

    const mb_bus_health_t *health = mb_master_get_health();
    TEST_ASSERT_EQUAL_UINT32(0, health->crc_errors);
    TEST_ASSERT_EQUAL_UINT32(0, health->timeouts);
}

void test_process_unsupported_fc_does_not_leak_a_prior_calls_frames(void)
{
    /* A prior, successful call leaves real bytes sitting in mb_core. */
    const uint16_t vals[2] = {0x1111, 0x2222};
    uint8_t frame[16];
    uint16_t len = build_read_response(frame, 31, 0x03, vals, 2);
    mock_transport_queue_response(frame, len);
    mb_request_t good_req = {31, 0x03, 0x0000, 2, {0}};
    mb_master_process(&good_req, 5000);
    TEST_ASSERT_EQUAL_UINT32(2, mblog_count());

    mblog_clear();
    mock_led_reset();

    /* An unsupported function code never reaches mb_core at all. */
    mb_request_t bad_req = {31, 0x99, 0x0000, 2, {0}};
    mb_result_t result = mb_master_process(&bad_req, 6000);

    TEST_ASSERT_EQUAL_INT(MB_ERR_PARAM, result.status);
    TEST_ASSERT_EQUAL_UINT32(0, mblog_count()); /* not the previous call's frames */
    TEST_ASSERT_EQUAL_INT(0, mock_led_history_count());
}

int main(int /*argc*/, char ** /*argv*/)
{
    UNITY_BEGIN();
    RUN_TEST(test_process_read_success_logs_both_frames_and_pulses_valid);
    RUN_TEST(test_process_timeout_logs_only_tx_and_increments_counter);
    RUN_TEST(test_process_crc_error_logs_both_frames_and_increments_counter);
    RUN_TEST(test_process_exception_records_code_and_health);
    RUN_TEST(test_process_param_error_touches_neither_wire_nor_led);
    RUN_TEST(test_process_unsupported_fc_does_not_leak_a_prior_calls_frames);
    return UNITY_END();
}
