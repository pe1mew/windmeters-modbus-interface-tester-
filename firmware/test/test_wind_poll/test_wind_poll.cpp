/**
 * wind_poll decode/config core — unit tests (native build)
 *
 * Covers completeRealisationPlan.md TASK-WIND's acceptance tests at the
 * decode/encode level, against the DUT's Technical Design Specification
 * (windmeters-modbus-interface/design/TDS.md §2.7/§2.8, v0.6+): three
 * physically separate build variants exist — wind speed, wind direction,
 * and combined (both sensors behind one slave address) — own slave
 * address, own compile-time build. All three implement the same register
 * layout as far as it goes (FR-MB27); a register the active build's
 * sensor doesn't use just reads 0. The combined build's input map is one
 * register longer than the single-sensor builds' (13 vs 12 — TDS §2.7
 * adds 30013, the combined build's direction raw ADC, since 30005 is
 * taken there by the speed pulse count); the holding map is uniformly 6
 * registers on every build (2026-07-11: added the anemometer calibration
 * pair, 40005/40006).
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

/* ── combined build (TDS §2.7, FR-MB27): 13-register map, both sensors
 *    live in one FC04 read, plus the direction raw ADC at 30013 ── */

void test_wind_poll_decodes_combined_fields_from_full_thirteen_register_block(void)
{
    /* Same 0-11 layout as the single-sensor builds, plus raw[12] = dir_raw_adc
     * (30013) — combined build only. Both dir_* and speed_* fields are live
     * simultaneously here, unlike either single-sensor build. */
    uint16_t raw[13] = {1828, 42, 1810, 39, 30, 0, 0x0301, 120, 0, 500, 0, 65, 520};
    wind_reading_t reading;
    wind_poll_decode(WIND_SENSOR_COMBINED, raw, &reading);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 182.8f, reading.dir_instant_deg);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 181.0f, reading.dir_avg_deg);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.2f, reading.speed_instant_ms);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.9f, reading.speed_avg_ms);
    TEST_ASSERT_EQUAL_UINT16(30, reading.raw_diagnostic);       /* pulse count, not raw ADC, on combined */
    TEST_ASSERT_EQUAL_UINT16(0, reading.seconds_since_pulse);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 6.5f, reading.gust_ms);
    TEST_ASSERT_EQUAL_UINT16(520, reading.dir_raw_adc);         /* 30013 — combined-only field */
    TEST_ASSERT_FALSE(reading.dir_fault);
}

void test_wind_poll_direction_fault_sentinel_also_applies_on_combined_build(void)
{
    /* FR-S38's fault path is the same code on combined as on a
     * direction-only build (per the DUT's own test report) — dir_fault
     * must not be gated to WIND_SENSOR_DIRECTION alone. */
    uint16_t raw[13] = {0};
    raw[0] = 65535;
    raw[2] = 65535;
    wind_reading_t reading;
    wind_poll_decode(WIND_SENSOR_COMBINED, raw, &reading);
    TEST_ASSERT_TRUE(reading.dir_fault);
}

void test_wind_poll_dir_raw_adc_is_zero_for_non_combined_types(void)
{
    /* Speed/direction builds only have a 12-register map (TDS §2.7) — a
     * caller is allowed to pass a 12-entry buffer for those (wind_poll.h's
     * contract), so decode() must never read index 12 unless type is
     * combined. Use a 13-entry buffer with a nonzero index 12 here
     * specifically to prove it's ignored, not just coincidentally 0. */
    uint16_t raw[13] = {0};
    raw[12] = 999; /* would be wrong if decode() read this for a non-combined type */
    wind_reading_t reading;
    wind_poll_decode(WIND_SENSOR_SPEED, raw, &reading);
    TEST_ASSERT_EQUAL_UINT16(0, reading.dir_raw_adc);
    wind_poll_decode(WIND_SENSOR_DIRECTION, raw, &reading);
    TEST_ASSERT_EQUAL_UINT16(0, reading.dir_raw_adc);
}

/* ── register counts (TDS §2.7/§2.8) — input count is type-dependent
 *    (12 single-sensor, 13 combined, FR-MB27); holding count is uniform
 *    (6, every build) ── */

void test_input_register_count_is_twelve_for_speed_and_direction(void)
{
    TEST_ASSERT_EQUAL_UINT8(12, wind_sensor_input_register_count(WIND_SENSOR_SPEED));
    TEST_ASSERT_EQUAL_UINT8(12, wind_sensor_input_register_count(WIND_SENSOR_DIRECTION));
}

void test_input_register_count_is_thirteen_for_combined(void)
{
    TEST_ASSERT_EQUAL_UINT8(13, wind_sensor_input_register_count(WIND_SENSOR_COMBINED));
}

void test_holding_register_count_is_six(void)
{
    TEST_ASSERT_EQUAL_UINT8(6, wind_sensor_holding_register_count());
}

/* ── config field register mapping (TDS §2.8 — fixed, no device-address
 *    register as of v0.6: FR-MB07/FR-MB26) ── */

void test_config_field_register_mapping(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x0000, wind_config_field_register(WIND_CFG_DIR_OFFSET));
    TEST_ASSERT_EQUAL_HEX16(0x0001, wind_config_field_register(WIND_CFG_MEASUREMENT_WINDOW));
    TEST_ASSERT_EQUAL_HEX16(0x0002, wind_config_field_register(WIND_CFG_AVERAGING_WINDOW));
    TEST_ASSERT_EQUAL_HEX16(0x0003, wind_config_field_register(WIND_CFG_LOW_SPEED_CUTOFF));
    TEST_ASSERT_EQUAL_HEX16(0x0004, wind_config_field_register(WIND_CFG_CALIBRATION_C));
    TEST_ASSERT_EQUAL_HEX16(0x0005, wind_config_field_register(WIND_CFG_PULSES_PER_ROTATION));
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

void test_config_field_encode_calibration_c_scaled_by_1000(void)
{
    /* TDS §2.8 default: 40005 = 980 raw <-> 0.980 m/rotation. */
    TEST_ASSERT_EQUAL_UINT16(980, wind_config_field_encode(WIND_CFG_CALIBRATION_C, 0.980f));
    TEST_ASSERT_EQUAL_UINT16(6553, wind_config_field_encode(WIND_CFG_CALIBRATION_C, 6.553f)); /* top of valid range */
}

void test_config_field_encode_pulses_per_rotation_unscaled(void)
{
    TEST_ASSERT_EQUAL_UINT16(1, wind_config_field_encode(WIND_CFG_PULSES_PER_ROTATION, 1.0f));
    TEST_ASSERT_EQUAL_UINT16(4, wind_config_field_encode(WIND_CFG_PULSES_PER_ROTATION, 4.0f));
}

void test_config_field_decode_round_trip(void)
{
    uint16_t raw = wind_config_field_encode(WIND_CFG_DIR_OFFSET, 45.6f);
    float decoded = wind_config_field_decode(WIND_CFG_DIR_OFFSET, raw);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 45.6f, decoded);

    uint16_t raw_cutoff = wind_config_field_encode(WIND_CFG_LOW_SPEED_CUTOFF, 3.5f);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 3.5f, wind_config_field_decode(WIND_CFG_LOW_SPEED_CUTOFF, raw_cutoff));

    uint16_t raw_calib = wind_config_field_encode(WIND_CFG_CALIBRATION_C, 0.980f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.980f, wind_config_field_decode(WIND_CFG_CALIBRATION_C, raw_calib));
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

/* ── wind interface decode (TDS §2.7, device/system registers) ── */

void test_wind_interface_decode_all_clear_status(void)
{
    uint16_t status_block[5] = {0x0000, 0, 0, 0, 0};
    wind_interface_status_t st;
    wind_interface_decode(status_block, &st);
    TEST_ASSERT_EQUAL_UINT16(0x0000, st.status_flags);
    TEST_ASSERT_FALSE(st.status_measurement_incomplete);
    TEST_ASSERT_FALSE(st.status_avg_not_filled);
    TEST_ASSERT_FALSE(st.status_dir_fault);
}

void test_wind_interface_decode_measurement_incomplete_bit_only(void)
{
    uint16_t status_block[5] = {WIND_STATUS_MEASUREMENT_INCOMPLETE, 0, 0, 0, 0};
    wind_interface_status_t st;
    wind_interface_decode(status_block, &st);
    TEST_ASSERT_TRUE(st.status_measurement_incomplete);
    TEST_ASSERT_FALSE(st.status_avg_not_filled);
    TEST_ASSERT_FALSE(st.status_dir_fault);
}

void test_wind_interface_decode_avg_not_filled_bit_only(void)
{
    uint16_t status_block[5] = {WIND_STATUS_AVG_NOT_FILLED, 0, 0, 0, 0};
    wind_interface_status_t st;
    wind_interface_decode(status_block, &st);
    TEST_ASSERT_FALSE(st.status_measurement_incomplete);
    TEST_ASSERT_TRUE(st.status_avg_not_filled);
    TEST_ASSERT_FALSE(st.status_dir_fault);
}

void test_wind_interface_decode_dir_fault_bit_only(void)
{
    uint16_t status_block[5] = {WIND_STATUS_DIR_FAULT, 0, 0, 0, 0};
    wind_interface_status_t st;
    wind_interface_decode(status_block, &st);
    TEST_ASSERT_FALSE(st.status_measurement_incomplete);
    TEST_ASSERT_FALSE(st.status_avg_not_filled);
    TEST_ASSERT_TRUE(st.status_dir_fault);
}

void test_wind_interface_decode_all_three_status_bits_set(void)
{
    uint16_t status_block[5] = {
        (uint16_t)(WIND_STATUS_MEASUREMENT_INCOMPLETE | WIND_STATUS_AVG_NOT_FILLED | WIND_STATUS_DIR_FAULT),
        0, 0, 0, 0
    };
    wind_interface_status_t st;
    wind_interface_decode(status_block, &st);
    TEST_ASSERT_EQUAL_UINT16(0x0007, st.status_flags);
    TEST_ASSERT_TRUE(st.status_measurement_incomplete);
    TEST_ASSERT_TRUE(st.status_avg_not_filled);
    TEST_ASSERT_TRUE(st.status_dir_fault);
}

void test_wind_interface_decode_unrelated_bit_does_not_leak_into_bools(void)
{
    /* TDS §2.7 says bits 3-15 are reserved (always 0) in practice, but
     * decode() must only ever consult bits 0/1/2 for the three bools
     * regardless of what a slave actually puts on the wire. */
    uint16_t status_block[5] = {0x00F8, 0, 0, 0, 0}; /* bits 3-7 set, bits 0-2 clear */
    wind_interface_status_t st;
    wind_interface_decode(status_block, &st);
    TEST_ASSERT_EQUAL_UINT16(0x00F8, st.status_flags); /* raw value still passed through verbatim */
    TEST_ASSERT_FALSE(st.status_measurement_incomplete);
    TEST_ASSERT_FALSE(st.status_avg_not_filled);
    TEST_ASSERT_FALSE(st.status_dir_fault);
}

void test_wind_interface_decode_identification_known_combined_value(void)
{
    /* 0x0301 is this project's own documented combined-build identity
     * value (see test_wind_poll_decodes_combined_fields_from_full_thirteen_register_block's
     * raw[6] above) — build_type 0x03 (combined), fw_version 0x01. */
    uint16_t status_block[5] = {0, 0x0301, 0, 0, 0};
    wind_interface_status_t st;
    wind_interface_decode(status_block, &st);
    TEST_ASSERT_EQUAL_UINT8(0x03, st.build_type);
    TEST_ASSERT_EQUAL_UINT8(0x01, st.fw_version);
}

void test_wind_interface_decode_identification_fw_version_only_nonzero(void)
{
    /* Proves the hi/lo split isn't accidentally swapped: fw_version alone
     * nonzero, build_type must stay 0. */
    uint16_t status_block[5] = {0, 0x0042, 0, 0, 0};
    wind_interface_status_t st;
    wind_interface_decode(status_block, &st);
    TEST_ASSERT_EQUAL_UINT8(0x00, st.build_type);
    TEST_ASSERT_EQUAL_UINT8(0x42, st.fw_version);
}

void test_wind_interface_decode_identification_build_type_only_nonzero(void)
{
    /* And the reverse: build_type alone nonzero, fw_version must stay 0. */
    uint16_t status_block[5] = {0, 0x0200, 0, 0, 0};
    wind_interface_status_t st;
    wind_interface_decode(status_block, &st);
    TEST_ASSERT_EQUAL_UINT8(0x02, st.build_type);
    TEST_ASSERT_EQUAL_UINT8(0x00, st.fw_version);
}

void test_wind_interface_decode_diagnostic_counters_pass_through_unscaled(void)
{
    /* Values >255 to catch an accidental uint8_t-width truncation
     * somewhere in the decode path — these are uint16_t fields
     * (wind_poll.h). */
    uint16_t status_block[5] = {0, 0, 40000, 1000, 500};
    wind_interface_status_t st;
    wind_interface_decode(status_block, &st);
    TEST_ASSERT_EQUAL_UINT16(40000, st.uptime_s);
    TEST_ASSERT_EQUAL_UINT16(1000, st.crc_error_count);
    TEST_ASSERT_EQUAL_UINT16(500, st.served_request_count);
}

void test_wind_interface_decode_reads_only_its_own_window_when_passed_pointer_into_larger_buffer(void)
{
    /* wind_poll.h's own contract: status_block may be &raw[5] into a
     * larger buffer that started at 0x0000 — decode() must read exactly
     * the 5 elements starting at the pointer it's given, nothing before
     * or after. Same spirit/technique as
     * test_wind_poll_dir_raw_adc_is_zero_for_non_combined_types above:
     * seed the out-of-window slots with sentinel values that would
     * produce obviously wrong output if leaked in, and the in-window
     * slots with the real values under test. */
    uint16_t raw[13];
    raw[0] = 9999; raw[1] = 9999; raw[2] = 9999; raw[3] = 9999; raw[4] = 9999; /* before the window */
    raw[5] = 0x0002;  /* status_flags: avg_not_filled only */
    raw[6] = 0x0301;  /* identification: build_type 3, fw_version 1 */
    raw[7] = 12345;   /* uptime_s */
    raw[8] = 111;     /* crc_error_count */
    raw[9] = 222;     /* served_request_count */
    raw[10] = 8888; raw[11] = 8888; raw[12] = 8888; /* after the window */

    wind_interface_status_t st;
    wind_interface_decode(&raw[5], &st);

    TEST_ASSERT_EQUAL_UINT16(0x0002, st.status_flags);
    TEST_ASSERT_FALSE(st.status_measurement_incomplete);
    TEST_ASSERT_TRUE(st.status_avg_not_filled);
    TEST_ASSERT_FALSE(st.status_dir_fault);
    TEST_ASSERT_EQUAL_UINT8(0x03, st.build_type);
    TEST_ASSERT_EQUAL_UINT8(0x01, st.fw_version);
    TEST_ASSERT_EQUAL_UINT16(12345, st.uptime_s);
    TEST_ASSERT_EQUAL_UINT16(111, st.crc_error_count);
    TEST_ASSERT_EQUAL_UINT16(222, st.served_request_count);
}

/* ── wind_build_type_name() (TDS FR-S32) ── */

void test_wind_build_type_name_speed(void)
{
    TEST_ASSERT_EQUAL_STRING("wind_speed", wind_build_type_name(0x01));
}

void test_wind_build_type_name_direction(void)
{
    TEST_ASSERT_EQUAL_STRING("wind_direction", wind_build_type_name(0x02));
}

void test_wind_build_type_name_combined(void)
{
    TEST_ASSERT_EQUAL_STRING("wind_combined", wind_build_type_name(0x03));
}

void test_wind_build_type_name_unknown_zero(void)
{
    TEST_ASSERT_EQUAL_STRING("unknown", wind_build_type_name(0x00));
}

void test_wind_build_type_name_unknown_max(void)
{
    TEST_ASSERT_EQUAL_STRING("unknown", wind_build_type_name(0xFF));
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
    RUN_TEST(test_wind_poll_decodes_combined_fields_from_full_thirteen_register_block);
    RUN_TEST(test_wind_poll_direction_fault_sentinel_also_applies_on_combined_build);
    RUN_TEST(test_wind_poll_dir_raw_adc_is_zero_for_non_combined_types);
    RUN_TEST(test_input_register_count_is_twelve_for_speed_and_direction);
    RUN_TEST(test_input_register_count_is_thirteen_for_combined);
    RUN_TEST(test_holding_register_count_is_six);
    RUN_TEST(test_config_field_register_mapping);
    RUN_TEST(test_config_field_encode_dir_offset_scaled_by_10);
    RUN_TEST(test_config_field_encode_low_speed_cutoff_scaled_by_10);
    RUN_TEST(test_config_field_encode_windows_unscaled);
    RUN_TEST(test_config_field_encode_calibration_c_scaled_by_1000);
    RUN_TEST(test_config_field_encode_pulses_per_rotation_unscaled);
    RUN_TEST(test_config_field_decode_round_trip);
    RUN_TEST(test_interval_elapsed_false_before_interval);
    RUN_TEST(test_interval_elapsed_true_at_or_after_interval);
    RUN_TEST(test_wind_interface_decode_all_clear_status);
    RUN_TEST(test_wind_interface_decode_measurement_incomplete_bit_only);
    RUN_TEST(test_wind_interface_decode_avg_not_filled_bit_only);
    RUN_TEST(test_wind_interface_decode_dir_fault_bit_only);
    RUN_TEST(test_wind_interface_decode_all_three_status_bits_set);
    RUN_TEST(test_wind_interface_decode_unrelated_bit_does_not_leak_into_bools);
    RUN_TEST(test_wind_interface_decode_identification_known_combined_value);
    RUN_TEST(test_wind_interface_decode_identification_fw_version_only_nonzero);
    RUN_TEST(test_wind_interface_decode_identification_build_type_only_nonzero);
    RUN_TEST(test_wind_interface_decode_diagnostic_counters_pass_through_unscaled);
    RUN_TEST(test_wind_interface_decode_reads_only_its_own_window_when_passed_pointer_into_larger_buffer);
    RUN_TEST(test_wind_build_type_name_speed);
    RUN_TEST(test_wind_build_type_name_direction);
    RUN_TEST(test_wind_build_type_name_combined);
    RUN_TEST(test_wind_build_type_name_unknown_zero);
    RUN_TEST(test_wind_build_type_name_unknown_max);
    return UNITY_END();
}
