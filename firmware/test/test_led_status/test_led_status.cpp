/**
 * LIB-LED RGB Status LED — unit tests (native build)
 *
 * Run with:  pio test -e native -f test_led_status
 */
#include <unity.h>
#include "led_status.h"
#include "mock_led_backend.h"

void setUp(void)
{
    mock_led_reset();
    led_init(&mock_led_backend);
    mock_led_reset(); /* discard the init-time colour set so each test starts clean */
}

void tearDown(void) {}

static void assert_color(int history_index, uint8_t exp_r, uint8_t exp_g, uint8_t exp_b)
{
    uint8_t r, g, b;
    mock_led_history_at(history_index, &r, &g, &b);
    TEST_ASSERT_EQUAL_HEX8(exp_r, r);
    TEST_ASSERT_EQUAL_HEX8(exp_g, g);
    TEST_ASSERT_EQUAL_HEX8(exp_b, b);
}

void test_led_init_sets_idle_blue(void)
{
    /* Re-init without discarding, to see the boot colour directly. */
    mock_led_reset();
    led_init(&mock_led_backend);

    TEST_ASSERT_EQUAL_INT(1, mock_led_history_count());
    assert_color(0, 0, 0, 255);
}

void test_led_set_scanning_is_distinct_from_idle(void)
{
    led_set_scanning();
    TEST_ASSERT_EQUAL_INT(1, mock_led_history_count());
    uint8_t r, g, b;
    mock_led_history_at(0, &r, &g, &b);
    TEST_ASSERT_FALSE(r == 0 && g == 0 && b == 255); /* must not equal idle blue */
}

void test_led_pulse_valid_flashes_green_then_returns_to_idle(void)
{
    led_set_idle();
    mock_led_reset();

    led_pulse_valid();

    TEST_ASSERT_EQUAL_INT(2, mock_led_history_count());
    assert_color(0, 0, 255, 0);   /* green flash */
    assert_color(1, 0, 0, 255);   /* back to idle blue */
    TEST_ASSERT_EQUAL_UINT32(LED_PULSE_MS, mock_led_last_delay_ms());
    TEST_ASSERT_EQUAL_INT(1, mock_led_delay_call_count());
}

void test_led_pulse_error_returns_to_scanning_not_idle(void)
{
    /* Base state is SCANNING, not idle — the pulse must revert to it. */
    led_set_scanning();
    mock_led_reset();

    led_pulse_error();

    TEST_ASSERT_EQUAL_INT(2, mock_led_history_count());
    assert_color(0, 255, 0, 0);     /* red flash */
    assert_color(1, 255, 180, 0);   /* back to scanning amber, NOT idle blue */
}

void test_led_fault_latches_solid_red(void)
{
    led_set_fault();
    TEST_ASSERT_EQUAL_INT(1, mock_led_history_count());
    assert_color(0, 255, 0, 0);
}

void test_led_fault_ignores_pulses_and_scanning(void)
{
    led_set_fault();
    mock_led_reset();

    led_pulse_valid();
    led_pulse_error();
    led_set_scanning();

    /* None of these should have touched the LED while faulted. */
    TEST_ASSERT_EQUAL_INT(0, mock_led_history_count());
    TEST_ASSERT_EQUAL_INT(0, mock_led_delay_call_count());
}

void test_led_set_idle_clears_fault(void)
{
    led_set_fault();
    mock_led_reset();

    led_set_idle();
    TEST_ASSERT_EQUAL_INT(1, mock_led_history_count());
    assert_color(0, 0, 0, 255);

    /* Pulses work normally again now that fault is cleared. */
    mock_led_reset();
    led_pulse_valid();
    TEST_ASSERT_EQUAL_INT(2, mock_led_history_count());
}

int main(int /*argc*/, char ** /*argv*/)
{
    UNITY_BEGIN();
    RUN_TEST(test_led_init_sets_idle_blue);
    RUN_TEST(test_led_set_scanning_is_distinct_from_idle);
    RUN_TEST(test_led_pulse_valid_flashes_green_then_returns_to_idle);
    RUN_TEST(test_led_pulse_error_returns_to_scanning_not_idle);
    RUN_TEST(test_led_fault_latches_solid_red);
    RUN_TEST(test_led_fault_ignores_pulses_and_scanning);
    RUN_TEST(test_led_set_idle_clears_fault);
    return UNITY_END();
}
