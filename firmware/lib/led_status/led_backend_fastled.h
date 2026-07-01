/**
 * @file led_backend_fastled.h
 * @brief Real led_backend_t backed by FastLED on GPIO35 — target hardware only.
 *
 * GPIO35, WS2812B, GRB order, brightness 50 — matches the confirmed-working
 * Atom-RS485-Base-explorations/blinkyS3/main.cpp test sketch exactly (see
 * memory/gotcha-log.md).
 */
#pragma once

#include "led_backend.h"

#define LED_GPIO_PIN 35

/**
 * @brief Initialise FastLED on GPIO35 and return a backend bound to it.
 * @return Pointer to a static led_backend_t suitable for led_init().
 */
const led_backend_t *led_backend_fastled_init(void);
