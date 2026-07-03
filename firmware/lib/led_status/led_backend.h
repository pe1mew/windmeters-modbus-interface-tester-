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

/**
 * @brief Hardware vtable injected into led_status.cpp via led_init().
 *
 * led_status.cpp's state machine (idle/scanning/fault/pulse precedence) is
 * expressed purely in terms of these two calls, so it never references
 * FastLED or Arduino's delay() and is fully exercisable on the host — see
 * test_led_status's mock_led_backend.cpp, which records calls instead of
 * driving real hardware. Both callbacks take @c ctx as their first
 * argument, unmodified, on every call.
 */
typedef struct {
    /** @brief Set the LED to this RGB colour immediately. Called by led_status.cpp on every state transition and at the start/end of every pulse. */
    void (*set_color)(void *ctx, uint8_t r, uint8_t g, uint8_t b);

    /** @brief Block for @p ms milliseconds (real delay() on target, a no-op recording call on the host). Called once per pulse (led_pulse_valid()/led_pulse_error()) to hold the pulse colour before reverting to the base state. */
    void (*delay_ms)(void *ctx, uint32_t ms);

    /** @brief Opaque state passed unmodified as the first argument to both callbacks above. May be NULL if the backend needs no state. */
    void *ctx;
} led_backend_t;
