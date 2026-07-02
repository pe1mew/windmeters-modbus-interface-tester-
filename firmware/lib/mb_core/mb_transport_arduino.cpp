#ifdef ARDUINO
#include "mb_transport_arduino.h"
#include <Arduino.h>

static void arduino_write(void * /*ctx*/, const uint8_t *buf, uint16_t len)
{
    Serial2.write(buf, len);
    Serial2.flush();
}

static uint16_t arduino_read(void * /*ctx*/, uint8_t *buf, uint16_t max_len, uint16_t timeout_ms)
{
    /* Stream::readBytes() waits up to setTimeout() for each byte and stops
     * on the first inter-byte gap that exceeds it — a reasonable proxy for
     * "the frame is over" without knowing the exact expected length up
     * front (exception responses are shorter than normal ones). */
    Serial2.setTimeout(timeout_ms);
    return (uint16_t)Serial2.readBytes((char *)buf, max_len);
}

static mb_transport_t s_arduino_transport = { arduino_write, arduino_read, 0 };

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
