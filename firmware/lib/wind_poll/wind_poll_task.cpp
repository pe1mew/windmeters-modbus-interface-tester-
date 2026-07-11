/**
 * @file wind_poll_task.cpp
 * @brief FreeRTOS/Arduino orchestration around the wind_poll core —
 * implementation. See wind_poll_task.h for the public API contract; this
 * file is Arduino-only and has no tests of its own (the decode/encode
 * math it drives is tested via wind_poll.h/.cpp instead).
 */
#ifdef ARDUINO
#include "wind_poll_task.h"
#include "modbus_master_task.h"
#include "cfg.h"
#include "cfg_keys.h"
#include <Arduino.h>
#include <string.h>

#define REPLY_TIMEOUT_MS 2000u         /**< Reply-wait budget for each queued Modbus request this task submits. */
#define POLL_CHECK_INTERVAL_MS 100u    /**< How often the task wakes to check whether it's due for its next poll. */

static bool               s_active        = false; /**< Set by wind_poll_set_active(); gates whether wind_poll_task_fn() submits any bus traffic. */
static uint8_t             s_target_addr   = 0; /**< Slave address currently (or most recently) targeted; set by wind_poll_set_active(). */
static wind_sensor_type_t s_active_type   = WIND_SENSOR_SPEED; /**< Sensor type currently (or most recently) targeted; set by wind_poll_set_active(). */
static wind_reading_t     s_latest; /**< Last successfully decoded reading; stale-but-kept on a failed poll, see wind_poll_get_latest(). */
static bool                s_has_data      = false; /**< Has any poll ever succeeded? Sticky — never reset back to false. */
static uint32_t            s_last_poll_ms  = 0; /**< millis() timestamp of the last successful poll; meaningful only when s_has_data is true. */

/**
 * @brief Submit a read (FC03/FC04) through modbus_master_task's queue and wait for the reply.
 * @param addr  Slave address to read, 1-247.
 * @param fc    Function code to submit — 0x03 (holding) or 0x04 (input).
 * @param start Raw 0-based starting register address.
 * @param count Number of registers to read; @p out must hold at least this many.
 * @param out   Destination for the decoded register values. Caller-owned;
 *              only written when this returns true.
 * @return true if the transaction completed with MB_OK within
 *         REPLY_TIMEOUT_MS and @p out was populated; false otherwise
 *         (timeout, or any non-MB_OK status).
 */
static bool do_read_registers(uint8_t addr, uint8_t fc, uint16_t start, uint8_t count, uint16_t *out)
{
    QueueHandle_t reply_queue = xQueueCreate(1, sizeof(mb_result_t));
    mb_task_request_t req;
    memset(&req, 0, sizeof(req));
    req.request.addr  = addr;
    req.request.fc    = fc;
    req.request.start = start;
    req.request.count = count;
    req.reply_to      = reply_queue;

    xQueueSend(modbus_master_get_queue(), &req, portMAX_DELAY);

    mb_result_t result;
    bool ok = false;
    if (xQueueReceive(reply_queue, &result, pdMS_TO_TICKS(REPLY_TIMEOUT_MS)) == pdTRUE && result.status == MB_OK) {
        memcpy(out, result.registers, (size_t)count * sizeof(uint16_t));
        ok = true;
    }
    vQueueDelete(reply_queue);
    return ok;
}

/**
 * @brief Submit a single-register write (FC06) through modbus_master_task's queue and wait for the reply.
 * @param addr  Slave address to write, 1-247.
 * @param reg   Raw 0-based holding register address to write.
 * @param value Raw register value to write (already encoded — see wind_config_field_encode()).
 * @return true if the slave echoed the write back unchanged (MB_OK) within
 *         REPLY_TIMEOUT_MS; false on timeout or any other status.
 */
static bool do_write_single(uint8_t addr, uint16_t reg, uint16_t value)
{
    QueueHandle_t reply_queue = xQueueCreate(1, sizeof(mb_result_t));
    mb_task_request_t req;
    memset(&req, 0, sizeof(req));
    req.request.addr      = addr;
    req.request.fc        = 0x06;
    req.request.start     = reg;
    req.request.values[0] = value;
    req.reply_to           = reply_queue;

    xQueueSend(modbus_master_get_queue(), &req, portMAX_DELAY);

    mb_result_t result;
    bool ok = false;
    if (xQueueReceive(reply_queue, &result, pdMS_TO_TICKS(REPLY_TIMEOUT_MS)) == pdTRUE) {
        ok = (result.status == MB_OK);
    }
    vQueueDelete(reply_queue);
    return ok;
}

/**
 * @brief Task body: while active and the configured poll interval has
 * elapsed (or no data has ever been fetched yet), reads the active type's
 * input registers (12 or 13, TDS §2.7/FR-MB27) in one FC04 transaction and
 * decodes them; otherwise idles. A failed read leaves
 * s_latest/s_has_data/s_last_poll_ms untouched so the last good reading
 * survives a transient bus error rather than being blanked.
 */
static void wind_poll_task_fn(void * /*pvParameters*/)
{
    for (;;) {
        if (s_active) {
            uint32_t interval = cfg_get_u32(CFG_KEY_WIND_POLL_INTERVAL, CFG_DEFAULT_WIND_POLL_INTERVAL);
            if (!s_has_data || wind_poll_interval_elapsed(millis(), s_last_poll_ms, interval)) {
                /* 13 is the combined build's map size (TDS §2.7); the
                 * single-sensor builds only request 12 (below) and never
                 * touch index 12, so a 13-entry buffer sized for the max
                 * case is safe to reuse for all three types. */
                uint16_t raw[13];
                uint8_t count = wind_sensor_input_register_count(s_active_type);
                if (do_read_registers(s_target_addr, 0x04, 0x0000, count, raw)) {
                    wind_poll_decode(s_active_type, raw, &s_latest);
                    s_has_data     = true;
                    s_last_poll_ms = millis();
                }
                /* On failure: s_latest/s_has_data are left as the last
                 * good reading — a growing wind_poll_age_ms() is how a
                 * future UI signals staleness, not a cleared display. */
            }
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_CHECK_INTERVAL_MS));
    }
}

void wind_poll_task_start(void)
{
    xTaskCreatePinnedToCore(wind_poll_task_fn, "wind_poll", 4096, NULL, 3, NULL, APP_CPU_NUM);
}

void wind_poll_set_active(uint8_t addr, wind_sensor_type_t type, bool active)
{
    s_target_addr = addr;
    s_active_type = type;
    s_active      = active;
}

bool wind_poll_is_active(void)
{
    return s_active;
}

wind_sensor_type_t wind_poll_get_active_type(void)
{
    return s_active_type;
}

wind_reading_t wind_poll_get_latest(void)
{
    return s_latest;
}

bool wind_poll_has_data(void)
{
    return s_has_data;
}

uint32_t wind_poll_age_ms(void)
{
    if (!s_has_data) {
        return 0xFFFFFFFFu;
    }
    return millis() - s_last_poll_ms;
}

bool wind_poll_read_config(uint8_t addr, wind_config_t *out)
{
    /* TDS §2.8: 6 holding registers, identical addresses/meaning
     * regardless of sensor type (FR-MB27) — no type-conditional "which
     * field is at raw[N]" branching, and no device_addr field (that
     * register no longer exists, FR-MB07/FR-MB26). */
    uint16_t raw[6];
    uint8_t count = wind_sensor_holding_register_count();
    if (!do_read_registers(addr, 0x03, 0x0000, count, raw)) {
        return false;
    }
    out->dir_offset_deg          = wind_config_field_decode(WIND_CFG_DIR_OFFSET, raw[0]);
    out->measurement_window_ms   = (uint16_t)wind_config_field_decode(WIND_CFG_MEASUREMENT_WINDOW, raw[1]);
    out->averaging_window_s      = (uint16_t)wind_config_field_decode(WIND_CFG_AVERAGING_WINDOW, raw[2]);
    out->low_speed_cutoff_ms     = wind_config_field_decode(WIND_CFG_LOW_SPEED_CUTOFF, raw[3]);
    out->calibration_c_m_per_rot = wind_config_field_decode(WIND_CFG_CALIBRATION_C, raw[4]);
    out->pulses_per_rotation     = (uint16_t)wind_config_field_decode(WIND_CFG_PULSES_PER_ROTATION, raw[5]);
    return true;
}

bool wind_poll_write_config_field(uint8_t addr, wind_config_field_t field, float value)
{
    uint16_t reg = wind_config_field_register(field);
    if (reg == 0xFFFF) {
        return false; /* unknown field enum value — never touch the wire */
    }
    uint16_t raw_value = wind_config_field_encode(field, value);
    return do_write_single(addr, reg, raw_value);
}

#endif /* ARDUINO */
