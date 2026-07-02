/**
 * @file mb_master.h
 * @brief Modbus master request processing — the testable core of MB-2.
 *
 * design/realisationPlan.md §3 describes modbus_master_task as a thin
 * FreeRTOS shim around "process one request": dispatch to mb_core, log
 * the TX+RX frames (LIB-LOG), pulse the status LED (LIB-LED), and update
 * bus health counters. All of that logic lives here instead of inside the
 * FreeRTOS task function, so it can be exercised with `pio test -e native`
 * using the same mocks already built for mb_core/led_status — no FreeRTOS,
 * no queues, no hardware required to test the actual behaviour.
 *
 * The real modbus_master_task (lib/mb_master/modbus_master_task.h, target
 * hardware only) is just: dequeue a request, call mb_master_process(),
 * send the result back to whichever caller is waiting.
 */
#pragma once

#include <stdint.h>
#include "mb_core.h"
#include "mb_frame.h" /* MB_MAX_FRAME_LEN */

typedef struct {
    uint8_t  addr;
    uint8_t  fc;           /**< 0x03, 0x04, 0x06, or 0x10 — anything else is rejected as MB_ERR_PARAM. */
    uint16_t start;         /**< Raw 0-based register address (design/scratchbook.md §5). */
    uint8_t  count;         /**< Registers for FC03/04/16; ignored for FC06. */
    uint16_t values[123];   /**< values[0] used for FC06; the first `count` entries used for FC16. */
} mb_request_t;

typedef struct {
    mb_status_t status;
    uint16_t    registers[125]; /**< Populated for FC03/FC04 when status == MB_OK. */
    uint8_t     exception_code; /**< Valid when status == MB_ERR_EXCEPTION. */

    /**
     * Raw wire frames + attempt count, copied out of mb_core's single-scratch-
     * value accessors (mb_get_last_tx/_rx/_attempts) while they're still
     * fresh, so this result stays correct after a FreeRTOS queue hop even if
     * modbus_master_task has already started the next queued transaction by
     * the time a caller reads it (design/api.md §4.4). raw_tx_len/raw_rx_len
     * are 0 when nothing was sent/received (PARAM rejection, or a pure
     * timeout for raw_rx) — same convention as the mb_core accessors.
     */
    uint8_t     raw_tx[MB_MAX_FRAME_LEN];
    uint16_t    raw_tx_len;
    uint8_t     raw_rx[MB_MAX_FRAME_LEN];
    uint16_t    raw_rx_len;
    uint8_t     attempts;
} mb_result_t;

typedef struct {
    uint32_t crc_errors;
    uint32_t timeouts;
    uint32_t framing_errors;
    bool     has_exception;
    uint8_t  last_exception;
} mb_bus_health_t;

/** @brief Reset the bus health counters. Does not touch mb_core's own state — call mb_init() separately. */
void mb_master_init(void);

/**
 * @brief Process one request end-to-end: dispatch, log, pulse the LED, update health.
 *
 * @param req          The request to execute.
 * @param timestamp_ms Clock value to stamp the log entries with (explicit,
 *                      not read internally, so this stays testable without
 *                      a real or mocked clock — the real caller passes
 *                      millis() or an NTP-synced equivalent).
 */
mb_result_t mb_master_process(const mb_request_t *req, uint32_t timestamp_ms);

/** @brief Current bus health snapshot. */
const mb_bus_health_t *mb_master_get_health(void);
