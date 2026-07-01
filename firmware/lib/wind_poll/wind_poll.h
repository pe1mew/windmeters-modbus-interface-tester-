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
 */
#pragma once

#include <stdint.h>

typedef struct {
    float    dir_instant_deg;   /**< Input reg 0x0000, raw/10. */
    float    speed_instant_ms;  /**< Input reg 0x0001, raw/10. */
    float    dir_avg_deg;       /**< Input reg 0x0002, raw/10. */
    float    speed_avg_ms;      /**< Input reg 0x0003, raw/10. */
    uint16_t raw_pulses;        /**< Input reg 0x0004, unscaled. */
} wind_reading_t;

/**
 * @brief Decode a 5-register FC04 read (0x0000-0x0004) into engineering units.
 * @param raw_registers Array of exactly 5 values, in register order.
 */
void wind_poll_decode(const uint16_t raw_registers[5], wind_reading_t *out);

/** @brief Which of the 4 holding registers (raw address) backs this config field. */
typedef enum {
    WIND_CFG_DEVICE_ADDR = 0,      /**< Holding reg 0x0000, unscaled, 1-247. */
    WIND_CFG_DIR_OFFSET,           /**< Holding reg 0x0001, ×10, degrees. */
    WIND_CFG_MEASUREMENT_WINDOW,   /**< Holding reg 0x0002, unscaled, ms. */
    WIND_CFG_AVERAGING_WINDOW,     /**< Holding reg 0x0003, unscaled, seconds. */
} wind_config_field_t;

/** @brief Raw 0-based holding register address for @p field. */
uint16_t wind_config_field_register(wind_config_field_t field);

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
