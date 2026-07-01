/**
 * @file bus_scan.h
 * @brief Bus Scanner — the testable core (TASK-SCAN,
 *        design/completeRealisationPlan.md).
 *
 * Same division of labour as everywhere else in this project: this file
 * is a pure state machine (which address are we on, did it respond, are
 * we done) with no queue/task/UART calls, so it's host-testable. The
 * actual "submit a probe to modbus_master_task and wait for the reply" is
 * scan_task.cpp (Arduino-only).
 */
#pragma once

#include <stdint.h>
#include "mb_core.h" /* mb_status_t */

typedef enum {
    SCAN_IDLE = 0,
    SCAN_RUNNING,
    SCAN_CANCELLED,
    SCAN_COMPLETE,
} scan_state_t;

/** @brief Max addresses trackable in one sweep — matches the valid Modbus unicast range. */
#define BUS_SCAN_MAX_FOUND 247

typedef struct {
    scan_state_t state;
    uint8_t current_addr; /**< Address just probed / about to be probed. */
    uint8_t range_start;
    uint8_t range_end;
    uint8_t found[BUS_SCAN_MAX_FOUND];
    uint8_t found_count;
} bus_scan_status_t;

/**
 * @brief Does this outcome mean "a device is there"?
 *
 * A Modbus exception still means something on the bus heard the probe and
 * replied — just not happily. Only genuine silence (timeout) or noise
 * (CRC/framing errors) mean "nothing valid answered".
 */
bool bus_scan_did_respond(mb_status_t status);

/** @brief Begin a sweep from @p range_start to @p range_end (inclusive). */
void bus_scan_start(uint8_t range_start, uint8_t range_end);

/** @brief Stop early. No-op if not currently running. */
void bus_scan_cancel(void);

bool bus_scan_is_active(void);

/**
 * @brief Record the outcome of probing the current address, then advance.
 *
 * No-op if the scan isn't running (already cancelled/complete) — guards
 * against a stray late result from a probe that was in flight when the
 * scan was cancelled.
 */
void bus_scan_record_probe_result(bool responded);

bus_scan_status_t bus_scan_get_status(void);
