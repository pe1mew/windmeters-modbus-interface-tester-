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
    return &s_arduino_transport;
}

#endif /* ARDUINO */
