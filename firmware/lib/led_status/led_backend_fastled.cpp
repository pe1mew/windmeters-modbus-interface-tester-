/**
 * @file led_backend_fastled.cpp
 * @brief Real led_backend_t backed by FastLED on GPIO35 — implementation.
 *
 * Compiled only under ARDUINO (excluded from the native test build, which
 * links mock_led_backend instead). Single fixed WS2812B pixel on
 * LED_GPIO_PIN, GRB colour order, brightness capped at 50 — matches the
 * confirmed-working blinkyS3 bring-up sketch exactly (see
 * memory/gotcha-log.md and led_backend_fastled.h's file header); deviating
 * from those parameters is how a "working" LED silently shows wrong
 * colours (RGB/GRB swap) or blinds someone on the bench (no brightness cap).
 */
#ifdef ARDUINO
#include "led_backend_fastled.h"
#include <FastLED.h>
#include <Arduino.h>

#define NUM_LEDS 1 /**< One fixed WS2812B pixel — this board has exactly one status LED. */

/** @brief The single physical pixel every fastled_* callback below writes through FastLED.show(). */
static CRGB s_leds[NUM_LEDS];

/**
 * @brief led_backend_t::set_color — writes the one pixel and pushes it out immediately via FastLED.show().
 * @param r Red channel, 0-255.
 * @param g Green channel, 0-255.
 * @param b Blue channel, 0-255.
 */
static void fastled_set_color(void * /*ctx*/, uint8_t r, uint8_t g, uint8_t b)
{
    s_leds[0] = CRGB(r, g, b);
    FastLED.show();
}

/**
 * @brief led_backend_t::delay_ms — real blocking Arduino delay(); acceptable here since led_status.cpp is not called from a latency-sensitive task.
 * @param ms Milliseconds to block for.
 */
static void fastled_delay_ms(void * /*ctx*/, uint32_t ms)
{
    delay(ms);
}

/** @brief The static backend instance returned by led_backend_fastled_init(); @c ctx is unused (0) since all state lives in s_leds. */
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
