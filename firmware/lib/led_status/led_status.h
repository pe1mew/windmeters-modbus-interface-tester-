/**
 * @file led_status.h
 * @brief RGB status LED (LIB-LED, completeRealisationPlan.md) — public API.
 *
 * Ports the template's idle/valid/error/fault colour convention onto this
 * hardware's confirmed LED pin (GPIO35, WS2812B — see
 * memory/gotcha-log.md and design/scratchbook.md §4.1), plus one new state
 * the template didn't need: "scanning", so idle-and-waiting looks
 * different from actively sweeping the bus.
 *
 * State precedence: FAULT dominates everything else. Once set, only
 * led_set_idle() clears it — led_pulse_valid()/led_pulse_error() are
 * ignored while in FAULT so the fault indication is never masked by a
 * transient pulse.
 */
#pragma once

#include <stdint.h>
#include "led_backend.h"

/** @brief Duration of a "pulse" flash before reverting to the current base state. */
#define LED_PULSE_MS 150u

/** @brief Bind the backend to use. Sets the initial state to idle. */
void led_init(const led_backend_t *backend);

/** @brief Steady blue — normal, waiting. */
void led_set_idle(void);

/** @brief Distinct steady colour (amber) — a Bus Scanner sweep is running. Ignored while in fault. */
void led_set_scanning(void);

/** @brief Brief green flash, then reverts to whatever the current base state is. Ignored while in fault. */
void led_pulse_valid(void);

/** @brief Brief red flash, then reverts to whatever the current base state is. Ignored while in fault. */
void led_pulse_error(void);

/** @brief Solid red, latched. Only led_set_idle() clears it. */
void led_set_fault(void);
