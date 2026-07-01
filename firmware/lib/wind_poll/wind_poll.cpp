#include "wind_poll.h"

void wind_poll_decode(const uint16_t raw_registers[5], wind_reading_t *out)
{
    out->dir_instant_deg  = (float)raw_registers[0] / 10.0f;
    out->speed_instant_ms = (float)raw_registers[1] / 10.0f;
    out->dir_avg_deg      = (float)raw_registers[2] / 10.0f;
    out->speed_avg_ms     = (float)raw_registers[3] / 10.0f;
    out->raw_pulses       = raw_registers[4];
}

uint16_t wind_config_field_register(wind_config_field_t field)
{
    switch (field) {
        case WIND_CFG_DEVICE_ADDR:         return 0x0000;
        case WIND_CFG_DIR_OFFSET:          return 0x0001;
        case WIND_CFG_MEASUREMENT_WINDOW:  return 0x0002;
        case WIND_CFG_AVERAGING_WINDOW:    return 0x0003;
        default:                           return 0xFFFF;
    }
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
