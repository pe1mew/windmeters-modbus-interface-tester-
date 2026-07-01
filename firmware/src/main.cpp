/**
 * @file main.cpp
 * @brief Windmeters Modbus Interface Tester — application entry point.
 *
 * Every library here (lib/*) is independently unit-tested under
 * `pio test -e native` and has been individually verified on real
 * hardware — see design/completeRealisationPlan.md for what each task
 * covers and memory/gotcha-log.md for how each was checked. This file is
 * intentionally just wiring: initialise the libraries that need it, start
 * every FreeRTOS task, done. All real behaviour lives in lib/, not here.
 *
 * One-time bring-up diagnostics (UART loopback with a G5<->G6 jumper, an
 * LED colour-cycle demo, forced Modbus/scan/wind-poll smoke tests) ran
 * during development and are not part of normal boot — a loopback jumper
 * left in place would break real RS485 traffic. See memory/gotcha-log.md
 * if a new board ever needs the loopback check redone by hand.
 */
#include <Arduino.h>

#include "led_status.h"
#include "led_backend_fastled.h"
#include "cfg.h"
#include "cfg_backend_preferences.h"
#include "cfg_keys.h"
#include "mb_core.h"
#include "mb_transport_arduino.h"
#include "mb_log.h"
#include "mb_master.h"
#include "modbus_master_task.h"
#include "wifi_manager_task.h"
#include "ntp_task.h"
#include "scan_task.h"
#include "wind_poll_task.h"
#include "web_server_task.h"

#define MB_LOG_CAPACITY 32

void setup()
{
    Serial.begin(115200);
    delay(2000); /* let native USB-CDC enumerate before the first log line */
    Serial.println("Windmeters Modbus Interface Tester starting...");

    cfg_init(cfg_backend_preferences_init());
    led_init(led_backend_fastled_init());

    mb_init(mb_transport_arduino_init(cfg_get_u32(CFG_KEY_MB_BAUD, CFG_DEFAULT_MB_BAUD)),
            cfg_get_u16(CFG_KEY_MB_TIMEOUT_MS, CFG_DEFAULT_MB_TIMEOUT_MS),
            cfg_get_u8(CFG_KEY_MB_RETRIES, CFG_DEFAULT_MB_RETRIES));
    mblog_init(MB_LOG_CAPACITY);
    mb_master_init();
    modbus_master_task_start();

    wifi_manager_task_start();
    ntp_task_start();
    scan_task_start();
    wind_poll_task_start();
    web_server_task_start();

    Serial.println("Ready — open the web UI (AP: http://192.168.4.1, or STA/mDNS once connected).");
}

void loop()
{
    /* Everything runs in FreeRTOS tasks started above. */
}
