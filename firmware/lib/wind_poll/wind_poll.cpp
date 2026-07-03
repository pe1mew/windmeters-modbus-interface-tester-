/**
 * @file wind_poll.cpp
 * @brief Wind Test decode/config core — implementation. See wind_poll.h
 * for the public API contract and the TDS §2.7/§2.8 register map this
 * file encodes/decodes against.
 */
#include "wind_poll.h"

#define WIND_FAULT_RAW 65535u /**< TDS FR-S38 sensor-fault sentinel. */

uint8_t wind_sensor_input_register_count(void)
{
    return 12u;
}

void wind_poll_decode(wind_sensor_type_t type, const uint16_t raw_registers[12], wind_reading_t *out)
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
    out->dir_fault           = (type == WIND_SENSOR_DIRECTION) &&
                                (raw_dir_instant == WIND_FAULT_RAW || raw_dir_avg == WIND_FAULT_RAW);
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
        default:
            return 0xFFFF;
    }
}

uint8_t wind_sensor_holding_register_count(void)
{
    return 4u; /* dir_offset, measurement_window, averaging_window, low_speed_cutoff — identical on both builds, TDS FR-MB27 */
}

uint16_t wind_config_field_encode(wind_config_field_t field, float value)
{
    if (field == WIND_CFG_DIR_OFFSET || field == WIND_CFG_LOW_SPEED_CUTOFF) {
        return (uint16_t)(value * 10.0f + 0.5f); /* ×10, rounded */
    }
    return (uint16_t)value; /* measurement window / averaging window: unscaled */
}

float wind_config_field_decode(wind_config_field_t field, uint16_t raw_value)
{
    if (field == WIND_CFG_DIR_OFFSET || field == WIND_CFG_LOW_SPEED_CUTOFF) {
        return (float)raw_value / 10.0f;
    }
    return (float)raw_value;
}

bool wind_poll_interval_elapsed(uint32_t now_ms, uint32_t last_poll_ms, uint32_t interval_ms)
{
    return (now_ms - last_poll_ms) >= interval_ms;
}
