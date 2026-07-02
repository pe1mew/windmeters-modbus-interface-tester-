/**
 * wind_poll decode/config core — unit tests (native build)
 *
 * Covers completeRealisationPlan.md TASK-WIND's acceptance tests at the
 * decode/encode level, split for wind speed and wind direction as two
 * physically separate units (windmeters-modbus-interface/design/scratchBook.md
 * "Modbus configuration" — same firmware source, compile-time define picks
 * which sensor a given build reports, own slave address, own compact
 * register numbering from 0x0000).
 *
 * Run with:  pio test -e native -f test_wind_poll
 */
#include <unity.h>
#include "wind_poll.h"

void setUp(void) {}
void tearDown(void) {}

/* ── decode — WindSpeed ── */

void test_wind_poll_decodes_speed_registers_correctly(void)
{
    /* scratchbook.md §7 Wind Test mockup's speed half: instant 4.2 m/s,
     * avg 3.9 m/s, raw pulses 27. */
    uint16_t raw[3] = {42, 39, 27};
    wind_reading_t reading;
    wind_poll_decode_speed(raw, &reading);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.2f, reading.speed_instant_ms);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.9f, reading.speed_avg_ms);
    TEST_ASSERT_EQUAL_UINT16(27, reading.raw_pulses);
}

void test_wind_poll_decodes_zero_speed(void)
{
    uint16_t raw[3] = {0, 0, 0};
    wind_reading_t reading;
    wind_poll_decode_speed(raw, &reading);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, reading.speed_instant_ms);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, reading.speed_avg_ms);
    TEST_ASSERT_EQUAL_UINT16(0, reading.raw_pulses);
}

void test_wind_poll_speed_does_not_use_s200_scale(void)
{
    /* The classic mistake this project explicitly flagged as a gotcha:
     * reusing the S200's ×1000 scale instead of this DUT's ×10. Raw 42
     * must decode to 4.2 m/s, NOT 0.042 m/s. */
    uint16_t raw[3] = {42, 0, 0};
    wind_reading_t reading;
    wind_poll_decode_speed(raw, &reading);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.2f, reading.speed_instant_ms);
    TEST_ASSERT_TRUE(reading.speed_instant_ms > 1.0f); /* would be ~0.04 under the wrong scale */
}

/* ── decode — WindDirection ── */

void test_wind_poll_decodes_direction_registers_correctly(void)
{
    /* scratchbook.md §7 Wind Test mockup's direction half: instant 183.4°,
     * avg 181.0°. */
    uint16_t raw[2] = {1834, 1810};
    wind_reading_t reading;
    wind_poll_decode_direction(raw, &reading);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 183.4f, reading.dir_instant_deg);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 181.0f, reading.dir_avg_deg);
}

void test_wind_poll_decodes_zero_direction(void)
{
    uint16_t raw[2] = {0, 0};
    wind_reading_t reading;
    wind_poll_decode_direction(raw, &reading);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, reading.dir_instant_deg);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, reading.dir_avg_deg);
}

void test_wind_poll_direction_does_not_use_s200_scale(void)
{
    uint16_t raw[2] = {1834, 0};
    wind_reading_t reading;
    wind_poll_decode_direction(raw, &reading);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 183.4f, reading.dir_instant_deg);
    TEST_ASSERT_TRUE(reading.dir_instant_deg > 100.0f); /* would be ~1.8 under the wrong scale */
}

void test_speed_decode_leaves_direction_fields_zero(void)
{
    uint16_t raw[3] = {42, 39, 27};
    wind_reading_t reading;
    wind_poll_decode_speed(raw, &reading);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, reading.dir_instant_deg);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, reading.dir_avg_deg);
}

void test_direction_decode_leaves_speed_fields_zero(void)
{
    uint16_t raw[2] = {1834, 1810};
    wind_reading_t reading;
    wind_poll_decode_direction(raw, &reading);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, reading.speed_instant_ms);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, reading.speed_avg_ms);
    TEST_ASSERT_EQUAL_UINT16(0, reading.raw_pulses);
}

/* ── register counts ── */

void test_input_register_counts(void)
{
    TEST_ASSERT_EQUAL_UINT8(3, wind_sensor_input_register_count(WIND_SENSOR_SPEED));
    TEST_ASSERT_EQUAL_UINT8(2, wind_sensor_input_register_count(WIND_SENSOR_DIRECTION));
}

void test_holding_register_counts(void)
{
    TEST_ASSERT_EQUAL_UINT8(3, wind_sensor_holding_register_count(WIND_SENSOR_SPEED));
    TEST_ASSERT_EQUAL_UINT8(3, wind_sensor_holding_register_count(WIND_SENSOR_DIRECTION));
}

/* ── config field register mapping — type-aware ── */

void test_config_field_register_mapping_speed(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x0000, wind_config_field_register(WIND_SENSOR_SPEED, WIND_CFG_DEVICE_ADDR));
    TEST_ASSERT_EQUAL_HEX16(0x0001, wind_config_field_register(WIND_SENSOR_SPEED, WIND_CFG_MEASUREMENT_WINDOW));
    TEST_ASSERT_EQUAL_HEX16(0x0002, wind_config_field_register(WIND_SENSOR_SPEED, WIND_CFG_AVERAGING_WINDOW));
    /* dir_offset doesn't exist on a speed device */
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, wind_config_field_register(WIND_SENSOR_SPEED, WIND_CFG_DIR_OFFSET));
}

void test_config_field_register_mapping_direction(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x0000, wind_config_field_register(WIND_SENSOR_DIRECTION, WIND_CFG_DEVICE_ADDR));
    TEST_ASSERT_EQUAL_HEX16(0x0001, wind_config_field_register(WIND_SENSOR_DIRECTION, WIND_CFG_DIR_OFFSET));
    TEST_ASSERT_EQUAL_HEX16(0x0002, wind_config_field_register(WIND_SENSOR_DIRECTION, WIND_CFG_AVERAGING_WINDOW));
    /* measurement_window doesn't exist on a direction device */
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, wind_config_field_register(WIND_SENSOR_DIRECTION, WIND_CFG_MEASUREMENT_WINDOW));
}

/* ── encode / decode (unchanged by the split — pure value math) ── */

void test_config_field_encode_device_addr_unscaled(void)
{
    TEST_ASSERT_EQUAL_UINT16(31, wind_config_field_encode(WIND_CFG_DEVICE_ADDR, 31.0f));
}

void test_config_field_encode_dir_offset_scaled_by_10(void)
{
    TEST_ASSERT_EQUAL_UINT16(1234, wind_config_field_encode(WIND_CFG_DIR_OFFSET, 123.4f));
}

void test_config_field_encode_windows_unscaled(void)
{
    TEST_ASSERT_EQUAL_UINT16(1000, wind_config_field_encode(WIND_CFG_MEASUREMENT_WINDOW, 1000.0f));
    TEST_ASSERT_EQUAL_UINT16(10, wind_config_field_encode(WIND_CFG_AVERAGING_WINDOW, 10.0f));
}

void test_config_field_decode_round_trip(void)
{
    uint16_t raw = wind_config_field_encode(WIND_CFG_DIR_OFFSET, 45.6f);
    float decoded = wind_config_field_decode(WIND_CFG_DIR_OFFSET, raw);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 45.6f, decoded);

    uint16_t raw_addr = wind_config_field_encode(WIND_CFG_DEVICE_ADDR, 31.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 31.0f, wind_config_field_decode(WIND_CFG_DEVICE_ADDR, raw_addr));
}

void test_interval_elapsed_false_before_interval(void)
{
    TEST_ASSERT_FALSE(wind_poll_interval_elapsed(1500, 1000, 1000)); /* only 500ms have passed */
}

void test_interval_elapsed_true_at_or_after_interval(void)
{
    TEST_ASSERT_TRUE(wind_poll_interval_elapsed(2000, 1000, 1000));  /* exactly 1000ms */
    TEST_ASSERT_TRUE(wind_poll_interval_elapsed(2500, 1000, 1000));  /* 1500ms, well past */
}

int main(int /*argc*/, char ** /*argv*/)
{
    UNITY_BEGIN();
    RUN_TEST(test_wind_poll_decodes_speed_registers_correctly);
    RUN_TEST(test_wind_poll_decodes_zero_speed);
    RUN_TEST(test_wind_poll_speed_does_not_use_s200_scale);
    RUN_TEST(test_wind_poll_decodes_direction_registers_correctly);
    RUN_TEST(test_wind_poll_decodes_zero_direction);
    RUN_TEST(test_wind_poll_direction_does_not_use_s200_scale);
    RUN_TEST(test_speed_decode_leaves_direction_fields_zero);
    RUN_TEST(test_direction_decode_leaves_speed_fields_zero);
    RUN_TEST(test_input_register_counts);
    RUN_TEST(test_holding_register_counts);
    RUN_TEST(test_config_field_register_mapping_speed);
    RUN_TEST(test_config_field_register_mapping_direction);
    RUN_TEST(test_config_field_encode_device_addr_unscaled);
    RUN_TEST(test_config_field_encode_dir_offset_scaled_by_10);
    RUN_TEST(test_config_field_encode_windows_unscaled);
    RUN_TEST(test_config_field_decode_round_trip);
    RUN_TEST(test_interval_elapsed_false_before_interval);
    RUN_TEST(test_interval_elapsed_true_at_or_after_interval);
    return UNITY_END();
}
