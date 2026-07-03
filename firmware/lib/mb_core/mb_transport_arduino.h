/**
 * @file mb_transport_arduino.h
 * @brief Real mb_transport_t backed by Serial2, for the windmeterTester
 *        hardware build only (never compiled into the native test env).
 *
 * UART pins confirmed on the bench: RX = G5, TX = G6 (design/scratchbook.md
 * §4.1/§4.2). No DE/RE GPIO — the Atomic RS485 Base auto-directions in
 * hardware.
 */
#pragma once

#include "mb_transport.h"

/** @brief Confirmed RS485 RX pin (GPIO), see design/scratchbook.md §4.1/§4.2. */
#define MB_UART_RX_PIN 5
/** @brief Confirmed RS485 TX pin (GPIO), see design/scratchbook.md §4.1/§4.2. */
#define MB_UART_TX_PIN 6

/**
 * @brief Initialise Serial2 at @p baud (8N1) on the confirmed RS485 pins
 *        and return a transport bound to it.
 *
 * @param baud UART baud rate, e.g. 9600 or 115200. Not validated here —
 *             must match whatever the DUT is configured for.
 * @return Pointer to a static mb_transport_t suitable for mb_init().
 */
const mb_transport_t *mb_transport_arduino_init(uint32_t baud);
