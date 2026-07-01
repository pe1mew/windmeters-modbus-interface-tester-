# Progress — Windmeters Modbus Interface Tester

| Field        | Value                                    |
|--------------|-------------------------------------------|
| Document     | Progress Snapshot                         |
| Project      | Windmeters Modbus Interface Tester        |
| Date         | 2026-07-01                                |
| Status       | Phase 1 (Libraries) + Phase 2 (FreeRTOS Tasks) complete; Phase 3 (Integration Test) not started |
| Related docs | `design/completeRealisationPlan.md` (Parts A/B this reports against), `design/realisationPlan.md` (MB-1/MB-2 detail), `design/scratchbook.md` (design source of truth), `design/whatsNext.md` (what follows this) |

---

## 1. Where things stand

Every library and every FreeRTOS task in `completeRealisationPlan.md` Parts A
and B is implemented, unit-tested, and individually verified against real
hardware. Part C — Integration Test (INT-01…INT-09, requiring a two-peer
RS-485 bench) — has not been started. See `whatsNext.md`.

**91/91 native unit tests pass** (`pio test -e native`, ~6s, reconfirmed
today). Nothing in this repo is committed yet — see §7.

---

## 2. Part A — Libraries

| Library | Purpose | Native tests | Status |
|---|---|---|---|
| `cfg` (LIB-NVS) | Typed NVS config persistence via Arduino `Preferences` | 8 | Done |
| `led_status` (LIB-LED) | RGB status LED, FastLED @ GPIO35 | 7 | Done |
| `mb_log` (LIB-LOG) | Modbus traffic ring buffer | 4 | Done |
| `mb_core` (LIB-MB) | Modbus RTU master protocol core — CRC16, FC03/04/06/16, timeout/retry | 20 | Done |

All four match their `completeRealisationPlan.md` §2 / `realisationPlan.md`
§2 planned API surfaces — no material divergence during implementation.

---

## 3. Part B — FreeRTOS Tasks

| Task | Purpose | Native tests (decision core) | Hardware verification |
|---|---|---|---|
| `modbus_master_task` (TASK-MB / MB-2) | Sole owner of the RS-485 UART; serves a request queue | 6 (`mb_master`) | Real queue → task → UART round trip |
| `wifi_manager_task` (TASK-WIFI) | AP always up, STA on stored credentials, mDNS on STA success | 6 (`wifi_manager`) | Real AP (independently confirmed via `netsh wlan show networks`), real STA connect + DHCP IP against a real network, real mDNS resolution |
| `ntp_task` (TASK-NTP) | Manual time-set fallback + real NTP sync | 12 (`ntp_manager`) | Manual set/reject verified; real internet NTP observed overwriting a manually-set clock once STA connected |
| `scan_task` (TASK-SCAN) | Bus sweep over a configurable address range | 8 (`bus_scan`) | Real sweep (addresses 1–5) through the queue/task/UART chain |
| `wind_poll_task` (TASK-WIND) | Periodic poll + config read/write for one target address | 10 (`wind_poll`) | Real poll/config-read/config-write/suspend cycle through the queue/task/UART chain |
| `web_server_task` (TASK-WEB) | HTTP + WebSocket server, serves the ported GUI | 10 (`web_core`) | Real HTTP requests, real WebSocket traffic, the Modicon-conversion worked example reproduced live via `POST /explorer/query` |

Native test breakdown: `test_mb_core` 20, `test_mb_log` 4, `test_led_status`
7, `test_cfg` 8, `test_mb_master` 6, `test_wifi_manager` 6,
`test_ntp_manager` 12, `test_bus_scan` 8, `test_wind_poll` 10,
`test_web_core` 10 — **91 total**.

**Important caveat:** none of the hardware verification above exercised a
real Modbus bus peer — every check above proved the plumbing (queue, task,
UART, WebSocket, HTTP) works, either with nothing listening on the bus or
(the very first bring-up test only) the tester's own TX looped back into its
own RX. Actually talking Modbus to another device is Phase 3's job — see
`whatsNext.md`.

---

## 4. GUI

`firmware/data/` (`index.html`, `style.css`, `app.js`) is the template's GUI
(`greenhouse-Controller-Modbus-sensor-emulator`), ported and extended per
`completeRealisationPlan.md`'s guiding principle — one scrollable page, no
framework, no build step:

- **Kept as-is:** page shell, Status section, Modbus Log section, WiFi
  Settings section, every `app.js` helper (`post()`, `setText()`,
  `setBadge()`, `setSliderInput()`, WebSocket connect/reconnect, the
  `type`-keyed message router).
- **Removed:** FG6485A/S200/Replay sections — not part of this project's
  scope.
- **Added:** Bus Scanner, Wind Test, Register Explorer — new sections, new
  `type` cases in the WebSocket router, backed by `web_core`'s JSON
  builders.
- **WiFi Settings is real, not a stub:** `POST /config/wifi` writes
  `wifi_ssid`/`wifi_pass` to NVS via `cfg_set_str`. Known limitation: takes
  effect on next reboot only, since `wifi_manager_task` evaluates stored
  credentials once, at boot (tracked as a refinement in `whatsNext.md`).

---

## 5. Application entry point

`firmware/src/main.cpp` was, until this session, a 333-line linear bring-up
script — a UART loopback self-test, an LED colour-cycle demo, an NVS
`boot_count` check, and forced smoke tests of the Modbus/WiFi/NTP/scan/
wind-poll tasks, all gated behind `Serial.println` narration and
multi-second delays before the web server ever started.

It's now 63 lines: `cfg_init` → `led_init` → `mb_init`/`mblog_init`/
`mb_master_init` → `modbus_master_task_start()`, then the remaining five
task-starts. No demo code, no artificial delays, no hardcoded target
address. The bring-up diagnostics did their job — every finding they
produced (G5/G6 pin assignment, GPIO35 LED, `ARDUINO_USB_CDC_ON_BOOT`, the
UART glitch byte) is recorded in `memory/gotcha-log.md`, not silently lost;
a comment in `main.cpp` points there if the UART loopback check ever needs
redoing by hand on a new board.

Verified: `pio run -e windmeterTester` builds clean after the cleanup
(SUCCESS, 11s).

---

## 6. Hardware facts confirmed this phase

- RS-485 base RX/TX = **G5/G6**, not G1/G2 as M5Stack's own docs claim for
  this base+board combination
- Onboard RGB LED = **GPIO35**, confirmed via `blinkyS3`, regardless of
  AtomS3 vs. AtomS3 Lite naming
- `-DARDUINO_USB_CDC_ON_BOOT=1` is required in `build_flags` or `Serial`
  silently goes nowhere
- UART loopback tests need an RX-buffer flush right after `Serial2.begin()`
  (a one-off glitch byte, not a wiring fault)
- NVS (`Preferences`) persistence confirmed across real reboots, not just
  reflashes
- WiFi AP, real STA connect, real NTP sync, and real mDNS resolution all
  independently verified against a real network

Full detail, including how each was diagnosed and what looked like a
hardware fault but wasn't: `memory/gotcha-log.md`.

---

## 7. Repo state

Nothing in this repo has been committed since the initial scaffolding
(`git log`: `09f6567 Initial commit...`, `8edf18a Update`). All Phase 1/
Phase 2 work — `firmware/`, `memory/`, `design/realisationPlan.md`,
`design/completeRealisationPlan.md`, and this file — is untracked or
modified in the working tree, left staged for review rather than committed
(per this project's workflow: changes are prepared and handed off, never
committed automatically).

While gathering context for this document, a test fixture in
`firmware/test/test_web_core/test_web_core.cpp` was found to contain the
real WiFi SSID used earlier this session to provision NVS credentials for
hardware verification (§3). The SSID itself was never a secret needed for
protocol testing — any placeholder string exercises the same JSON-building
code path — so it's been replaced with `"test-network"`; tests re-run clean
(10/10). The WiFi password was never written to any tracked file.

---

## 8. What's next

Everything in Parts A and B of `completeRealisationPlan.md` is done. What's
left is Part C — Integration Test — plus a short list of known refinements.
See `design/whatsNext.md`.

---

*End of Progress Snapshot*
