#include "mock_led_backend.h"

static int      s_history_count     = 0;
static uint32_t s_delay_call_count  = 0;

static void mock_set_color(void * /*ctx*/, uint8_t /*r*/, uint8_t /*g*/, uint8_t /*b*/)
{
    s_history_count++;
}

static void mock_delay_ms(void * /*ctx*/, uint32_t /*ms*/)
{
    s_delay_call_count++;
}

led_backend_t mock_led_backend = { mock_set_color, mock_delay_ms, 0 };

void mock_led_reset(void)
{
    s_history_count    = 0;
    s_delay_call_count = 0;
}

int mock_led_history_count(void)
{
    return s_history_count;
}

uint32_t mock_led_delay_call_count(void)
{
    return s_delay_call_count;
}
