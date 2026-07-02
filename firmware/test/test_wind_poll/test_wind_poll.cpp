/**
 * wind_poll decode/config core — unit tests (native build)
 *
 * Covers completeRealisationPlan.md TASK-WIND's acceptance tests at the
 * decode/encode level, against the DUT's Technical Design Specification
 * (windmeters-modbus-interface/design/TDS.md §2.7/§2.8, v0.6): wind speed
 * and wind direction are two physically separate units — own slave
 * address, own compile-time build — but as of TDS v0.6 both implement an
 * *identical* 12-input/4-holding register map at identical addresses
 * (FR-MB27); a register the active sensor doesn't use just reads 0.
 *
 * Run with:  pio test -e native -f test_wind_poll
 */
#include <unity.h>
#include "wind_poll.h"

void setUp(void) {}
void tearDown(void) {}

/* ── decode ── */

void test_wind_poll_decodes_speed_fields_from_full_register_block(void)
{
    /* TDS §2.7 raw layout: 0=dir_instant 1=speed_instant 2=dir_avg
     * 3=speed_avg 4=raw_diagnostic 5=status 6=id 7=uptime 8=crc_errs
     * 9=req_count 10=seconds_since_pulse 11=gust. On a speed build,
     * 0/2 (direction fields) read 0 off the wire. */
    uint16_t raw[12] = {0, 42, 0, 39, 27, 0, 0x0142, 120, 0, 500, 8, 65};
    wind_reading_t reading;
    wind_poll_decode(WIND_SENSOR_SPEED, raw, &reading);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.2f, reading.speed_instant_ms);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.9f, reading.speed_avg_ms);
    TEST_ASSERT_EQUAL_UINT16(27, reading.raw_diagnostic);
    TEST_ASSERT_EQUAL_UINT16(8, reading.seconds_since_pulse);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 6.5f, reading.gust_ms);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, reading.dir_instant_deg);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, reading.dir_avg_deg);
    TEST_ASSERT_FALSE(reading.dir_fault);
}

void test_wind_poll_decodes_direction_fields_from_full_register_block(void)
{
    uint16_t raw[12] = {1834, 0, 1810, 0, 512, 0, 0x0242, 300, 0, 12, 0, 0};
    wind_reading_t reading;
    wind_poll_decode(WIND_SENSOR_DIRECTION, raw, &reading);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 183.4f, reading.dir_instant_deg);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 181.0f, reading.dir_avg_deg);
    TEST_ASSERT_EQUAL_UINT16(512, reading.raw_diagnostic); /* raw ADC conversion, direction build */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, reading.speed_instant_ms);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, reading.speed_avg_ms);
    TEST_ASSERT_EQUAL_UINT16(0, reading.seconds_since_pulse); /* speed-only field, 0 on direction build */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, reading.gust_ms);  /* speed-only field, 0 on direction build */
    TEST_ASSERT_FALSE(reading.dir_fault);
}

void test_wind_poll_speed_does_not_use_s200_scale(void)
{
    /* The classic mistake this project explicitly flagged as a gotcha:
     * reusing the S200's ×1000 scale instead of this DUT's ×10. Raw 42
     * must decode to 4.2 m/s, NOT 0.042 m/s. */
    uint16_t raw[12] = {0};
    raw[1] = 42;
    wind_reading_t reading;
    wind_poll_decode(WIND_SENSOR_SPEED, raw, &reading);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.2f, reading.speed_instant_ms);
    TEST_ASSERT_TRUE(reading.speed_instant_ms > 1.0f); /* would be ~0.04 under the wrong scale */
}

void test_wind_poll_direction_does_not_use_s200_scale(void)
{
    uint16_t raw[12] = {0};
    raw[0] = 1834;
    wind_reading_t reading;
    wind_poll_decode(WIND_SENSOR_DIRECTION, raw, &reading);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 183.4f, reading.dir_instant_deg);
    TEST_ASSERT_TRUE(reading.dir_instant_deg > 100.0f); /* would be ~1.8 under the wrong scale */
}

void test_wind_poll_decodes_all_zero_block(void)
{
    uint16_t raw[12] = {0};
    wind_reading_t reading;
    wind_poll_decode(WIND_SENSOR_SPEED, raw, &reading);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, reading.speed_instant_ms);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, reading.speed_avg_ms);
    TEST_ASSERT_EQUAL_UINT16(0, reading.raw_diagnostic);
    TEST_ASSERT_FALSE(reading.dir_fault);
}

/* ── TDS FR-S38 direction sensor fault sentinel ── */

void test_wind_poll_direction_fault_sentinel_sets_flag(void)
{
    uint16_t raw[12] = {0};
    raw[0] = 65535; /* dir_instant fault sentinel */
    raw[2] = 65535; /* dir_avg fault sentinel */
    wind_reading_t reading;
    wind_poll_decode(WIND_SENSOR_DIRECTION, raw, &reading);
    TEST_ASSERT_TRUE(reading.dir_fault);
}

void test_wind_poll_direction_fault_sentinel_on_only_one_field_still_flags(void)
{
    uint16_t raw[12] = {0};
    raw[0] = 65535; /* only instant is at the sentinel */
    wind_reading_t reading;
    wind_poll_decode(WIND_SENSOR_DIRECTION, raw, &reading);
    TEST_ASSERT_TRUE(reading.dir_fault);
}

void test_wind_poll_fault_sentinel_ignored_on_speed_build(void)
{
    /* 0x0000/0x0002 aren't meaningful on a speed build even if the wire
     * happens to carry 65535 there (shouldn't happen per TDS §2.7's ○
     * convention, but dir_fault must stay build-type-gated regardless). */
    uint16_t raw[12] = {0};
    raw[0] = 65535;
    wind_reading_t reading;
    wind_poll_decode(WIND_SENSOR_SPEED, raw, &reading);
    TEST_ASSERT_FALSE(reading.dir_fault);
}

/* ── register counts (TDS §2.7/§2.8 — fixed, identical on both builds) ── */

void test_input_register_count_is_twelve(void)
{
    TEST_ASSERT_EQUAL_UINT8(12, wind_sensor_input_register_count());
}

void test_holding_register_count_is_four(void)
{
    TEST_ASSERT_EQUAL_UINT8(4, wind_sensor_holding_register_count());
}

/* ── config field register mapping (TDS §2.8 — fixed, no device-address
 *    register as of v0.6: FR-MB07/FR-MB26) ── */

void test_config_field_register_mapping(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x0000, wind_config_field_register(WIND_CFG_DIR_OFFSET));
    TEST_ASSERT_EQUAL_HEX16(0x0001, wind_config_field_register(WIND_CFG_MEASUREMENT_WINDOW));
    TEST_ASSERT_EQUAL_HEX16(0x0002, wind_config_field_register(WIND_CFG_AVERAGING_WINDOW));
    TEST_ASSERT_EQUAL_HEX16(0x0003, wind_config_field_register(WIND_CFG_LOW_SPEED_CUTOFF));
}

/* ── encode / decode ── */

void test_config_field_encode_dir_offset_scaled_by_10(void)
{
    TEST_ASSERT_EQUAL_UINT16(1234, wind_config_field_encode(WIND_CFG_DIR_OFFSET, 123.4f));
}

void test_config_field_encode_low_speed_cutoff_scaled_by_10(void)
{
    TEST_ASSERT_EQUAL_UINT16(40, wind_config_field_encode(WIND_CFG_LOW_SPEED_CUTOFF, 4.0f));
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

    uint16_t raw_cutoff = wind_config_field_encode(WIND_CFG_LOW_SPEED_CUTOFF, 3.5f);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 3.5f, wind_config_field_decode(WIND_CFG_LOW_SPEED_CUTOFF, raw_cutoff));
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
    RUN_TEST(test_wind_poll_decodes_speed_fields_from_full_register_block);
    RUN_TEST(test_wind_poll_decodes_direction_fields_from_full_register_block);
    RUN_TEST(test_wind_poll_speed_does_not_use_s200_scale);
    RUN_TEST(test_wind_poll_direction_does_not_use_s200_scale);
    RUN_TEST(test_wind_poll_decodes_all_zero_block);
    RUN_TEST(test_wind_poll_direction_fault_sentinel_sets_flag);
    RUN_TEST(test_wind_poll_direction_fault_sentinel_on_only_one_field_still_flags);
    RUN_TEST(test_wind_poll_fault_sentinel_ignored_on_speed_build);
    RUN_TEST(test_input_register_count_is_twelve);
    RUN_TEST(test_holding_register_count_is_four);
    RUN_TEST(test_config_field_register_mapping);
    RUN_TEST(test_config_field_encode_dir_offset_scaled_by_10);
    RUN_TEST(test_config_field_encode_low_speed_cutoff_scaled_by_10);
    RUN_TEST(test_config_field_encode_windows_unscaled);
    RUN_TEST(test_config_field_decode_round_trip);
    RUN_TEST(test_interval_elapsed_false_before_interval);
    RUN_TEST(test_interval_elapsed_true_at_or_after_interval);
    return UNITY_END();
}
