#include "mb_master.h"
#include "mb_frame.h" /* MB_MAX_FRAME_LEN */
#include "mb_log.h"
#include "led_status.h"
#include <string.h>
#include <stdio.h>

static mb_bus_health_t s_health;

void mb_master_init(void)
{
    memset(&s_health, 0, sizeof(s_health));
}

const mb_bus_health_t *mb_master_get_health(void)
{
    return &s_health;
}

static const char *status_name(mb_status_t status)
{
    switch (status) {
        case MB_OK:            return "OK";
        case MB_ERR_TIMEOUT:   return "TIMEOUT";
        case MB_ERR_CRC:       return "CRC_ERR";
        case MB_ERR_EXCEPTION: return "EXCEPTION";
        case MB_ERR_FRAMING:   return "FRAMING_ERR";
        case MB_ERR_PARAM:     return "PARAM_ERR";
        default:               return "?";
    }
}

static void log_frame(bool is_tx, const uint8_t *raw, uint16_t raw_len,
                       const char *summary, uint32_t timestamp_ms)
{
    if (raw_len == 0) {
        return; /* nothing was sent (PARAM rejection) or nothing came back (timeout) */
    }
    mb_log_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.timestamp_ms = timestamp_ms;
    entry.is_tx = is_tx;
    entry.raw_len = (uint8_t)((raw_len < sizeof(entry.raw)) ? raw_len : sizeof(entry.raw));
    memcpy(entry.raw, raw, entry.raw_len);
    strncpy(entry.summary, summary, sizeof(entry.summary) - 1);
    mblog_append(&entry);
}

mb_result_t mb_master_process(const mb_request_t *req, uint32_t timestamp_ms)
{
    mb_result_t result;
    memset(&result, 0, sizeof(result));
    bool dispatched = true; /* false for an fc mb_core never saw at all — see below */

    switch (req->fc) {
        case 0x03:
            result.status = mb_read_holding_registers(req->addr, req->start, req->count, result.registers);
            break;
        case 0x04:
            result.status = mb_read_input_registers(req->addr, req->start, req->count, result.registers);
            break;
        case 0x06:
            result.status = mb_write_single_register(req->addr, req->start, req->values[0]);
            break;
        case 0x10:
            result.status = mb_write_multiple_registers(req->addr, req->start, req->count, req->values);
            break;
        default:
            result.status = MB_ERR_PARAM;
            dispatched = false;
            break;
    }

    if (result.status == MB_ERR_EXCEPTION) {
        result.exception_code = mb_last_exception_code();
    }

    char summary[64];
    snprintf(summary, sizeof(summary), "FC%02X addr%u start0x%04X cnt%u -> %s",
              req->fc, req->addr, req->start, req->count, status_name(result.status));

    /* Only ask mb_core for its last tx/rx when this request actually
     * reached it — otherwise mb_get_last_tx/rx() would return whatever a
     * PREVIOUS, unrelated call left behind, and this rejected request
     * would get logged with someone else's bytes. */
    uint8_t  tx[MB_MAX_FRAME_LEN] = {0};
    uint8_t  rx[MB_MAX_FRAME_LEN] = {0};
    uint16_t tx_len = dispatched ? mb_get_last_tx(tx, sizeof(tx)) : 0;
    uint16_t rx_len = dispatched ? mb_get_last_rx(rx, sizeof(rx)) : 0;

    log_frame(true, tx, tx_len, summary, timestamp_ms);
    log_frame(false, rx, rx_len, summary, timestamp_ms);

    /* Copied out now, while still fresh, rather than leaving callers to call
     * mb_get_last_tx/_rx/_attempts() themselves later — those are
     * single-scratch-value accessors that the *next* queued transaction can
     * silently overwrite before a caller on the far side of a FreeRTOS queue
     * hop gets scheduled (design/api.md §4.4). */
    result.raw_tx_len = tx_len;
    memcpy(result.raw_tx, tx, tx_len);
    result.raw_rx_len = rx_len;
    memcpy(result.raw_rx, rx, rx_len);
    result.attempts = dispatched ? mb_get_last_attempts() : 0;

    switch (result.status) {
        case MB_OK:
            led_pulse_valid();
            break;
        case MB_ERR_CRC:
            s_health.crc_errors++;
            led_pulse_error();
            break;
        case MB_ERR_TIMEOUT:
            s_health.timeouts++;
            led_pulse_error();
            break;
        case MB_ERR_FRAMING:
            s_health.framing_errors++;
            led_pulse_error();
            break;
        case MB_ERR_EXCEPTION:
            s_health.has_exception = true;
            s_health.last_exception = result.exception_code;
            led_pulse_error();
            break;
        case MB_ERR_PARAM:
            /* caller bug (bad request) — never touched the wire, no LED/counter change */
            break;
    }

    return result;
}
