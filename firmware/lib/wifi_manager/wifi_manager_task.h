/**
 * @file wifi_manager_task.h
 * @brief FreeRTOS/Arduino orchestration around the wifi_manager decision
 *        core (TASK-WIFI, design/completeRealisationPlan.md).
 *
 * Thin by the same design as modbus_master_task.cpp: the actual decisions
 * (attempt STA? keep the AP up?) live in wifi_manager.h/.cpp and are
 * tested there. This file just drives WiFi.softAP()/WiFi.begin()/MDNS and
 * polls for the outcome — no tests of its own, Arduino/FreeRTOS-only.
 */
#pragma once

#ifdef ARDUINO

#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/**
 * @brief Current WiFi status snapshot — mode, connection state, and radio details.
 */
typedef struct {
    bool    ap_active;    /**< True while the AP radio is up (always true at boot; cleared once STA connects and the AP is torn down). */
    bool    sta_connected; /**< True once WiFi.begin() has succeeded and stayed connected — never set at all if no credentials were stored. */
    char    mode_str[8];  /**< "AP", "AP+STA" (connecting), or "STA". */
    char    ssid[33];     /**< Active AP SSID, or the connected STA SSID. */
    char    ip[16];       /**< Dotted-quad; only meaningful once sta_connected. */
    int8_t  rssi;         /**< Only meaningful once sta_connected. */
} wifi_status_t;

/**
 * @brief Start the task. cfg_init() must already have been called —
 *        this reads wifi_ssid/wifi_pass from NVS.
 */
void wifi_manager_task_start(void);

/**
 * @brief Current status snapshot (for the eventual Status page / WebSocket push).
 * @return Status struct by value — cheap, small, safe to call every broadcast tick.
 */
wifi_status_t wifi_manager_get_status(void);

#endif /* ARDUINO */
