#ifdef ARDUINO
#include "wind_poll_task.h"
#include "modbus_master_task.h"
#include "cfg.h"
#include "cfg_keys.h"
#include <Arduino.h>
#include <string.h>

#define REPLY_TIMEOUT_MS 2000u
#define POLL_CHECK_INTERVAL_MS 100u

static bool               s_active        = false;
static uint8_t             s_target_addr   = 0;
static wind_sensor_type_t s_active_type   = WIND_SENSOR_SPEED;
static wind_reading_t     s_latest;
static bool                s_has_data      = false;
static uint32_t            s_last_poll_ms  = 0;

/** @brief Submit a read (FC03/FC04) through modbus_master_task's queue and wait for the reply. */
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

/** @brief Submit a single-register write (FC06) through modbus_master_task's queue and wait for the reply. */
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

static void wind_poll_task_fn(void * /*pvParameters*/)
{
    for (;;) {
        if (s_active) {
            uint32_t interval = cfg_get_u32(CFG_KEY_WIND_POLL_INTERVAL, CFG_DEFAULT_WIND_POLL_INTERVAL);
            if (!s_has_data || wind_poll_interval_elapsed(millis(), s_last_poll_ms, interval)) {
                /* TDS §2.7 (v0.6): both builds implement the same 12-register
                 * input block (FR-MB27) — always read all 12, not a
                 * type-conditional 2 or 3 like before that spec matured. */
                uint16_t raw[12];
                uint8_t count = wind_sensor_input_register_count();
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
    /* TDS §2.8 (v0.6): 4 holding registers, identical addresses/meaning
     * regardless of sensor type (FR-MB27) — no more type-conditional
     * "which field is at raw[1]" branching, and no device_addr field
     * (that register no longer exists, FR-MB07/FR-MB26). */
    uint16_t raw[4];
    uint8_t count = wind_sensor_holding_register_count();
    if (!do_read_registers(addr, 0x03, 0x0000, count, raw)) {
        return false;
    }
    out->dir_offset_deg        = wind_config_field_decode(WIND_CFG_DIR_OFFSET, raw[0]);
    out->measurement_window_ms = (uint16_t)wind_config_field_decode(WIND_CFG_MEASUREMENT_WINDOW, raw[1]);
    out->averaging_window_s    = (uint16_t)wind_config_field_decode(WIND_CFG_AVERAGING_WINDOW, raw[2]);
    out->low_speed_cutoff_ms   = wind_config_field_decode(WIND_CFG_LOW_SPEED_CUTOFF, raw[3]);
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
