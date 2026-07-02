/**
 * @file wind_poll.h
 * @brief Wind Test — the testable decode/config core (TASK-WIND,
 *        design/completeRealisationPlan.md).
 *
 * Register map and scaling from design/scratchbook.md §5 — raw addresses,
 * ×10 (NOT the S200's ×1000, see the scaling gotcha there). This file has
 * no queue/task/UART calls, so it's host-testable; the actual periodic
 * FC04 poll and FC06 config writes through modbus_master_task's queue are
 * wind_poll_task.cpp (Arduino-only).
 *
 * Wind speed and wind direction are two physically separate units, not one
 * combined device — same PCB/firmware source, but a compile-time define
 * picks which sensor a given build reports
 * (windmeters-modbus-interface/design/scratchBook.md "Modbus configuration"),
 * with its own slave address (speed: 30, or 35 jumpered; direction: 31, or
 * 36 jumpered) and its own compact register numbering starting at 0x0000.
 * Every function below that touches registers takes a wind_sensor_type_t so
 * it reads/writes only the registers that type's firmware actually has.
 */
#pragma once

#include <stdint.h>

typedef enum {
    WIND_SENSOR_SPEED = 0,
    WIND_SENSOR_DIRECTION,
} wind_sensor_type_t;

typedef struct {
    float    dir_instant_deg;   /**< WindDirection input reg 0x0000, raw/10. Unused (0) for a WIND_SENSOR_SPEED reading. */
    float    speed_instant_ms;  /**< WindSpeed input reg 0x0000, raw/10. Unused (0) for a WIND_SENSOR_DIRECTION reading. */
    float    dir_avg_deg;       /**< WindDirection input reg 0x0001, raw/10. */
    float    speed_avg_ms;      /**< WindSpeed input reg 0x0001, raw/10. */
    uint16_t raw_pulses;        /**< WindSpeed input reg 0x0002, unscaled. Speed-only — no anemometer pulses on a direction unit. */
} wind_reading_t;

/**
 * @brief Decode a WindSpeed FC04 read (3 registers: speed_instant, speed_avg,
 *        raw_pulses) into engineering units. Leaves the direction fields at 0.
 */
void wind_poll_decode_speed(const uint16_t raw_registers[3], wind_reading_t *out);

/**
 * @brief Decode a WindDirection FC04 read (2 registers: dir_instant, dir_avg)
 *        into engineering units. Leaves the speed/pulse fields at 0.
 */
void wind_poll_decode_direction(const uint16_t raw_registers[2], wind_reading_t *out);

/** @brief Number of FC04 input registers @p type's firmware implements (3 for speed, 2 for direction). */
uint8_t wind_sensor_input_register_count(wind_sensor_type_t type);

/**
 * @brief Which config field exists, shared by both sensor types except
 * where noted — the register *number* still depends on @p type (see
 * wind_config_field_register()).
 */
typedef enum {
    WIND_CFG_DEVICE_ADDR = 0,      /**< Holding reg 0x0000 on both types, unscaled, 1-247. */
    WIND_CFG_DIR_OFFSET,           /**< Holding reg 0x0001, ×10 degrees — WIND_SENSOR_DIRECTION only. */
    WIND_CFG_MEASUREMENT_WINDOW,   /**< Holding reg 0x0001, unscaled ms — WIND_SENSOR_SPEED only. */
    WIND_CFG_AVERAGING_WINDOW,     /**< Holding reg 0x0002 on both types, unscaled seconds. */
} wind_config_field_t;

/**
 * @brief Raw 0-based holding register address for @p field on @p type's
 * firmware, or 0xFFFF if @p field doesn't exist for that type (e.g.
 * WIND_CFG_DIR_OFFSET on a WIND_SENSOR_SPEED device).
 */
uint16_t wind_config_field_register(wind_sensor_type_t type, wind_config_field_t field);

/** @brief Number of holding registers @p type's firmware implements (3 for both types today). */
uint8_t wind_sensor_holding_register_count(wind_sensor_type_t type);

/** @brief Engineering-unit value -> raw register value to write via FC06. */
uint16_t wind_config_field_encode(wind_config_field_t field, float value);

/** @brief Raw register value (from an FC03 read) -> engineering-unit value. */
float wind_config_field_decode(wind_config_field_t field, uint16_t raw_value);

/**
 * @brief Has @p interval_ms elapsed since the last poll?
 *
 * Unsigned subtraction is correct modulo 2^32 across a millis() wraparound
 * — same reasoning as ntp_manager_millis_to_epoch().
 */
bool wind_poll_interval_elapsed(uint32_t now_ms, uint32_t last_poll_ms, uint32_t interval_ms);
