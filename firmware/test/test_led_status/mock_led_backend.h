/**
 * @file mock_led_backend.h
 * @brief Fake led_backend_t for the native unit-test build.
 *
 * Records every set_color() call (so a test can inspect the exact colour
 * sequence a pulse produced, not just the final state) and every
 * delay_ms() call, without actually waiting.
 */
#pragma once

#include <stdint.h>
#include "led_backend.h"

#define MOCK_LED_HISTORY_MAX 16

void mock_led_reset(void);

/** @brief Number of set_color() calls recorded since the last reset. */
int mock_led_history_count(void);

/** @brief Colour set on the @p index'th set_color() call (0 = first). */
void mock_led_history_at(int index, uint8_t *r, uint8_t *g, uint8_t *b);

/** @brief Most recent delay_ms() argument (0 if never called). */
uint32_t mock_led_last_delay_ms(void);

/** @brief Number of delay_ms() calls recorded since the last reset. */
int mock_led_delay_call_count(void);

extern led_backend_t mock_led_backend;
