/**
 * @file mock_transport.h
 * @brief Fake mb_transport_t for the native unit-test build.
 *
 * Unlike a real transport, this one doesn't simulate time passing — each
 * queued event is either "here are N response bytes" or "timeout" (0
 * bytes), popped in order on successive read() calls. That's enough to
 * exercise mb_core's timeout/retry loop without a controllable clock.
 *
 * @note Do not include this header in production (target) builds.
 */
#pragma once

#include <stdint.h>
#include "../../lib/mb_core/mb_transport.h"

/** @brief Clear all queued events, the last-transmitted buffer, and write count. */
void mock_transport_reset(void);

/** @brief Queue @p len response bytes to be returned by the next read() call. */
void mock_transport_queue_response(const uint8_t *bytes, uint16_t len);

/** @brief Queue a simulated timeout — the next read() call returns 0 bytes. */
void mock_transport_queue_timeout(void);

/**
 * @brief Copy the bytes written by the most recent write() call into @p buf.
 * @return Number of bytes copied.
 */
int mock_transport_get_transmitted(uint8_t *buf, int max_len);

/** @brief Number of write() calls made since the last reset. */
int mock_transport_get_write_count(void);

/** @brief The transport instance to pass to mb_init(). */
extern mb_transport_t mock_transport;
