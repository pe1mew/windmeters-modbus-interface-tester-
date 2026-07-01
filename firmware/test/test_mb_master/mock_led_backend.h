/**
 * @file mock_led_backend.h
 * @brief Same fake led_backend_t as test_led_status/mock_led_backend.h —
 *        duplicated for the same reason mock_transport.h is (see there).
 *
 * Only what mb_master's tests need: how many pulse-worthy set_color()
 * calls happened. Full colour-sequence inspection lives in
 * test_led_status; these tests only care whether led_status was driven at
 * all for a given mb_status_t outcome.
 */
#pragma once

#include <stdint.h>
#include "../../lib/led_status/led_backend.h"

void mock_led_reset(void);
int  mock_led_history_count(void);
uint32_t mock_led_delay_call_count(void);

extern led_backend_t mock_led_backend;
