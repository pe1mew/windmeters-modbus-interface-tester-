#include "mock_led_backend.h"
#include <string.h>

typedef struct { uint8_t r, g, b; } mock_color_t;

static mock_color_t s_history[MOCK_LED_HISTORY_MAX];
static int          s_history_count = 0;

static uint32_t s_last_delay_ms      = 0;
static int      s_delay_call_count  = 0;

static void mock_set_color(void * /*ctx*/, uint8_t r, uint8_t g, uint8_t b)
{
    if (s_history_count < MOCK_LED_HISTORY_MAX) {
        s_history[s_history_count].r = r;
        s_history[s_history_count].g = g;
        s_history[s_history_count].b = b;
        s_history_count++;
    }
}

static void mock_delay_ms(void * /*ctx*/, uint32_t ms)
{
    s_last_delay_ms = ms;
    s_delay_call_count++;
}

led_backend_t mock_led_backend = { mock_set_color, mock_delay_ms, 0 };

void mock_led_reset(void)
{
    memset(s_history, 0, sizeof(s_history));
    s_history_count    = 0;
    s_last_delay_ms    = 0;
    s_delay_call_count = 0;
}

int mock_led_history_count(void)
{
    return s_history_count;
}

void mock_led_history_at(int index, uint8_t *r, uint8_t *g, uint8_t *b)
{
    *r = s_history[index].r;
    *g = s_history[index].g;
    *b = s_history[index].b;
}

uint32_t mock_led_last_delay_ms(void)
{
    return s_last_delay_ms;
}

int mock_led_delay_call_count(void)
{
    return s_delay_call_count;
}
