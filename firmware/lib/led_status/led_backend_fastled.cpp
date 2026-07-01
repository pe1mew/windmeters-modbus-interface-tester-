#ifdef ARDUINO
#include "led_backend_fastled.h"
#include <FastLED.h>
#include <Arduino.h>

#define NUM_LEDS 1

static CRGB s_leds[NUM_LEDS];

static void fastled_set_color(void * /*ctx*/, uint8_t r, uint8_t g, uint8_t b)
{
    s_leds[0] = CRGB(r, g, b);
    FastLED.show();
}

static void fastled_delay_ms(void * /*ctx*/, uint32_t ms)
{
    delay(ms);
}

static led_backend_t s_fastled_backend = { fastled_set_color, fastled_delay_ms, 0 };

const led_backend_t *led_backend_fastled_init(void)
{
    FastLED.addLeds<WS2812B, LED_GPIO_PIN, GRB>(s_leds, NUM_LEDS);
    FastLED.setBrightness(50);
    s_leds[0] = CRGB::Black;
    FastLED.show();
    return &s_fastled_backend;
}

#endif /* ARDUINO */
