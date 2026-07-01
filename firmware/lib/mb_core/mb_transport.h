/**
 * @file mb_transport.h
 * @brief Injectable byte-transport interface used by mb_core.
 *
 * mb_core.cpp never calls Serial2 (or anything hardware-specific) directly —
 * it calls through this struct instead. The real implementation
 * (mb_transport_arduino) wraps Serial2 with a millis()-based timeout loop;
 * the native unit tests substitute a fake transport that returns canned
 * bytes instantly, with no real waiting. This is what makes timeout/retry
 * logic testable on the host (see design/realisationPlan.md MB-1).
 */
#pragma once

#include <stdint.h>

typedef struct {
    /**
     * @brief Send @p len bytes. Expected to block until fully transmitted.
     */
    void (*write)(void *ctx, const uint8_t *buf, uint16_t len);

    /**
     * @brief Receive up to @p max_len bytes, waiting up to @p timeout_ms.
     * @return Number of bytes actually received; 0 means "no response
     *         within the timeout" (mb_core treats 0 as a timeout).
     */
    uint16_t (*read)(void *ctx, uint8_t *buf, uint16_t max_len, uint16_t timeout_ms);

    /** @brief Opaque context passed back to write()/read(); may be NULL. */
    void *ctx;
} mb_transport_t;
