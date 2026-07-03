/**
 * @file mb_frame.h
 * @brief Modbus RTU frame-level logic — pure functions, no hardware dependency.
 *
 * CRC16, and FC03/FC04/FC06/FC16 request building + response parsing.
 * Operates entirely on caller-supplied byte buffers — no UART, no timing,
 * no FreeRTOS. This is what makes it host-testable with `pio test -e native`
 * (see design/realisationPlan.md MB-1).
 *
 * @see mb_core.h for the public API that wraps this with actual I/O.
 */
#pragma once

#include <stdint.h>

/** @brief Maximum registers per FC03/FC04 read (Modbus spec limit). */
#define MB_MAX_READ_REGISTERS   125u

/** @brief Maximum registers per FC16 write (Modbus spec limit). */
#define MB_MAX_WRITE_REGISTERS  123u

/** @brief Buffer size safely fitting the largest RTU frame this project uses. */
#define MB_MAX_FRAME_LEN        256u

/**
 * @brief Result of parsing a response frame.
 */
typedef enum {
    MB_FRAME_OK = 0,        /**< Frame is well-formed and matches the request. */
    MB_FRAME_ERR_CRC,       /**< CRC16 did not match. */
    MB_FRAME_ERR_EXCEPTION, /**< Slave returned a Modbus exception response. */
    MB_FRAME_ERR_FRAMING,   /**< Wrong length, function code, or address echo. */
} mb_frame_status_t;

/**
 * @brief Compute the Modbus RTU CRC16.
 *
 * Polynomial 0xA001 (reflected 0x8005), initial value 0xFFFF — the standard
 * Modbus CRC, verified against the same reference vectors used by
 * greenhouse-Controller's drivers/modBus/test/test_modbus_rtu.cpp.
 *
 * @param buf Data bytes (request/response payload, excluding the CRC itself).
 * @param len Number of bytes.
 * @return 16-bit CRC, host byte order (not yet split into wire lo/hi bytes).
 */
uint16_t mb_crc16(const uint8_t *buf, uint16_t len);

/**
 * @brief Build a FC03 (read holding) or FC04 (read input) request frame.
 *
 * @param out   Destination buffer, at least 8 bytes.
 * @param addr  Slave address (not validated here — caller's job).
 * @param fc    0x03 or 0x04.
 * @param start Starting register address (raw, 0-based).
 * @param count Number of registers to read.
 * @return Frame length in bytes (always 8).
 */
uint16_t mb_build_read_request(uint8_t *out, uint8_t addr, uint8_t fc,
                                uint16_t start, uint16_t count);

/**
 * @brief Build a FC06 (write single register) request frame.
 * @param out   Destination buffer, at least 8 bytes.
 * @param addr  Slave address (not validated here — caller's job).
 * @param reg   Register address to write (raw, 0-based).
 * @param value Value to write, host order.
 * @return Frame length in bytes (always 8).
 */
uint16_t mb_build_write_single_request(uint8_t *out, uint8_t addr,
                                        uint16_t reg, uint16_t value);

/**
 * @brief Build a FC16 (write multiple registers) request frame.
 * @param out    Destination buffer, at least 9 + count*2 bytes.
 * @param addr   Slave address (not validated here — caller's job).
 * @param start  Starting register address (raw, 0-based).
 * @param count  Number of registers (caller ensures <= MB_MAX_WRITE_REGISTERS).
 * @param values Register values, host order, count entries.
 * @return Frame length in bytes.
 */
uint16_t mb_build_write_multiple_request(uint8_t *out, uint8_t addr,
                                          uint16_t start, uint8_t count,
                                          const uint16_t *values);

/**
 * @brief Parse a FC03/FC04 response frame.
 *
 * Validates CRC, address echo, function code (including the exception
 * variant fc|0x80), and declared byte count before decoding registers.
 *
 * @param buf            Received frame bytes.
 * @param len            Received frame length.
 * @param expect_addr    Slave address the request was sent to.
 * @param expect_fc      0x03 or 0x04 — the function code that was requested.
 * @param expect_count   Number of registers that were requested.
 * @param out            Destination for decoded registers (expect_count entries).
 * @param exception_code Set when the return value is MB_FRAME_ERR_EXCEPTION.
 * @return Parse result.
 */
mb_frame_status_t mb_parse_read_response(const uint8_t *buf, uint16_t len,
                                          uint8_t expect_addr, uint8_t expect_fc,
                                          uint8_t expect_count, uint16_t *out,
                                          uint8_t *exception_code);

/**
 * @brief Parse a FC06 response frame (expected to echo the request exactly).
 *
 * Validates CRC, address echo, function code (including the exception
 * variant 0x06|0x80), and that the echoed register/value match what was
 * sent — a slave that returns a well-formed but different echo is treated
 * as a framing error, not silently accepted.
 *
 * @param buf            Received frame bytes.
 * @param len            Received frame length.
 * @param expect_addr    Slave address the request was sent to.
 * @param expect_reg     Register address that was written.
 * @param expect_value   Value that was written, host order.
 * @param exception_code Set when the return value is MB_FRAME_ERR_EXCEPTION.
 * @return Parse result.
 */
mb_frame_status_t mb_parse_write_single_response(const uint8_t *buf, uint16_t len,
                                                  uint8_t expect_addr,
                                                  uint16_t expect_reg,
                                                  uint16_t expect_value,
                                                  uint8_t *exception_code);

/**
 * @brief Parse a FC16 response frame (echoes address/start/count, not data).
 *
 * Per the Modbus spec, an FC16 response does not echo the written register
 * values — only start address and count — so unlike
 * mb_parse_write_single_response() there is no @p values to verify here.
 *
 * @param buf            Received frame bytes.
 * @param len            Received frame length.
 * @param expect_addr    Slave address the request was sent to.
 * @param expect_start   Starting register address that was written.
 * @param expect_count   Number of registers that were written.
 * @param exception_code Set when the return value is MB_FRAME_ERR_EXCEPTION.
 * @return Parse result.
 */
mb_frame_status_t mb_parse_write_multiple_response(const uint8_t *buf, uint16_t len,
                                                    uint8_t expect_addr,
                                                    uint16_t expect_start,
                                                    uint8_t expect_count,
                                                    uint8_t *exception_code);
