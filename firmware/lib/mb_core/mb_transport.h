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

/**
 * @brief Byte-level I/O contract mb_core.cpp uses instead of calling
 *        hardware (e.g. Serial2) directly.
 *
 * Exactly one instance is active at a time — the one passed to mb_init().
 * mb_core is single-threaded and single-instance, so neither callback needs
 * to be reentrant or thread-safe; they only ever need to satisfy one
 * in-flight request/response at a time (see do_transaction() in
 * mb_core.cpp). A concrete instance normally lives as a static/global for
 * its whole process lifetime (see mb_transport_arduino_init()) and is
 * handed to mb_init() as a `const mb_transport_t *` that must outlive every
 * subsequent mb_* call.
 */
typedef struct {
    /**
     * @brief Send @p len bytes. Expected to block until fully transmitted.
     *
     * Called once per attempt by do_transaction() in mb_core.cpp, immediately
     * before the matching read(). @p buf is owned by the caller (mb_core's
     * internal request buffer) and is only guaranteed valid for the duration
     * of this call — an implementation that needs the bytes afterwards must
     * copy them.
     *
     * @param ctx Opaque context, passed through unchanged from the @c ctx
     *            field below; NULL if unused.
     * @param buf Bytes to send (a complete Modbus RTU request frame,
     *            including CRC). Caller-owned, valid only for this call.
     * @param len Number of bytes in @p buf.
     */
    void (*write)(void *ctx, const uint8_t *buf, uint16_t len);

    /**
     * @brief Receive up to @p max_len bytes, waiting up to @p timeout_ms.
     *
     * Called once per attempt by do_transaction() in mb_core.cpp, immediately
     * after the matching write(). An implementation decides for itself what
     * "the frame is over" means (e.g. an inter-byte gap, since Modbus RTU
     * response length isn't known up front for exception responses) — it is
     * not told how many bytes to expect.
     *
     * @param ctx       Opaque context, passed through unchanged from the
     *                  @c ctx field below; NULL if unused.
     * @param buf       Destination buffer for received bytes. Caller-owned
     *                  (mb_core's internal response buffer); must be at
     *                  least @p max_len bytes.
     * @param max_len   Capacity of @p buf.
     * @param timeout_ms Maximum time to wait for a response, milliseconds.
     * @return Number of bytes actually received; 0 means "no response
     *         within the timeout" (mb_core treats 0 as a timeout).
     */
    uint16_t (*read)(void *ctx, uint8_t *buf, uint16_t max_len, uint16_t timeout_ms);

    /** @brief Opaque context passed back to write()/read(); may be NULL. */
    void *ctx;
} mb_transport_t;
