/**
 * LIB-MB Modbus Master Core — unit tests (native build)
 *
 * Covers every test in design/realisationPlan.md §2 (frame-level, via
 * mb_frame.h directly) and §3 (transaction-level, via mb_core.h + the mock
 * transport).
 *
 * Run with:  pio test -e native
 *
 * CRC16/Modbus reference vectors (cross-checked against
 * greenhouse-Controller/drivers/modBus/test/test_modbus_rtu/test_modbus_rtu.cpp,
 * which verified them by running its own implementation):
 *   {0x01,0x03,0x00,0x00,0x00,0x02} -> 0x0BC4  (frame ends ...C4 0B, lo-byte-first)
 *   {0xFF}                          -> 0x00FF
 */

#include <unity.h>
#include "../../lib/mb_core/mb_frame.h"
#include "../../lib/mb_core/mb_core.h"
#include "mock_transport.h"

/* ---------------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------------- */

/**
 * Local CRC16/Modbus implementation, independent of mb_crc16() under test,
 * used to build expected/valid frames inside the tests — same approach as
 * the drivers/modBus precedent, to avoid a test validating itself.
 */
static uint16_t ref_crc16(const uint8_t *buf, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)buf[i];
        for (uint8_t b = 0; b < 8; b++) {
            if (crc & 0x0001) {
                crc = (uint16_t)((crc >> 1) ^ 0xA001);
            } else {
                crc = (uint16_t)(crc >> 1);
            }
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

/** Build a valid FC03/FC04 response frame and return its length. */
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

/** Build a Modbus exception response frame [addr][fc|0x80][code][crc_lo][crc_hi]. */
static uint16_t build_exception_response(uint8_t *frame, uint8_t addr, uint8_t fc,
                                          uint8_t exception_code)
{
    frame[0] = addr;
    frame[1] = (uint8_t)(fc | 0x80);
    frame[2] = exception_code;
    ref_append_crc(frame, 3);
    return 5;
}

/* ---------------------------------------------------------------------------
 * Unity fixtures
 * --------------------------------------------------------------------------- */
void setUp(void)
{
    mock_transport_reset();
    mb_init(&mock_transport, 200, 1); /* 200 ms timeout, 1 retry — project defaults */
}

void tearDown(void) {}

/* =========================================================================
 * Frame-level tests (mb_frame.h — no transport involved)
 * ========================================================================= */

void test_crc16_matches_reference(void)
{
    const uint8_t buf1[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x02};
    TEST_ASSERT_EQUAL_HEX16(0x0BC4, mb_crc16(buf1, sizeof(buf1)));

    const uint8_t buf2[] = {0xFF};
    TEST_ASSERT_EQUAL_HEX16(0x00FF, mb_crc16(buf2, sizeof(buf2)));
}

void test_build_request_fc03(void)
{
    uint8_t frame[8];
    uint16_t len = mb_build_read_request(frame, 0x01, 0x03, 0x0000, 0x0002);

    TEST_ASSERT_EQUAL_UINT16(8, len);
    TEST_ASSERT_EQUAL_HEX8(0x01, frame[0]);
    TEST_ASSERT_EQUAL_HEX8(0x03, frame[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, frame[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, frame[3]);
    TEST_ASSERT_EQUAL_HEX8(0x00, frame[4]);
    TEST_ASSERT_EQUAL_HEX8(0x02, frame[5]);
    /* CRC of the first 6 bytes is 0x0BC4 (verified above) -> lo=0xC4, hi=0x0B */
    TEST_ASSERT_EQUAL_HEX8(0xC4, frame[6]);
    TEST_ASSERT_EQUAL_HEX8(0x0B, frame[7]);
}

void test_build_request_fc04(void)
{
    uint8_t frame[8];
    mb_build_read_request(frame, 0x02, 0x04, 0x0000, 0x0002);
    TEST_ASSERT_EQUAL_HEX8(0x02, frame[0]);
    TEST_ASSERT_EQUAL_HEX8(0x04, frame[1]);
}

void test_build_request_fc06(void)
{
    uint8_t frame[8];
    uint16_t len = mb_build_write_single_request(frame, 0x01, 0x0002, 0x1234);

    TEST_ASSERT_EQUAL_UINT16(8, len);
    TEST_ASSERT_EQUAL_HEX8(0x01, frame[0]);
    TEST_ASSERT_EQUAL_HEX8(0x06, frame[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, frame[2]);
    TEST_ASSERT_EQUAL_HEX8(0x02, frame[3]);
    TEST_ASSERT_EQUAL_HEX8(0x12, frame[4]);
    TEST_ASSERT_EQUAL_HEX8(0x34, frame[5]);

    uint16_t expect_crc = ref_crc16(frame, 6);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(expect_crc & 0xFF), frame[6]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(expect_crc >> 8), frame[7]);
}

void test_build_request_fc16(void)
{
    uint16_t values[2] = {0x1234, 0x5678};
    uint8_t frame[13];
    uint16_t len = mb_build_write_multiple_request(frame, 0x01, 0x0000, 2, values);

    TEST_ASSERT_EQUAL_UINT16(13, len);
    TEST_ASSERT_EQUAL_HEX8(0x01, frame[0]);
    TEST_ASSERT_EQUAL_HEX8(0x10, frame[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, frame[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, frame[3]);
    TEST_ASSERT_EQUAL_HEX8(0x00, frame[4]);
    TEST_ASSERT_EQUAL_HEX8(0x02, frame[5]);
    TEST_ASSERT_EQUAL_HEX8(0x04, frame[6]);   /* byte count = 2 regs * 2 */
    TEST_ASSERT_EQUAL_HEX8(0x12, frame[7]);
    TEST_ASSERT_EQUAL_HEX8(0x34, frame[8]);
    TEST_ASSERT_EQUAL_HEX8(0x56, frame[9]);
    TEST_ASSERT_EQUAL_HEX8(0x78, frame[10]);

    uint16_t expect_crc = ref_crc16(frame, 11);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(expect_crc & 0xFF), frame[11]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(expect_crc >> 8), frame[12]);
}

void test_parse_response_valid_fc03_fc04(void)
{
    const uint16_t vals[2] = {0x1234, 0x5678};
    uint8_t frame[16];
    uint16_t len = build_read_response(frame, 0x01, 0x03, vals, 2);

    uint16_t out[2] = {0};
    uint8_t exc = 0;
    mb_frame_status_t fs = mb_parse_read_response(frame, len, 0x01, 0x03, 2, out, &exc);

    TEST_ASSERT_EQUAL_INT(MB_FRAME_OK, fs);
    TEST_ASSERT_EQUAL_HEX16(0x1234, out[0]);
    TEST_ASSERT_EQUAL_HEX16(0x5678, out[1]);

    /* Same check for FC04 */
    len = build_read_response(frame, 0x2C, 0x04, vals, 2);
    fs = mb_parse_read_response(frame, len, 0x2C, 0x04, 2, out, &exc);
    TEST_ASSERT_EQUAL_INT(MB_FRAME_OK, fs);
    TEST_ASSERT_EQUAL_HEX16(0x1234, out[0]);
    TEST_ASSERT_EQUAL_HEX16(0x5678, out[1]);
}

void test_parse_response_crc_error(void)
{
    const uint16_t vals[2] = {0x1234, 0x5678};
    uint8_t frame[16];
    uint16_t len = build_read_response(frame, 0x01, 0x03, vals, 2);
    frame[len - 2] ^= 0xFF; /* flip every bit in the CRC low byte */

    uint16_t out[2] = {0};
    uint8_t exc = 0;
    mb_frame_status_t fs = mb_parse_read_response(frame, len, 0x01, 0x03, 2, out, &exc);
    TEST_ASSERT_EQUAL_INT(MB_FRAME_ERR_CRC, fs);
}

void test_parse_response_exception(void)
{
    uint8_t frame[8];
    uint16_t len = build_exception_response(frame, 0x01, 0x03, 0x02 /* Illegal Data Address */);

    uint16_t out[2] = {0};
    uint8_t exc = 0;
    mb_frame_status_t fs = mb_parse_read_response(frame, len, 0x01, 0x03, 2, out, &exc);
    TEST_ASSERT_EQUAL_INT(MB_FRAME_ERR_EXCEPTION, fs);
    TEST_ASSERT_EQUAL_HEX8(0x02, exc);
}

void test_parse_response_wrong_function_code(void)
{
    const uint16_t vals[2] = {0x1234, 0x5678};
    uint8_t frame[16];
    /* Slave replies with FC04's function code to an FC03 request */
    uint16_t len = build_read_response(frame, 0x01, 0x04, vals, 2);

    uint16_t out[2] = {0};
    uint8_t exc = 0;
    mb_frame_status_t fs = mb_parse_read_response(frame, len, 0x01, 0x03, 2, out, &exc);
    TEST_ASSERT_EQUAL_INT(MB_FRAME_ERR_FRAMING, fs);
}

void test_parse_response_wrong_slave_echo(void)
{
    const uint16_t vals[2] = {0x1234, 0x5678};
    uint8_t frame[16];
    /* Frame is a well-formed reply from address 2, but we asked address 1 */
    uint16_t len = build_read_response(frame, 0x02, 0x03, vals, 2);

    uint16_t out[2] = {0};
    uint8_t exc = 0;
    mb_frame_status_t fs = mb_parse_read_response(frame, len, 0x01, 0x03, 2, out, &exc);
    TEST_ASSERT_EQUAL_INT(MB_FRAME_ERR_FRAMING, fs);
}

/* =========================================================================
 * Transaction-level tests (mb_core.h + mock_transport)
 * ========================================================================= */

void test_timeout_no_response(void)
{
    mb_init(&mock_transport, 200, 0); /* 0 retries: exactly one attempt */
    mock_transport_queue_timeout();

    uint16_t out[2] = {0};
    mb_status_t status = mb_read_holding_registers(1, 0, 2, out);
    TEST_ASSERT_EQUAL_INT(MB_ERR_TIMEOUT, status);
    TEST_ASSERT_EQUAL_INT(1, mock_transport_get_write_count());
}

void test_retry_recovers_from_one_timeout(void)
{
    mb_init(&mock_transport, 200, 1); /* 1 retry: up to two attempts */
    mock_transport_queue_timeout();

    const uint16_t vals[2] = {0x00AA, 0x00BB};
    uint8_t frame[16];
    uint16_t len = build_read_response(frame, 1, 0x03, vals, 2);
    mock_transport_queue_response(frame, len);

    uint16_t out[2] = {0};
    mb_status_t status = mb_read_holding_registers(1, 0, 2, out);

    TEST_ASSERT_EQUAL_INT(MB_OK, status);
    TEST_ASSERT_EQUAL_HEX16(0x00AA, out[0]);
    TEST_ASSERT_EQUAL_HEX16(0x00BB, out[1]);
    TEST_ASSERT_EQUAL_INT(2, mock_transport_get_write_count()); /* exactly one retry spent */
}

void test_retry_exhausted_returns_timeout(void)
{
    mb_init(&mock_transport, 200, 1); /* 1 retry: up to two attempts, both fail */
    mock_transport_queue_timeout();
    mock_transport_queue_timeout();

    uint16_t out[2] = {0};
    mb_status_t status = mb_read_holding_registers(1, 0, 2, out);
    TEST_ASSERT_EQUAL_INT(MB_ERR_TIMEOUT, status);
    TEST_ASSERT_EQUAL_INT(2, mock_transport_get_write_count());
}

void test_register_count_bounds(void)
{
    uint16_t out[126] = {0};

    TEST_ASSERT_EQUAL_INT(MB_ERR_PARAM, mb_read_holding_registers(1, 0, 0, out));
    TEST_ASSERT_EQUAL_INT(MB_ERR_PARAM, mb_read_holding_registers(1, 0, 126, out));
    TEST_ASSERT_EQUAL_INT(MB_ERR_PARAM, mb_read_input_registers(1, 0, 126, out));

    uint16_t values[124] = {0};
    TEST_ASSERT_EQUAL_INT(MB_ERR_PARAM, mb_write_multiple_registers(1, 0, 0, values));
    TEST_ASSERT_EQUAL_INT(MB_ERR_PARAM, mb_write_multiple_registers(1, 0, 124, values));
}

void test_broadcast_address_rejected(void)
{
    uint16_t out[2] = {0};
    uint16_t values[2] = {0};

    TEST_ASSERT_EQUAL_INT(MB_ERR_PARAM, mb_read_holding_registers(0, 0, 2, out));
    TEST_ASSERT_EQUAL_INT(MB_ERR_PARAM, mb_read_input_registers(0, 0, 2, out));
    TEST_ASSERT_EQUAL_INT(MB_ERR_PARAM, mb_write_single_register(0, 0, 1234));
    TEST_ASSERT_EQUAL_INT(MB_ERR_PARAM, mb_write_multiple_registers(0, 0, 2, values));
}

void test_mb_core_write_single_register_round_trip(void)
{
    /* FC06 response echoes the request exactly */
    uint8_t frame[8];
    uint16_t len = mb_build_write_single_request(frame, 1, 0x0002, 0x1234);
    mock_transport_queue_response(frame, len);

    mb_status_t status = mb_write_single_register(1, 0x0002, 0x1234);
    TEST_ASSERT_EQUAL_INT(MB_OK, status);

    uint8_t tx[8];
    mock_transport_get_transmitted(tx, sizeof(tx));
    TEST_ASSERT_EQUAL_HEX8(0x06, tx[1]);
    TEST_ASSERT_EQUAL_HEX8(0x12, tx[4]);
    TEST_ASSERT_EQUAL_HEX8(0x34, tx[5]);
}

void test_mb_core_write_multiple_registers_round_trip(void)
{
    uint16_t values[2] = {0x1234, 0x5678};

    /* FC16 response echoes address/start/count, not the data */
    uint8_t resp[8];
    resp[0] = 1;
    resp[1] = 0x10;
    resp[2] = 0x00; resp[3] = 0x00; /* start */
    resp[4] = 0x00; resp[5] = 0x02; /* count */
    ref_append_crc(resp, 6);

    mock_transport_queue_response(resp, 8);

    mb_status_t status = mb_write_multiple_registers(1, 0x0000, 2, values);
    TEST_ASSERT_EQUAL_INT(MB_OK, status);
}

void test_get_last_tx_rx_after_successful_call(void)
{
    const uint16_t vals[2] = {0x00AA, 0x00BB};
    uint8_t frame[16];
    uint16_t len = build_read_response(frame, 1, 0x03, vals, 2);
    mock_transport_queue_response(frame, len);

    uint16_t out[2] = {0};
    mb_read_holding_registers(1, 0, 2, out);

    uint8_t tx[16];
    uint16_t tx_len = mb_get_last_tx(tx, sizeof(tx));
    TEST_ASSERT_EQUAL_UINT16(8, tx_len); /* FC03 read request is always 8 bytes */
    TEST_ASSERT_EQUAL_HEX8(0x03, tx[1]);

    uint8_t rx[16];
    uint16_t rx_len = mb_get_last_rx(rx, sizeof(rx));
    TEST_ASSERT_EQUAL_UINT16(len, rx_len);
    TEST_ASSERT_EQUAL_HEX8(0x03, rx[1]);
}

void test_get_last_rx_is_empty_after_timeout(void)
{
    /* First, a successful call so there's something to wrongly "leak". */
    const uint16_t vals[2] = {0x1111, 0x2222};
    uint8_t frame[16];
    uint16_t len = build_read_response(frame, 1, 0x03, vals, 2);
    mock_transport_queue_response(frame, len);
    uint16_t out[2] = {0};
    mb_read_holding_registers(1, 0, 2, out);

    /* Now a call that times out entirely — rx must read back as empty. */
    mb_init(&mock_transport, 200, 0);
    mock_transport_queue_timeout();
    mb_read_holding_registers(1, 0, 2, out);

    uint8_t rx[16];
    TEST_ASSERT_EQUAL_UINT16(0, mb_get_last_rx(rx, sizeof(rx)));
}

void test_get_last_tx_rx_empty_after_param_rejection(void)
{
    /* A prior successful call... */
    const uint16_t vals[2] = {0x1111, 0x2222};
    uint8_t frame[16];
    uint16_t len = build_read_response(frame, 1, 0x03, vals, 2);
    mock_transport_queue_response(frame, len);
    uint16_t out[2] = {0};
    mb_read_holding_registers(1, 0, 2, out);

    /* ...followed by one PARAM-rejected call (addr=0) must not report the
     * previous call's bytes as if they belonged to this one. */
    mb_status_t status = mb_read_holding_registers(0, 0, 2, out);
    TEST_ASSERT_EQUAL_INT(MB_ERR_PARAM, status);

    uint8_t tx[16], rx[16];
    TEST_ASSERT_EQUAL_UINT16(0, mb_get_last_tx(tx, sizeof(tx)));
    TEST_ASSERT_EQUAL_UINT16(0, mb_get_last_rx(rx, sizeof(rx)));
}

void test_attempts_is_one_on_first_try_success(void)
{
    const uint16_t vals[2] = {0x00AA, 0x00BB};
    uint8_t frame[16];
    uint16_t len = build_read_response(frame, 1, 0x03, vals, 2);
    mock_transport_queue_response(frame, len);

    uint16_t out[2] = {0};
    mb_read_holding_registers(1, 0, 2, out);
    TEST_ASSERT_EQUAL_UINT8(1, mb_get_last_attempts());
}

void test_attempts_reflects_one_retry_consumed(void)
{
    mb_init(&mock_transport, 200, 1); /* 1 retry: up to two attempts */
    mock_transport_queue_timeout();

    const uint16_t vals[2] = {0x00AA, 0x00BB};
    uint8_t frame[16];
    uint16_t len = build_read_response(frame, 1, 0x03, vals, 2);
    mock_transport_queue_response(frame, len);

    uint16_t out[2] = {0};
    mb_read_holding_registers(1, 0, 2, out);
    TEST_ASSERT_EQUAL_UINT8(2, mb_get_last_attempts());
}

void test_attempts_after_retries_exhausted_equals_retries_plus_one(void)
{
    mb_init(&mock_transport, 200, 1);
    mock_transport_queue_timeout();
    mock_transport_queue_timeout();

    uint16_t out[2] = {0};
    mb_read_holding_registers(1, 0, 2, out);
    TEST_ASSERT_EQUAL_UINT8(2, mb_get_last_attempts());
}

void test_attempts_zero_after_param_rejection(void)
{
    /* A prior successful call leaves a nonzero attempts count sitting there. */
    const uint16_t vals[2] = {0x1111, 0x2222};
    uint8_t frame[16];
    uint16_t len = build_read_response(frame, 1, 0x03, vals, 2);
    mock_transport_queue_response(frame, len);
    uint16_t out[2] = {0};
    mb_read_holding_registers(1, 0, 2, out);
    TEST_ASSERT_EQUAL_UINT8(1, mb_get_last_attempts());

    /* PARAM-rejected call (addr=0) must not report the previous call's count. */
    mb_status_t status = mb_read_holding_registers(0, 0, 2, out);
    TEST_ASSERT_EQUAL_INT(MB_ERR_PARAM, status);
    TEST_ASSERT_EQUAL_UINT8(0, mb_get_last_attempts());
}

/* =========================================================================
 * Pre-TX RX flush (shared-bus stale-traffic guard)
 * ========================================================================= */

void test_flush_runs_before_write_and_reports_discarded_bytes(void)
{
    /* 30 bytes of another master's traffic are sitting in the RX buffer when
     * we go to transmit — the exact shared-bus scenario from the bench. */
    mock_transport_stage_stale_bytes(30);

    const uint16_t vals[2] = {0x00AA, 0x00BB};
    uint8_t frame[16];
    uint16_t len = build_read_response(frame, 1, 0x03, vals, 2);
    mock_transport_queue_response(frame, len);

    uint16_t out[2] = {0};
    mb_status_t status = mb_read_holding_registers(1, 0, 2, out);

    /* The transaction still succeeds — the stale bytes were dropped, not read
     * ahead of the real response. */
    TEST_ASSERT_EQUAL_INT(MB_OK, status);
    TEST_ASSERT_EQUAL_HEX16(0x00AA, out[0]);
    /* Flush happened, before the write, and its count was reported. */
    TEST_ASSERT_EQUAL_INT(0, mock_transport_write_saw_unflushed_stale());
    TEST_ASSERT_EQUAL_INT(1, mock_transport_get_flush_count());
    TEST_ASSERT_EQUAL_UINT16(30, mb_get_last_discarded());
}

void test_clean_bus_reports_zero_discarded(void)
{
    /* Nothing staged: a clean bus discards nothing but still flushes once. */
    const uint16_t vals[2] = {0x00AA, 0x00BB};
    uint8_t frame[16];
    uint16_t len = build_read_response(frame, 1, 0x03, vals, 2);
    mock_transport_queue_response(frame, len);

    uint16_t out[2] = {0};
    mb_read_holding_registers(1, 0, 2, out);

    TEST_ASSERT_EQUAL_UINT16(0, mb_get_last_discarded());
    TEST_ASSERT_EQUAL_INT(1, mock_transport_get_flush_count());
}

void test_flush_runs_once_per_write_attempt(void)
{
    /* One retry, first attempt times out — both attempts must flush before
     * their write, since junk can re-accumulate between the two. */
    mb_init(&mock_transport, 200, 1);
    mock_transport_queue_timeout();

    const uint16_t vals[2] = {0x00AA, 0x00BB};
    uint8_t frame[16];
    uint16_t len = build_read_response(frame, 1, 0x03, vals, 2);
    mock_transport_queue_response(frame, len);

    uint16_t out[2] = {0};
    mb_status_t status = mb_read_holding_registers(1, 0, 2, out);

    TEST_ASSERT_EQUAL_INT(MB_OK, status);
    TEST_ASSERT_EQUAL_INT(2, mock_transport_get_write_count());
    TEST_ASSERT_EQUAL_INT(2, mock_transport_get_flush_count()); /* one flush per attempt */
}

void test_discarded_is_zero_after_param_rejection(void)
{
    /* Stale bytes are staged, but a PARAM-rejected call never reaches the
     * transport — it must neither flush nor report a discard count. */
    mock_transport_stage_stale_bytes(12);

    uint16_t out[2] = {0};
    mb_status_t status = mb_read_holding_registers(0 /* broadcast, rejected */, 0, 2, out);

    TEST_ASSERT_EQUAL_INT(MB_ERR_PARAM, status);
    TEST_ASSERT_EQUAL_UINT16(0, mb_get_last_discarded());
    TEST_ASSERT_EQUAL_INT(0, mock_transport_get_flush_count());
}

/* ---------------------------------------------------------------------------
 * Entry point
 * --------------------------------------------------------------------------- */
int main(int /*argc*/, char ** /*argv*/)
{
    UNITY_BEGIN();

    RUN_TEST(test_crc16_matches_reference);
    RUN_TEST(test_build_request_fc03);
    RUN_TEST(test_build_request_fc04);
    RUN_TEST(test_build_request_fc06);
    RUN_TEST(test_build_request_fc16);
    RUN_TEST(test_parse_response_valid_fc03_fc04);
    RUN_TEST(test_parse_response_crc_error);
    RUN_TEST(test_parse_response_exception);
    RUN_TEST(test_parse_response_wrong_function_code);
    RUN_TEST(test_parse_response_wrong_slave_echo);
    RUN_TEST(test_timeout_no_response);
    RUN_TEST(test_retry_recovers_from_one_timeout);
    RUN_TEST(test_retry_exhausted_returns_timeout);
    RUN_TEST(test_register_count_bounds);
    RUN_TEST(test_broadcast_address_rejected);
    RUN_TEST(test_mb_core_write_single_register_round_trip);
    RUN_TEST(test_mb_core_write_multiple_registers_round_trip);
    RUN_TEST(test_get_last_tx_rx_after_successful_call);
    RUN_TEST(test_get_last_rx_is_empty_after_timeout);
    RUN_TEST(test_get_last_tx_rx_empty_after_param_rejection);
    RUN_TEST(test_attempts_is_one_on_first_try_success);
    RUN_TEST(test_attempts_reflects_one_retry_consumed);
    RUN_TEST(test_attempts_after_retries_exhausted_equals_retries_plus_one);
    RUN_TEST(test_attempts_zero_after_param_rejection);
    RUN_TEST(test_flush_runs_before_write_and_reports_discarded_bytes);
    RUN_TEST(test_clean_bus_reports_zero_discarded);
    RUN_TEST(test_flush_runs_once_per_write_attempt);
    RUN_TEST(test_discarded_is_zero_after_param_rejection);

    return UNITY_END();
}
