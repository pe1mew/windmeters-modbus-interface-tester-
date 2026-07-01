/**
 * @file led_backend.h
 * @brief Injectable LED + delay backend used by led_status.cpp.
 *
 * Same reason mb_core.cpp never calls Serial2 directly (see
 * lib/mb_core/mb_transport.h): led_status.cpp never calls FastLED or
 * Arduino's delay() directly, so its state-machine logic (which colour for
 * which state, what reverts to what) is host-testable without hardware.
 */
#pragma once

#include <stdint.h>

typedef struct {
    /** @brief Set the LED to this RGB colour immediately. */
    void (*set_color)(void *ctx, uint8_t r, uint8_t g, uint8_t b);

    /** @brief Block for @p ms milliseconds (real delay() on target, a no-op recording call on the host). */
    void (*delay_ms)(void *ctx, uint32_t ms);

    void *ctx;
} led_backend_t;
