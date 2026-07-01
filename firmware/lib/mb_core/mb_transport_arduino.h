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

#define MB_UART_RX_PIN 5
#define MB_UART_TX_PIN 6

/**
 * @brief Initialise Serial2 at @p baud (8N1) on the confirmed RS485 pins
 *        and return a transport bound to it.
 *
 * @return Pointer to a static mb_transport_t suitable for mb_init().
 */
const mb_transport_t *mb_transport_arduino_init(uint32_t baud);
