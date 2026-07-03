/**
 * @file wind_poll.h
 * @brief Wind Test — the testable decode/config core (TASK-WIND,
 *        design/completeRealisationPlan.md).
 *
 * Register map and scaling from the DUT's own Technical Design
 * Specification (`windmeters-modbus-interface/design/TDS.md` §2.7/§2.8,
 * v0.6) — this file has no queue/task/UART calls, so it's host-testable;
 * the actual periodic FC04 poll and FC06 config writes through
 * modbus_master_task's queue are wind_poll_task.cpp (Arduino-only).
 *
 * Wind speed and wind direction are two physically separate units, not one
 * combined device — same PCB/firmware source, but a compile-time define
 * picks which sensor a given build reports, with its own slave address
 * (speed: 30, or 35 jumpered; direction: 31, or 36 jumpered — TDS FR-S03).
 * As of TDS v0.6, both builds implement an *identical* register map at
 * identical addresses (FR-MB27) — a register the active sensor doesn't use
 * simply reads 0, rather than the two builds having different maps. That
 * replaced an earlier (pre-TDS) assumption in this file that each type had
 * its own compact, different-sized register block — corrected 2026-07-02
 * once the DUT's TDS matured enough to specify this precisely.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Which physical sensor a given build reports (TDS FR-S03) — picked
 * at compile time, not something read off the wire (there is no type
 * register). Passed to wind_poll_decode() to select which input-register
 * fields are meaningful for the current build; see wind_reading_t.
 */
typedef enum {
    WIND_SENSOR_SPEED = 0,   /**< Anemometer build, slave address 30 (or 35 jumpered). */
    WIND_SENSOR_DIRECTION,   /**< Wind-vane build, slave address 31 (or 36 jumpered). */
} wind_sensor_type_t;

/**
 * @brief One decoded snapshot of the DUT's input register block (TDS
 * §2.7) — a single 12-register FC04 read starting at raw 0x0000, wire
 * layout identical on both builds (FR-MB27). Only the fields meaningful
 * for @p type passed to wind_poll_decode() are populated from real data;
 * the other type's fields come off the wire as 0 (the DUT reports 0 for a
 * sensor it doesn't have) and are left that way here, not specially
 * zeroed — same convention, no divergence to track.
 *
 * Device-level protocol diagnostics also live in this input block (status
 * flags 0x0005, identification 0x0006, uptime 0x0007, CRC-error count
 * 0x0008, served-request count 0x0009 — TDS §2.7) but aren't decoded here:
 * they're not wind measurement data, and Register Explorer already reaches
 * them by raw address without needing dedicated decode/GUI support.
 */
typedef struct {
    float    dir_instant_deg;     /**< 0x0000 (30001), raw/10. 0 on a speed build. */
    float    speed_instant_ms;    /**< 0x0001 (30002), raw/10. 0 on a direction build. */
    float    dir_avg_deg;         /**< 0x0002 (30003), raw/10. 0 on a speed build. */
    float    speed_avg_ms;        /**< 0x0003 (30004), raw/10. 0 on a direction build. */
    uint16_t raw_diagnostic;      /**< 0x0004 (30005), unscaled — pulse count last window (speed build) or last raw 10-bit ADC conversion (direction build), TDS §2.7. */
    uint16_t seconds_since_pulse; /**< 0x000A (30011), unscaled seconds since the last anemometer edge — speed build only, 0 on direction (TDS FR-S36). */
    float    gust_ms;             /**< 0x000B (30012), raw/10 — max single-window speed within the current averaging window — speed build only, 0 on direction (TDS FR-S37). */
    bool     dir_fault;           /**< True when 0x0000/0x0002 read the TDS FR-S38 fault sentinel (65535) — a floating direction-sensor wiper for >2s. Always false on a speed build (those registers read 0, not 65535, there). */
} wind_reading_t;

/**
 * @brief Number of input registers the DUT implements (TDS §2.7) — always
 * 12 (raw 0x0000-0x000B), identical on both builds (FR-MB27). Read as one
 * FC04 block starting at 0x0000.
 * @return Always 12 — a function rather than a bare constant so callers
 *         (wind_poll_task.cpp) don't hardcode the register count either.
 */
uint8_t wind_sensor_input_register_count(void);

/**
 * @brief Decode a 12-register FC04 read (TDS §2.7, raw 0x0000-0x000B)
 * into engineering units for @p type. The wire layout is identical
 * regardless of type (FR-MB27) — only which fields are meaningful for the
 * caller differs; see wind_reading_t's field comments.
 * @param type          Which physical sensor this build reports; selects
 *                       whether dir_fault can be set (direction only).
 * @param raw_registers Exactly 12 raw register values, in wire order,
 *                       as read by one FC04 starting at 0x0000. Caller-owned.
 * @param out           Destination for the decoded reading. Caller-owned;
 *                       fully overwritten (every field), not merged with
 *                       any prior contents.
 */
void wind_poll_decode(wind_sensor_type_t type, const uint16_t raw_registers[12], wind_reading_t *out);

/**
 * @brief Holding register fields (TDS §2.8) — 4 registers, same
 * addresses, on both builds (FR-MB27). There is no device-address
 * register as of TDS v0.6: the Modbus address is hardware-configured only
 * (build define + PC4 solder jumper, TDS FR-S03/FR-MB07) and cannot be
 * read or written over Modbus — a write to the old address register's
 * former address now correctly returns exception 02 (illegal data
 * address) on real DUT firmware.
 */
typedef enum {
    WIND_CFG_DIR_OFFSET = 0,     /**< Holding reg 0x0000 (40001), 0.1° units, valid range 0-3599, default 0. */
    WIND_CFG_MEASUREMENT_WINDOW, /**< Holding reg 0x0001 (40002), ms, valid range 100-60000, default 1000. */
    WIND_CFG_AVERAGING_WINDOW,   /**< Holding reg 0x0002 (40003), s, valid range 1-600 (also subject to the TDS FR-S31 cross-register constraint with the measurement window), default 10. */
    WIND_CFG_LOW_SPEED_CUTOFF,   /**< Holding reg 0x0003 (40004), 0.1 m/s units, valid range 0-50, default 4. */
} wind_config_field_t;

/**
 * @brief Raw 0-based holding register address for @p field (TDS §2.8) —
 * a fixed mapping, identical on both builds (FR-MB27), unlike the
 * pre-TDS-v0.6 design this replaced where the address depended on sensor
 * type.
 * @param field Which config field to map.
 * @return Raw 0-based holding register address (0x0000-0x0003) for
 *         @p field; 0xFFFF if @p field is not a valid wind_config_field_t
 *         value (defensive — callers should never pass one).
 */
uint16_t wind_config_field_register(wind_config_field_t field);

/**
 * @brief Number of holding registers the DUT implements (TDS §2.8) —
 * always 4 (raw 0x0000-0x0003), identical on both builds.
 * @return Always 4 — a function rather than a bare constant so callers
 *         (wind_poll_task.cpp) don't hardcode the register count either.
 */
uint8_t wind_sensor_holding_register_count(void);

/**
 * @brief Engineering-unit value -> raw register value to write via FC06 (TDS §2.8 scaling).
 * @param field Which config field @p value belongs to; selects the scale
 *              factor (×10 for DIR_OFFSET/LOW_SPEED_CUTOFF, unscaled for
 *              the two window fields).
 * @param value Engineering-unit value to encode (e.g. degrees, m/s, ms, s
 *              — see wind_config_field_t's field comments for units/range).
 * @return Raw register value ready to write via FC06. Not range-checked
 *         against the field's valid range (TDS §2.8) — the caller (or the
 *         DUT itself, via a Modbus exception) is responsible for that.
 */
uint16_t wind_config_field_encode(wind_config_field_t field, float value);

/**
 * @brief Raw register value (from an FC03 read) -> engineering-unit value.
 * @param field     Which config field @p raw_value belongs to; selects the
 *                  scale factor, the inverse of wind_config_field_encode().
 * @param raw_value Raw register value as read via FC03.
 * @return Decoded engineering-unit value.
 */
float wind_config_field_decode(wind_config_field_t field, uint16_t raw_value);

/**
 * @brief Has @p interval_ms elapsed since the last poll?
 *
 * Unsigned subtraction is correct modulo 2^32 across a millis() wraparound
 * — same reasoning as ntp_manager_millis_to_epoch().
 *
 * @param now_ms       Caller's current time.
 * @param last_poll_ms Time of the last successful poll.
 * @param interval_ms  Configured poll interval.
 * @return true if (now_ms - last_poll_ms) >= interval_ms; false otherwise.
 */
bool wind_poll_interval_elapsed(uint32_t now_ms, uint32_t last_poll_ms, uint32_t interval_ms);
