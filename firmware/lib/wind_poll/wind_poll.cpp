/**
 * @file wind_poll.cpp
 * @brief Wind Test decode/config core — implementation. See wind_poll.h
 * for the public API contract and the TDS §2.7/§2.8 register map this
 * file encodes/decodes against.
 */
#include "wind_poll.h"

#define WIND_FAULT_RAW 65535u /**< TDS FR-S38 sensor-fault sentinel. */

uint8_t wind_sensor_input_register_count(wind_sensor_type_t type)
{
    return (type == WIND_SENSOR_COMBINED) ? 13u : 12u;
}

void wind_poll_decode(wind_sensor_type_t type, const uint16_t raw_registers[13], wind_reading_t *out)
{
    uint16_t raw_dir_instant = raw_registers[0];
    uint16_t raw_dir_avg     = raw_registers[2];

    out->dir_instant_deg     = (float)raw_dir_instant / 10.0f;
    out->speed_instant_ms    = (float)raw_registers[1] / 10.0f;
    out->dir_avg_deg         = (float)raw_dir_avg / 10.0f;
    out->speed_avg_ms        = (float)raw_registers[3] / 10.0f;
    out->raw_diagnostic      = raw_registers[4];
    out->seconds_since_pulse = raw_registers[10];
    out->gust_ms             = (float)raw_registers[11] / 10.0f;
    out->dir_fault           = (type == WIND_SENSOR_DIRECTION || type == WIND_SENSOR_COMBINED) &&
                                (raw_dir_instant == WIND_FAULT_RAW || raw_dir_avg == WIND_FAULT_RAW);
    /* raw_registers[12] (30013) only exists on the combined build's map —
     * never read it for the other two types, since callers are allowed to
     * pass a 12-entry buffer for those (wind_poll.h's own contract). */
    out->dir_raw_adc         = (type == WIND_SENSOR_COMBINED) ? raw_registers[12] : 0u;
}

uint16_t wind_config_field_register(wind_config_field_t field)
{
    switch (field) {
        case WIND_CFG_DIR_OFFSET:
            return 0x0000;
        case WIND_CFG_MEASUREMENT_WINDOW:
            return 0x0001;
        case WIND_CFG_AVERAGING_WINDOW:
            return 0x0002;
        case WIND_CFG_LOW_SPEED_CUTOFF:
            return 0x0003;
        case WIND_CFG_CALIBRATION_C:
            return 0x0004;
        case WIND_CFG_PULSES_PER_ROTATION:
            return 0x0005;
        default:
            return 0xFFFF;
    }
}

uint8_t wind_sensor_holding_register_count(void)
{
    return 6u; /* dir_offset, measurement_window, averaging_window, low_speed_cutoff, calibration_c, pulses_per_rotation — identical on every build, TDS FR-MB27 */
}

uint16_t wind_config_field_encode(wind_config_field_t field, float value)
{
    if (field == WIND_CFG_DIR_OFFSET || field == WIND_CFG_LOW_SPEED_CUTOFF) {
        return (uint16_t)(value * 10.0f + 0.5f); /* ×10, rounded */
    }
    if (field == WIND_CFG_CALIBRATION_C) {
        return (uint16_t)(value * 1000.0f + 0.5f); /* ×1000, rounded — TDS §2.8's "0.001 m/rotation" unit */
    }
    return (uint16_t)value; /* measurement window / averaging window / pulses_per_rotation: unscaled */
}

float wind_config_field_decode(wind_config_field_t field, uint16_t raw_value)
{
    if (field == WIND_CFG_DIR_OFFSET || field == WIND_CFG_LOW_SPEED_CUTOFF) {
        return (float)raw_value / 10.0f;
    }
    if (field == WIND_CFG_CALIBRATION_C) {
        return (float)raw_value / 1000.0f;
    }
    return (float)raw_value;
}

bool wind_poll_interval_elapsed(uint32_t now_ms, uint32_t last_poll_ms, uint32_t interval_ms)
{
    return (now_ms - last_poll_ms) >= interval_ms;
}
