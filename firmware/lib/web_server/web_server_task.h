/**
 * @file web_server_task.h
 * @brief HTTP + WebSocket server (TASK-WEB, design/completeRealisationPlan.md).
 *
 * Arduino-only orchestration around web_core.h's JSON builders and every
 * other task's public API — no tests of its own, same division of labour
 * as the rest of this project. Serves the ported GUI from SPIFFS
 * (firmware/data/), pushes type-tagged WebSocket JSON, and exposes the
 * REST endpoints the GUI's app.js calls.
 */
#pragma once

#ifdef ARDUINO

/**
 * @brief Start the web server + its periodic WebSocket broadcast task.
 *
 * cfg_init(), modbus_master_task_start(), scan_task_start(),
 * wind_poll_task_start(), wifi_manager_task_start(), and ntp_task_start()
 * must all already have run — this only serves and orchestrates them, it
 * doesn't start any of them itself.
 */
void web_server_task_start(void);

#endif /* ARDUINO */
