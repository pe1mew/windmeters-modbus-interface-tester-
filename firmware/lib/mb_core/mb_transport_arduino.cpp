/**
 * @file mb_transport_arduino.cpp
 * @brief Real mb_transport_t backed by Serial2 — implementation.
 *
 * Entirely gated behind `#ifdef ARDUINO`: this file compiles to nothing in
 * the native test env (see mb_frame.h's file-level doc comment), since there is no
 * Serial2 there and the native tests use a fake transport instead. There is
 * no non-Arduino stub branch here — the whole translation unit is a no-op
 * outside the Arduino build.
 *
 * Single static mb_transport_t instance (s_arduino_transport) — matches
 * mb_core's singleton assumption (see mb_transport.h) that exactly one
 * transport is active for the process lifetime.
 */
#ifdef ARDUINO
#include "mb_transport_arduino.h"
#include <Arduino.h>

/**
 * @brief mb_transport_t::write implementation: blocking Serial2 send.
 *
 * Satisfies the write() contract in mb_transport.h — blocks until fully
 * transmitted via Serial2.flush(), so the matching read() that mb_core
 * issues right after is guaranteed to start after the last TX bit is on
 * the wire.
 *
 * @param buf Request frame bytes to send.
 * @param len Number of bytes in @p buf to send.
 */
static void arduino_write(void * /*ctx*/, const uint8_t *buf, uint16_t len)
{
    Serial2.write(buf, len);
    Serial2.flush();
}

/**
 * @brief mb_transport_t::read implementation: Serial2 read with an
 *        inter-byte timeout standing in for "end of frame".
 *
 * @param buf        Destination buffer for the response frame.
 * @param max_len    Capacity of @p buf in bytes.
 * @param timeout_ms Per-byte inter-byte timeout; a gap longer than this
 *                    ends the read.
 * @return Number of bytes actually read into @p buf (0 on a full timeout
 *         with nothing received).
 */
static uint16_t arduino_read(void * /*ctx*/, uint8_t *buf, uint16_t max_len, uint16_t timeout_ms)
{
    /* Stream::readBytes() waits up to setTimeout() for each byte and stops
     * on the first inter-byte gap that exceeds it — a reasonable proxy for
     * "the frame is over" without knowing the exact expected length up
     * front (exception responses are shorter than normal ones). */
    Serial2.setTimeout(timeout_ms);
    return (uint16_t)Serial2.readBytes((char *)buf, max_len);
}

/**
 * @brief mb_transport_t::flush implementation: drain Serial2's RX buffer now.
 *
 * Satisfies the flush() contract in mb_transport.h — discards every byte
 * currently buffered (does not block waiting for more) and returns the
 * count. mb_core calls this before each transmit so a request's read() can't
 * pick up traffic overheard from other masters on a shared RS-485 bus while
 * this tester was idle. Same drain idiom the boot-time flush in
 * mb_transport_arduino_init() uses, but counted, and run on every request
 * rather than only once at startup.
 *
 * @return Number of stale bytes discarded (0 if the buffer was already empty).
 */
static uint16_t arduino_flush(void * /*ctx*/)
{
    uint16_t discarded = 0;
    while (Serial2.available()) {
        Serial2.read();
        discarded++;
    }
    return discarded;
}

/** @brief The one live mb_transport_t instance, wired to the arduino_write()/arduino_read()/arduino_flush() trio above. */
static mb_transport_t s_arduino_transport = { arduino_write, arduino_read, arduino_flush, 0 };

const mb_transport_t *mb_transport_arduino_init(uint32_t baud)
{
    Serial2.begin(baud, SERIAL_8N1, MB_UART_RX_PIN, MB_UART_TX_PIN);

    /* A stray byte reliably lands in the RX buffer right after begin() —
     * confirmed on the bench via UART loopback (memory/gotcha-log.md).
     * Nothing reads Serial2 between here and the first real transaction, so
     * that byte sits queued indefinitely and gets consumed as the leading
     * byte of whatever the first real read is, shifting every byte after it
     * by one position — corrupts that read's CRC, and often the next read
     * too (the last real byte of the first, now-misaligned read is left
     * behind for the second read to pick up first). Symptom: the first
     * Modbus transaction(s) after every boot fail with a CRC or framing
     * error; every transaction since is clean, until the next reset.
     * Flushing here — once, before anything else touches the bus — is
     * cheap and fully closes it. A short settle delay first, since the
     * glitch is a peripheral-init artifact that may not have landed in the
     * buffer the instant begin() returns. */
    delay(10);
    while (Serial2.available()) {
        Serial2.read();
    }

    return &s_arduino_transport;
}

#endif /* ARDUINO */
