/**
 * @file main.cpp
 * @brief Windmeters Modbus Interface Tester — application entry point.
 *
 * Every library under lib/ is independently unit-tested under
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

/**
 * @brief Arduino entry point: bring up every library and start every task.
 *
 * Order matters only where a task depends on another's init having run
 * first (cfg before anything that reads a stored setting; mb_master_init()
 * before modbus_master_task_start(), since the task owns the queue
 * mb_master.h expects to already exist). Task start order among
 * independent tasks is otherwise arbitrary — FreeRTOS scheduling, not this
 * function, decides what runs when.
 *
 * The diagnostic `Serial.println()` calls run last, after every task has
 * started, not first — see the `setTxTimeoutMs()` comment inline below for
 * why: they used to run first and could block the entire function forever
 * with no USB host attached. Don't move a print above the task-start block
 * without re-reading that comment.
 */
void setup()
{
    Serial.begin(115200);
    /* This board's native USB-CDC ("Hardware CDC and JTAG" mode, see
     * platformio.ini) blocks Serial writes until a USB host actually
     * enumerates and reads them — unlike a UART-to-USB bridge chip, which
     * just sends bytes whether or not anything's listening. Powered only
     * via the Atomic RS485 Base (no USB host ever attached), that first
     * blocking write used to hang forever, and since it ran before every
     * *_task_start() call below, WiFi (and everything else) never started
     * — the symptom looked like "WiFi needs USB" but was really "setup()
     * itself never gets past its first log line without USB". Fixed here
     * (0 = never block) and by moving every print below the task starts,
     * so the fix holds even if a write happens to still block. See
     * memory/gotcha-log.md. */
    Serial.setTxTimeoutMs(0);

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

    delay(2000); /* let native USB-CDC enumerate before these lines, if a host is attached — cosmetic only now: setTxTimeoutMs(0) above means they never block boot either way */
    Serial.println("Windmeters Modbus Interface Tester starting...");
    Serial.println("Ready — open the web UI (AP: http://192.168.4.1, or STA/mDNS once connected).");
}

/**
 * @brief Arduino's own idle loop — deliberately empty.
 *
 * All real work happens in the FreeRTOS tasks setup() started; the
 * Arduino core's hidden loopTask that calls this still needs a loop()
 * symbol to link, but it has nothing to do here.
 */
void loop()
{
    /* Everything runs in FreeRTOS tasks started above. */
}
