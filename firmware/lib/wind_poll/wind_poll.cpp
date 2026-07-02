#include "wind_poll.h"
#include <string.h>

void wind_poll_decode_speed(const uint16_t raw_registers[3], wind_reading_t *out)
{
    memset(out, 0, sizeof(*out));
    out->speed_instant_ms = (float)raw_registers[0] / 10.0f;
    out->speed_avg_ms     = (float)raw_registers[1] / 10.0f;
    out->raw_pulses       = raw_registers[2];
}

void wind_poll_decode_direction(const uint16_t raw_registers[2], wind_reading_t *out)
{
    memset(out, 0, sizeof(*out));
    out->dir_instant_deg = (float)raw_registers[0] / 10.0f;
    out->dir_avg_deg     = (float)raw_registers[1] / 10.0f;
}

uint8_t wind_sensor_input_register_count(wind_sensor_type_t type)
{
    return (type == WIND_SENSOR_SPEED) ? 3u : 2u;
}

uint16_t wind_config_field_register(wind_sensor_type_t type, wind_config_field_t field)
{
    switch (field) {
        case WIND_CFG_DEVICE_ADDR:
            return 0x0000;
        case WIND_CFG_DIR_OFFSET:
            return (type == WIND_SENSOR_DIRECTION) ? 0x0001 : 0xFFFF;
        case WIND_CFG_MEASUREMENT_WINDOW:
            return (type == WIND_SENSOR_SPEED) ? 0x0001 : 0xFFFF;
        case WIND_CFG_AVERAGING_WINDOW:
            return 0x0002;
        default:
            return 0xFFFF;
    }
}

uint8_t wind_sensor_holding_register_count(wind_sensor_type_t /*type*/)
{
    return 3u; /* both types: device_addr, {dir_offset|measurement_window}, averaging_window */
}

uint16_t wind_config_field_encode(wind_config_field_t field, float value)
{
    if (field == WIND_CFG_DIR_OFFSET) {
        return (uint16_t)(value * 10.0f + 0.5f); /* ×10, rounded */
    }
    return (uint16_t)value; /* device addr / measurement window / averaging window: unscaled */
}

float wind_config_field_decode(wind_config_field_t field, uint16_t raw_value)
{
    if (field == WIND_CFG_DIR_OFFSET) {
        return (float)raw_value / 10.0f;
    }
    return (float)raw_value;
}

bool wind_poll_interval_elapsed(uint32_t now_ms, uint32_t last_poll_ms, uint32_t interval_ms)
{
    return (now_ms - last_poll_ms) >= interval_ms;
}
