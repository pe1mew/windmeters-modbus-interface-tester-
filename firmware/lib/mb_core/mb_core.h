/**
 * @file mb_core.h
 * @brief Modbus RTU master — public API (MB-1, design/realisationPlan.md §2).
 *
 * Wraps mb_frame.h's pure protocol logic with actual I/O (via an injected
 * mb_transport_t) and a timeout/retry loop. This is the library the future
 * modbus_master_task (MB-2) will call — see design/realisationPlan.md.
 *
 * Not thread-safe: exactly one caller at a time, same rule the eventual
 * MB-2 FreeRTOS task enforces by being the sole owner of the UART.
 */
#pragma once

#include <stdint.h>
#include "mb_transport.h"

/**
 * @brief Outcome of an mb_* transaction call.
 */
typedef enum {
    MB_OK = 0,          /**< Request sent and a valid, matching response decoded. */
    MB_ERR_TIMEOUT,     /**< No complete response within the configured timeout (all retries used). */
    MB_ERR_CRC,         /**< Response CRC did not match. */
    MB_ERR_EXCEPTION,   /**< Slave returned a Modbus exception — see mb_last_exception_code(). */
    MB_ERR_FRAMING,     /**< Wrong length, function code, or address echo. */
    MB_ERR_PARAM,       /**< Bad argument (addr 0, count out of range, NULL out). */
} mb_status_t;

/**
 * @brief Initialise the Modbus master core.
 * @param transport  Byte transport to use (must outlive all mb_* calls).
 * @param timeout_ms Response timeout per attempt.
 * @param retries    Additional attempts after the first (0 = no retry).
 */
void mb_init(const mb_transport_t *transport, uint16_t timeout_ms, uint8_t retries);

/**
 * @brief Change the response timeout at runtime (e.g. from a settings page).
 * @param timeout_ms New per-attempt response timeout, in milliseconds.
 */
void mb_set_timeout(uint16_t timeout_ms);

/**
 * @brief Change the retry count at runtime.
 * @param retries New number of additional attempts after the first (0 = no retry).
 */
void mb_set_retries(uint8_t retries);

/**
 * @brief Read holding registers (FC03).
 * @param addr  Slave address, 1-247 (0 = broadcast, rejected).
 * @param start Raw 0-based register address.
 * @param count 1-125.
 * @param out   Buffer for @p count decoded register values.
 * @return MB_OK on a valid matching response; MB_ERR_PARAM if @p addr is 0,
 *         @p out is NULL, or @p count is 0 or exceeds MB_MAX_READ_REGISTERS;
 *         otherwise the transport/frame error that occurred.
 */
mb_status_t mb_read_holding_registers(uint8_t addr, uint16_t start, uint8_t count, uint16_t *out);

/**
 * @brief Read input registers (FC04). Same argument rules as mb_read_holding_registers().
 * @param addr  Slave address, 1-247 (0 = broadcast, rejected).
 * @param start Raw 0-based register address.
 * @param count 1-125.
 * @param out   Buffer for @p count decoded register values.
 * @return MB_OK on a valid matching response; MB_ERR_PARAM if @p addr is 0,
 *         @p out is NULL, or @p count is 0 or exceeds MB_MAX_READ_REGISTERS;
 *         otherwise the transport/frame error that occurred.
 */
mb_status_t mb_read_input_registers(uint8_t addr, uint16_t start, uint8_t count, uint16_t *out);

/**
 * @brief Write a single holding register (FC06).
 * @param addr  Slave address, 1-247 (0 = broadcast, rejected).
 * @param reg   Raw 0-based register address.
 * @param value Value to write, host order.
 * @return MB_OK if the slave echoed the request back unchanged;
 *         MB_ERR_PARAM if @p addr is 0; otherwise the transport/frame error
 *         that occurred.
 */
mb_status_t mb_write_single_register(uint8_t addr, uint16_t reg, uint16_t value);

/**
 * @brief Write multiple holding registers (FC16).
 * @param addr   Slave address, 1-247 (0 = broadcast, rejected).
 * @param start  Raw 0-based starting register address.
 * @param count  1-123.
 * @param values Register values to write, host order, @p count entries.
 * @return MB_OK if the slave echoed address/start/count back unchanged;
 *         MB_ERR_PARAM if @p addr is 0, @p values is NULL, or @p count is 0
 *         or exceeds MB_MAX_WRITE_REGISTERS; otherwise the transport/frame
 *         error that occurred.
 */
mb_status_t mb_write_multiple_registers(uint8_t addr, uint16_t start, uint8_t count, const uint16_t *values);

/**
 * @brief Exception code from the most recent MB_ERR_EXCEPTION result.
 *
 * Valid only immediately after a call that returned MB_ERR_EXCEPTION —
 * this is a single scratch variable, not per-call state. A caller that
 * needs the code must read it before making another mb_* call.
 *
 * @return The Modbus exception code (e.g. 0x02 Illegal Data Address) from
 *         the slave's most recent exception response.
 */
uint8_t mb_last_exception_code(void);

/**
 * @brief Raw bytes of the request frame from the most recent mb_* call.
 *
 * Populated on every attempt (a retry re-sends an identical request, so
 * only the final attempt's bytes are kept). For a caller that needs to log
 * traffic (see the future modbus_master_task) — same single-scratch-value
 * caveat as mb_last_exception_code(): valid only until the next mb_* call.
 *
 * @param buf     Destination for the copied bytes. Caller-owned; must be at
 *                least @p max_len bytes.
 * @param max_len Capacity of @p buf.
 * @return Number of bytes copied into @p buf (0 if nothing was sent, e.g.
 *         the previous call was rejected by parameter validation).
 */
uint16_t mb_get_last_tx(uint8_t *buf, uint16_t max_len);

/**
 * @brief Raw bytes of the response frame from the most recent mb_* call.
 * @param buf     Destination for the copied bytes. Caller-owned; must be at
 *                least @p max_len bytes.
 * @param max_len Capacity of @p buf.
 * @return Number of bytes copied into @p buf (0 if no response was
 *         received — a timeout, or nothing was sent in the first place).
 */
uint16_t mb_get_last_rx(uint8_t *buf, uint16_t max_len);

/**
 * @brief Attempts the most recent mb_* call took (1 + retries actually consumed).
 *
 * Same single-scratch-value caveat as mb_last_exception_code() /
 * mb_get_last_tx() / mb_get_last_rx() — valid only until the next mb_* call.
 *
 * @return Number of attempts (1 + retries actually consumed); 0 if the call
 *         never reached the transport (rejected by parameter validation) —
 *         callers that need this value durably (e.g. across a FreeRTOS
 *         queue hop) must copy it out immediately, same as the tx/rx bytes.
 */
uint8_t mb_get_last_attempts(void);

/**
 * @brief Stale RX bytes flushed before transmitting, over the most recent mb_* call.
 *
 * mb_core drains the transport's RX buffer immediately before each write
 * (see mb_transport_t::flush and do_transaction() in mb_core.cpp); this is
 * the total it discarded across every attempt of the last call — normally 0,
 * but nonzero when the master overheard third-party traffic on a shared
 * RS-485 bus while idle. A pure diagnostic: the transaction still succeeds,
 * this just reports how much stale backlog had to be cleared first
 * (surfaced as "[flushed N]" in mb_master_process()'s log summary).
 *
 * Same single-scratch-value caveat as mb_get_last_attempts() — valid only
 * until the next mb_* call; 0 after a PARAM rejection, and 0 for a transport
 * whose flush callback is NULL.
 *
 * @return Number of stale bytes discarded before this call's request(s).
 */
uint16_t mb_get_last_discarded(void);
