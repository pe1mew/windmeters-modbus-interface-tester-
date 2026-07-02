#ifdef ARDUINO
#include "scan_task.h"
#include "modbus_master_task.h"
#include "led_status.h"
#include <Arduino.h>
#include <string.h>

#define PROBE_REPLY_TIMEOUT_MS 2000u
#define PROBE_FC 0x04

typedef struct {
    bool    is_cancel;
    uint8_t range_start;
    uint8_t range_end;
} scan_command_t;

static QueueHandle_t s_command_queue = 0;

/** @brief Probe one address (FC04, 1 register at 0x0000) through the real master task and record the outcome. */
static void probe_current_address(void)
{
    uint8_t addr = bus_scan_get_status().current_addr;

    QueueHandle_t reply_queue = xQueueCreate(1, sizeof(mb_result_t));
    mb_task_request_t task_req;
    memset(&task_req, 0, sizeof(task_req));
    task_req.request.addr  = addr;
    task_req.request.fc    = PROBE_FC;
    task_req.request.start = 0x0000;
    task_req.request.count = 1;
    task_req.reply_to      = reply_queue;

    uint32_t start_ms = millis();
    xQueueSend(modbus_master_get_queue(), &task_req, portMAX_DELAY);

    mb_result_t result;
    bool responded = false;
    if (xQueueReceive(reply_queue, &result, pdMS_TO_TICKS(PROBE_REPLY_TIMEOUT_MS)) == pdTRUE) {
        responded = bus_scan_did_respond(result.status);
    }
    vQueueDelete(reply_queue);

    uint32_t now_ms = millis();
    bus_scan_record_probe_result(responded, PROBE_FC, now_ms - start_ms, now_ms);
}

/** @brief Drain any command that arrived while a sweep is running, without blocking. */
static void check_for_cancel(void)
{
    scan_command_t cmd;
    if (xQueueReceive(s_command_queue, &cmd, 0) == pdTRUE && cmd.is_cancel) {
        bus_scan_cancel();
    }
}

static void scan_task_fn(void * /*pvParameters*/)
{
    scan_command_t cmd;
    for (;;) {
        if (xQueueReceive(s_command_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            if (cmd.is_cancel) {
                continue; /* nothing running to cancel */
            }

            led_set_scanning();
            bus_scan_start(cmd.range_start, cmd.range_end, millis());
            while (bus_scan_is_active()) {
                probe_current_address();
                check_for_cancel();
            }
            led_set_idle();
        }
    }
}

void scan_task_start(void)
{
    s_command_queue = xQueueCreate(4, sizeof(scan_command_t));
    xTaskCreatePinnedToCore(scan_task_fn, "scan_task", 4096, NULL, 3, NULL, APP_CPU_NUM);
}

void scan_task_request_start(uint8_t range_start, uint8_t range_end)
{
    scan_command_t cmd = { false, range_start, range_end };
    xQueueSend(s_command_queue, &cmd, 0);
}

void scan_task_request_cancel(void)
{
    scan_command_t cmd = { true, 0, 0 };
    xQueueSend(s_command_queue, &cmd, 0);
}

#endif /* ARDUINO */
