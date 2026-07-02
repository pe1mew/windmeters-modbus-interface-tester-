#include "mb_core.h"
#include "mb_frame.h"
#include <string.h>

static const mb_transport_t *s_transport   = 0;
static uint16_t               s_timeout_ms = 200;
static uint8_t                s_retries    = 1;
static uint8_t                s_last_exception = 0;

static uint8_t  s_last_tx[MB_MAX_FRAME_LEN];
static uint16_t s_last_tx_len = 0;
static uint8_t  s_last_rx[MB_MAX_FRAME_LEN];
static uint16_t s_last_rx_len = 0;
static uint8_t  s_last_attempts = 0;

void mb_init(const mb_transport_t *transport, uint16_t timeout_ms, uint8_t retries)
{
    s_transport  = transport;
    s_timeout_ms = timeout_ms;
    s_retries    = retries;
}

void mb_set_timeout(uint16_t timeout_ms) { s_timeout_ms = timeout_ms; }
void mb_set_retries(uint8_t retries)     { s_retries = retries; }
uint8_t mb_last_exception_code(void)     { return s_last_exception; }

uint16_t mb_get_last_tx(uint8_t *buf, uint16_t max_len)
{
    uint16_t n = (s_last_tx_len < max_len) ? s_last_tx_len : max_len;
    memcpy(buf, s_last_tx, n);
    return n;
}

uint16_t mb_get_last_rx(uint8_t *buf, uint16_t max_len)
{
    uint16_t n = (s_last_rx_len < max_len) ? s_last_rx_len : max_len;
    memcpy(buf, s_last_rx, n);
    return n;
}

uint8_t mb_get_last_attempts(void)
{
    return s_last_attempts;
}

static mb_status_t frame_status_to_mb_status(mb_frame_status_t fs)
{
    switch (fs) {
        case MB_FRAME_OK:            return MB_OK;
        case MB_FRAME_ERR_CRC:       return MB_ERR_CRC;
        case MB_FRAME_ERR_EXCEPTION: return MB_ERR_EXCEPTION;
        case MB_FRAME_ERR_FRAMING:   return MB_ERR_FRAMING;
        default:                     return MB_ERR_FRAMING;
    }
}

/**
 * @brief Send @p req and wait for a response, retrying on timeout only.
 *
 * A malformed-but-present response (bad CRC, wrong echo, exception) is NOT
 * retried — only "nothing came back at all" is. This matches
 * design/realisationPlan.md's test_retry_recovers_from_one_timeout: retry
 * exists for a slave that occasionally misses a request, not to paper over
 * a persistently wrong response.
 *
 * @param[out] resp_len Set to the number of bytes received on success.
 * @return MB_OK if *something* was received (caller still has to parse it),
 *         MB_ERR_TIMEOUT if every attempt (1 + retries) got nothing back.
 */
static mb_status_t do_transaction(const uint8_t *req, uint16_t req_len,
                                   uint8_t *resp, uint16_t *resp_len)
{
    uint16_t tx_copy_len = (req_len < MB_MAX_FRAME_LEN) ? req_len : MB_MAX_FRAME_LEN;
    memcpy(s_last_tx, req, tx_copy_len);
    s_last_tx_len = tx_copy_len;
    s_last_rx_len = 0; /* cleared up front so a timeout doesn't leak a prior call's RX bytes */

    for (uint8_t attempt = 0; attempt <= s_retries; attempt++) {
        s_transport->write(s_transport->ctx, req, req_len);
        uint16_t n = s_transport->read(s_transport->ctx, resp, MB_MAX_FRAME_LEN, s_timeout_ms);
        if (n > 0) {
            *resp_len = n;
            uint16_t rx_copy_len = (n < MB_MAX_FRAME_LEN) ? n : MB_MAX_FRAME_LEN;
            memcpy(s_last_rx, resp, rx_copy_len);
            s_last_rx_len = rx_copy_len;
            s_last_attempts = (uint8_t)(attempt + 1);
            return MB_OK;
        }
        /* n == 0: no response this attempt — loop retries if any left. */
    }
    s_last_attempts = (uint8_t)(s_retries + 1);
    return MB_ERR_TIMEOUT;
}

static mb_status_t read_registers(uint8_t addr, uint8_t fc, uint16_t start,
                                   uint8_t count, uint16_t *out)
{
    /* Cleared here, not just in do_transaction(), so a PARAM-rejected call
     * (which never reaches do_transaction) doesn't leave a prior call's
     * bytes looking like they belong to this one — mb_master_process()
     * uses tx_len > 0 to decide whether a transaction touched the wire. */
    s_last_tx_len = 0;
    s_last_rx_len = 0;
    s_last_attempts = 0;

    if (addr == 0 || out == 0)                    return MB_ERR_PARAM;
    if (count == 0 || count > MB_MAX_READ_REGISTERS) return MB_ERR_PARAM;

    uint8_t req[MB_MAX_FRAME_LEN];
    uint16_t req_len = mb_build_read_request(req, addr, fc, start, count);

    uint8_t resp[MB_MAX_FRAME_LEN];
    uint16_t resp_len = 0;
    mb_status_t status = do_transaction(req, req_len, resp, &resp_len);
    if (status != MB_OK) {
        return status;
    }

    uint8_t exc = 0;
    mb_frame_status_t fs = mb_parse_read_response(resp, resp_len, addr, fc, count, out, &exc);
    if (fs == MB_FRAME_ERR_EXCEPTION) {
        s_last_exception = exc;
    }
    return frame_status_to_mb_status(fs);
}

mb_status_t mb_read_holding_registers(uint8_t addr, uint16_t start, uint8_t count, uint16_t *out)
{
    return read_registers(addr, 0x03, start, count, out);
}

mb_status_t mb_read_input_registers(uint8_t addr, uint16_t start, uint8_t count, uint16_t *out)
{
    return read_registers(addr, 0x04, start, count, out);
}

mb_status_t mb_write_single_register(uint8_t addr, uint16_t reg, uint16_t value)
{
    s_last_tx_len = 0;
    s_last_rx_len = 0;
    s_last_attempts = 0;
    if (addr == 0) return MB_ERR_PARAM;

    uint8_t req[MB_MAX_FRAME_LEN];
    uint16_t req_len = mb_build_write_single_request(req, addr, reg, value);

    uint8_t resp[MB_MAX_FRAME_LEN];
    uint16_t resp_len = 0;
    mb_status_t status = do_transaction(req, req_len, resp, &resp_len);
    if (status != MB_OK) {
        return status;
    }

    uint8_t exc = 0;
    mb_frame_status_t fs = mb_parse_write_single_response(resp, resp_len, addr, reg, value, &exc);
    if (fs == MB_FRAME_ERR_EXCEPTION) {
        s_last_exception = exc;
    }
    return frame_status_to_mb_status(fs);
}

mb_status_t mb_write_multiple_registers(uint8_t addr, uint16_t start, uint8_t count,
                                         const uint16_t *values)
{
    s_last_tx_len = 0;
    s_last_rx_len = 0;
    s_last_attempts = 0;
    if (addr == 0 || values == 0)                    return MB_ERR_PARAM;
    if (count == 0 || count > MB_MAX_WRITE_REGISTERS) return MB_ERR_PARAM;

    uint8_t req[MB_MAX_FRAME_LEN];
    uint16_t req_len = mb_build_write_multiple_request(req, addr, start, count, values);

    uint8_t resp[MB_MAX_FRAME_LEN];
    uint16_t resp_len = 0;
    mb_status_t status = do_transaction(req, req_len, resp, &resp_len);
    if (status != MB_OK) {
        return status;
    }

    uint8_t exc = 0;
    mb_frame_status_t fs = mb_parse_write_multiple_response(resp, resp_len, addr, start, count, &exc);
    if (fs == MB_FRAME_ERR_EXCEPTION) {
        s_last_exception = exc;
    }
    return frame_status_to_mb_status(fs);
}
