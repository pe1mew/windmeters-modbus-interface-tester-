/**
 * @file led_status.cpp
 * @brief RGB status LED (LIB-LED) — implementation.
 *
 * The whole state machine reduces to one enum (led_base_state_t) plus the
 * FAULT-dominates precedence rule enforced at each entry point below —
 * see led_status.h's file header for why FAULT is special-cased instead of
 * being just another base state a pulse could interrupt. Pulses
 * (led_pulse_valid()/led_pulse_error()) are not represented in the enum:
 * they're a transient set_color()+delay_ms()+revert sequence, not a state
 * the machine remembers, which is why apply_base_color() only ever needs
 * to know about the three enum values.
 */
#include "led_status.h"

/** @brief The three steady ("base") states led_status.cpp can be in between pulses. Pulses are not a base state — see file header. */
typedef enum {
    LED_BASE_IDLE = 0,     /**< Steady blue, waiting. Entered by led_init() and led_set_idle(); the only state transition that can clear FAULT. */
    LED_BASE_SCANNING,     /**< Steady amber, a Bus Scanner sweep is running. Entered by led_set_scanning(); ignored while in FAULT. */
    LED_BASE_FAULT,        /**< Solid red, latched. Entered by led_set_fault(); dominates all other states and all pulses until led_set_idle(). */
} led_base_state_t;

/** @brief Backend bound by led_init(); NULL until then. Every function below dereferences it without a NULL check — callers must call led_init() first. */
static const led_backend_t *s_backend    = 0;
/** @brief Current steady state, used both to pick the colour on the next apply_base_color() call and to gate the FAULT-dominates rule. */
static led_base_state_t      s_base_state = LED_BASE_IDLE;

/** @brief Push s_base_state's colour to the backend. The sole place base-state-to-RGB mapping lives, so every state-changing function below stays a one-line state update plus this call. */
static void apply_base_color(void)
{
    switch (s_base_state) {
        case LED_BASE_IDLE:     s_backend->set_color(s_backend->ctx, 0,   0,   255); break; /* blue */
        case LED_BASE_SCANNING: s_backend->set_color(s_backend->ctx, 255, 180, 0);   break; /* amber */
        case LED_BASE_FAULT:    s_backend->set_color(s_backend->ctx, 255, 0,   0);   break; /* solid red */
    }
}

void led_init(const led_backend_t *backend)
{
    s_backend   = backend;
    s_base_state = LED_BASE_IDLE;
    apply_base_color();
}

void led_set_idle(void)
{
    s_base_state = LED_BASE_IDLE;
    apply_base_color();
}

void led_set_scanning(void)
{
    if (s_base_state == LED_BASE_FAULT) {
        return; /* fault dominates */
    }
    s_base_state = LED_BASE_SCANNING;
    apply_base_color();
}

void led_set_fault(void)
{
    s_base_state = LED_BASE_FAULT;
    apply_base_color();
}

void led_pulse_valid(void)
{
    if (s_base_state == LED_BASE_FAULT) {
        return; /* fault dominates — no pulses shown while faulted */
    }
    s_backend->set_color(s_backend->ctx, 0, 255, 0); /* green */
    s_backend->delay_ms(s_backend->ctx, LED_PULSE_MS);
    apply_base_color();
}

void led_pulse_error(void)
{
    if (s_base_state == LED_BASE_FAULT) {
        return;
    }
    s_backend->set_color(s_backend->ctx, 255, 0, 0); /* red */
    s_backend->delay_ms(s_backend->ctx, LED_PULSE_MS);
    apply_base_color();
}
