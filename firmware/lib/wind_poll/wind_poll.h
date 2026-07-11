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
 * Three build variants exist — wind speed, wind direction, and combined
 * (both sensors behind one slave address) — same PCB/firmware source, but
 * a compile-time define picks which one a given build is, with its own
 * slave address (speed: 30/35 jumpered; direction: 31/36; combined: 32/37
 * — TDS FR-S03). All three implement the *same* register layout at the
 * same addresses as far as it goes (FR-MB27) — a register the active
 * build's sensor doesn't use simply reads 0 — but the combined build's
 * input map is one register longer than the single-sensor builds' (13 vs
 * 12: TDS §2.7 adds 30013 for the combined build's direction raw ADC,
 * since 30005 is taken by the speed pulse count there). The holding map is
 * uniformly 6 registers on every build (added 2026-07-11 for the
 * anemometer calibration pair, 40005/40006 — inert, but still
 * readable/writable, on a direction-only build per FR-MB27/FR-S40).
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Which physical sensor(s) a given build reports (TDS FR-S03) —
 * picked at compile time, not something read off the wire (there is no
 * type register, though 0x0006/30007's high byte identifies it, TDS
 * FR-S32). Passed to wind_poll_decode() to select which input-register
 * fields are meaningful for the current build; see wind_reading_t.
 */
typedef enum {
    WIND_SENSOR_SPEED = 0,   /**< Anemometer-only build, slave address 30 (or 35 jumpered). */
    WIND_SENSOR_DIRECTION,   /**< Wind-vane-only build, slave address 31 (or 36 jumpered). */
    WIND_SENSOR_COMBINED,    /**< Both sensors behind one slave address, 32 (or 37 jumpered). 13-register input map, TDS §2.7. */
} wind_sensor_type_t;

/**
 * @brief One decoded snapshot of the DUT's input register block (TDS
 * §2.7) — a single FC04 read starting at raw 0x0000, 12 registers on the
 * single-sensor builds or 13 on combined (FR-MB27; see
 * wind_sensor_input_register_count()). Only the fields meaningful for
 * @p type passed to wind_poll_decode() are populated from real data; the
 * other fields come off the wire as 0 (the DUT reports 0 for a sensor it
 * doesn't have) and are left that way here, not specially zeroed — same
 * convention, no divergence to track.
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
    uint16_t raw_diagnostic;      /**< 0x0004 (30005), unscaled, build-specific: pulse count last window (speed or combined build) or last raw 10-bit ADC conversion (direction-only build), TDS §2.7. */
    uint16_t seconds_since_pulse; /**< 0x000A (30011), unscaled seconds since the last anemometer edge — speed/combined only, 0 on direction-only (TDS FR-S36). */
    float    gust_ms;             /**< 0x000B (30012), raw/10 — max single-window speed within the current averaging window — speed/combined only, 0 on direction-only (TDS FR-S37). */
    bool     dir_fault;           /**< True when 0x0000/0x0002 read the TDS FR-S38 fault sentinel (65535) — a floating direction-sensor wiper for >2s. Always false on a speed-only build (those registers read 0, not 65535, there). */
    uint16_t dir_raw_adc;         /**< 0x000C (30013), unscaled 10-bit ADC conversion — combined build ONLY (the register doesn't exist at all on a single-sensor build, TDS §2.7). On a direction-only build the equivalent value is raw_diagnostic instead, since 30013 isn't mapped there. */
} wind_reading_t;

/**
 * @brief Number of input registers the DUT implements for @p type (TDS
 * §2.7) — 12 (raw 0x0000-0x000B) on the single-sensor builds, 13
 * (0x0000-0x000C) on combined, which adds 30013 (FR-MB27). Read as one
 * FC04 block starting at 0x0000; a single-sensor build's map edge is
 * 0x000C, so requesting 13 there would fail with exception 02 (FR-MB13) —
 * the caller must size the request to @p type, not assume 13 universally.
 * @param type Which build to report the count for.
 * @return 13 for WIND_SENSOR_COMBINED, 12 otherwise.
 */
uint8_t wind_sensor_input_register_count(wind_sensor_type_t type);

/**
 * @brief Decode an FC04 read (TDS §2.7, raw 0x0000-0x000B or -0x000C) into
 * engineering units for @p type. The wire layout is identical as far as
 * the single-sensor builds' 12 registers go (FR-MB27); combined adds a
 * 13th (dir_raw_adc, 0x000C) that only wind_poll_decode() reads when
 * @p type is WIND_SENSOR_COMBINED — see wind_reading_t's field comments.
 * @param type          Which physical build this is; selects which fields
 *                       are meaningful (dir_fault on direction/combined;
 *                       dir_raw_adc, and reading raw_registers[12] at all,
 *                       on combined only).
 * @param raw_registers Raw register values, in wire order, as read by one
 *                       FC04 starting at 0x0000 — 12 entries are read for
 *                       WIND_SENSOR_SPEED/WIND_SENSOR_DIRECTION, 13 for
 *                       WIND_SENSOR_COMBINED (index 12 is never touched
 *                       otherwise, so a 12-entry buffer is safe to pass
 *                       for those two types). Caller-owned.
 * @param out           Destination for the decoded reading. Caller-owned;
 *                       fully overwritten (every field), not merged with
 *                       any prior contents.
 */
void wind_poll_decode(wind_sensor_type_t type, const uint16_t raw_registers[13], wind_reading_t *out);

/**
 * @brief Holding register fields (TDS §2.8) — 6 registers, same addresses,
 * on every build (FR-MB27): the last two (calibration/pulses-per-rotation,
 * added 2026-07-11) are inert on a direction-only build but still exist,
 * persist, and are readable/writable there (TDS FR-S40) — same "shared map,
 * build-inappropriate fields just don't do anything" convention the input
 * registers already use. There is no device-address register as of TDS
 * v0.6: the Modbus address is hardware-configured only (build define + PC4
 * solder jumper, TDS FR-S03/FR-MB07) and cannot be read or written over
 * Modbus — a write to the old address register's former address now
 * correctly returns exception 02 (illegal data address) on real DUT
 * firmware.
 */
typedef enum {
    WIND_CFG_DIR_OFFSET = 0,       /**< Holding reg 0x0000 (40001), 0.1° units, valid range 0-3599, default 0. */
    WIND_CFG_MEASUREMENT_WINDOW,   /**< Holding reg 0x0001 (40002), ms, valid range 100-60000, default 1000. */
    WIND_CFG_AVERAGING_WINDOW,     /**< Holding reg 0x0002 (40003), s, valid range 1-600 (also subject to the TDS FR-S31 cross-register constraint with the measurement window), default 10. */
    WIND_CFG_LOW_SPEED_CUTOFF,     /**< Holding reg 0x0003 (40004), 0.1 m/s units, valid range 0-50, default 4. */
    WIND_CFG_CALIBRATION_C,        /**< Holding reg 0x0004 (40005), anemometer calibration factor C, 0.001 m/rotation units, valid range 1-6553, default 980 (TDS FR-S25/FR-S40). Inert on a direction-only build. */
    WIND_CFG_PULSES_PER_ROTATION,  /**< Holding reg 0x0005 (40006), anemometer pulses per rotation, unscaled integer, valid range 1-1000, default 1 (TDS FR-S40). Inert on a direction-only build. */
} wind_config_field_t;

/**
 * @brief Raw 0-based holding register address for @p field (TDS §2.8) —
 * a fixed mapping, identical on every build (FR-MB27), unlike the
 * pre-TDS-v0.6 design this replaced where the address depended on sensor
 * type.
 * @param field Which config field to map.
 * @return Raw 0-based holding register address (0x0000-0x0005) for
 *         @p field; 0xFFFF if @p field is not a valid wind_config_field_t
 *         value (defensive — callers should never pass one).
 */
uint16_t wind_config_field_register(wind_config_field_t field);

/**
 * @brief Number of holding registers the DUT implements (TDS §2.8) —
 * always 6 (raw 0x0000-0x0005), identical on every build (grew from 4
 * when the anemometer calibration pair was added, 2026-07-11).
 * @return Always 6 — a function rather than a bare constant so callers
 *         (wind_poll_task.cpp) don't hardcode the register count either.
 */
uint8_t wind_sensor_holding_register_count(void);

/**
 * @brief Engineering-unit value -> raw register value to write via FC06 (TDS §2.8 scaling).
 * @param field Which config field @p value belongs to; selects the scale
 *              factor (×10 for DIR_OFFSET/LOW_SPEED_CUTOFF, ×1000 for
 *              CALIBRATION_C, unscaled for the two window fields and
 *              PULSES_PER_ROTATION).
 * @param value Engineering-unit value to encode (e.g. degrees, m/s, ms, s,
 *              m/rotation, pulses/rotation — see wind_config_field_t's
 *              field comments for units/range).
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
