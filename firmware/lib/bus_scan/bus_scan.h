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

    /**
     * Parallel arrays, indexed alongside found[] — added for
     * design/api.md §5.3's per-device functions_ok/round_trip_ms. Kept
     * separate from found[] itself (rather than replacing it with a struct
     * array) so the existing WebSocket type:"scan" payload
     * (web_core_build_scan_json) is untouched.
     */
    uint8_t  found_fc[BUS_SCAN_MAX_FOUND];            /**< Function code that got found[i]'s reply — always 0x04 today (bus_scan probes FC04 only); a real field rather than a hardcoded assumption, for when/if multi-FC probing is added. */
    uint32_t found_round_trip_ms[BUS_SCAN_MAX_FOUND]; /**< Wall-clock time for found[i]'s probe. */

    uint32_t start_ms;    /**< Set by bus_scan_start(). */
    uint32_t duration_ms; /**< Set once state becomes SCAN_COMPLETE (design/api.md §5.3). 0 while running/idle/cancelled. */
} bus_scan_status_t;

/**
 * @brief Does this outcome mean "a device is there"?
 *
 * A Modbus exception still means something on the bus heard the probe and
 * replied — just not happily. Only genuine silence (timeout) or noise
 * (CRC/framing errors) mean "nothing valid answered".
 */
bool bus_scan_did_respond(mb_status_t status);

/** @brief Begin a sweep from @p range_start to @p range_end (inclusive). @p timestamp_ms feeds duration_ms once complete. */
void bus_scan_start(uint8_t range_start, uint8_t range_end, uint32_t timestamp_ms);

/** @brief Stop early. No-op if not currently running. */
void bus_scan_cancel(void);

bool bus_scan_is_active(void);

/**
 * @brief Record the outcome of probing the current address, then advance.
 *
 * No-op if the scan isn't running (already cancelled/complete) — guards
 * against a stray late result from a probe that was in flight when the
 * scan was cancelled.
 *
 * @param fc             Function code that was probed (only stored when
 *                        @p responded is true).
 * @param round_trip_ms  Wall-clock time the probe took (only stored when
 *                        @p responded is true).
 * @param timestamp_ms   Used to compute duration_ms if this probe completes
 *                        the sweep; ignored otherwise.
 */
void bus_scan_record_probe_result(bool responded, uint8_t fc, uint32_t round_trip_ms, uint32_t timestamp_ms);

bus_scan_status_t bus_scan_get_status(void);
