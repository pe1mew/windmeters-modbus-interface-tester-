/**
 * @file mb_master.cpp
 * @brief mb_master_process() — dispatch one request to mb_core, log it, drive the LED, tally health.
 *
 * The one non-obvious contract this file enforces: a request is only
 * considered to have "touched the wire" if its fc was one mb_core actually
 * dispatched (see `dispatched` in mb_master_process()). An unrecognised fc
 * is rejected as MB_ERR_PARAM without calling into mb_core at all, so the
 * mb_get_last_tx/_rx/_attempts() single-scratch-value accessors would still
 * hold whatever a *previous, unrelated* call left behind — reading them
 * anyway would silently mislabel that leftover data as belonging to this
 * request. `dispatched` gates those reads so a rejected request is logged
 * (and returned) with zero-length raw frames instead.
 */
#include "mb_master.h"
#include "mb_frame.h" /* MB_MAX_FRAME_LEN */
#include "mb_log.h"
#include "led_status.h"
#include <string.h>
#include <stdio.h>

static mb_bus_health_t s_health; /**< The single running health-counter instance; see mb_master_get_health(). */

void mb_master_init(void)
{
    memset(&s_health, 0, sizeof(s_health));
}

const mb_bus_health_t *mb_master_get_health(void)
{
    return &s_health;
}

/**
 * @brief Short uppercase tag for a status, used in the one-line log summary (e.g. "TIMEOUT").
 * @param status Status to name.
 * @return Static string literal; never NULL, unrecognised values fall back to "?".
 */
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

/**
 * @brief Build one mb_log_entry_t from a raw frame and append it via mblog_append().
 *
 * Silently does nothing for a zero-length frame (see the raw_len == 0 check
 * below) — a PARAM-rejected request or a timed-out response never touched
 * the wire, so there's nothing worth logging, and calling this
 * unconditionally would append a bogus zero-byte entry for every rejection.
 *
 * @param is_tx        true logs this as the outbound request, false as the
 *                      inbound response — mirrors mb_log_entry_t::is_tx.
 * @param raw          Frame bytes; truncated to fit entry.raw if longer.
 * @param raw_len      Number of valid bytes in @p raw; 0 is a no-op (see above).
 * @param summary      One-line decoded summary shared by both the TX and RX
 *                      entries for this transaction (same string, appended twice).
 * @param timestamp_ms Stamped into the entry verbatim; see mb_master_process().
 */
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

    /* Stale RX bytes mb_core discarded before transmitting this request
     * (overheard third-party traffic on a shared RS-485 bus). 0 for a
     * request that never reached the wire — same `dispatched` gate as the
     * tx/rx reads below, and for the same reason (mb_get_last_discarded()
     * is a single scratch value that a prior call would otherwise leak). */
    uint16_t discarded = dispatched ? mb_get_last_discarded() : 0;

    char summary[64];
    int summary_len = snprintf(summary, sizeof(summary), "FC%02X addr%u start0x%04X cnt%u -> %s",
                                req->fc, req->addr, req->start, req->count, status_name(result.status));
    /* Append the flush diagnostic only when something was actually discarded,
     * so a clean bus logs the exact same summary it always has. Guarded on
     * the base summary having fit, so we never write past summary[]. */
    if (discarded > 0 && summary_len > 0 && (size_t)summary_len < sizeof(summary)) {
        snprintf(summary + summary_len, sizeof(summary) - (size_t)summary_len,
                  " [flushed %u]", discarded);
    }

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
