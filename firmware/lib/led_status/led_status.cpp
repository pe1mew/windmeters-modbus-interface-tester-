#include "led_status.h"

typedef enum {
    LED_BASE_IDLE = 0,
    LED_BASE_SCANNING,
    LED_BASE_FAULT,
} led_base_state_t;

static const led_backend_t *s_backend    = 0;
static led_base_state_t      s_base_state = LED_BASE_IDLE;

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
