# Progress — Windmeters Modbus Interface Tester

| Field        | Value                                    |
|--------------|-------------------------------------------|
| Document     | Progress Snapshot                         |
| Project      | Windmeters Modbus Interface Tester        |
| Date         | 2026-07-02 (updated again — wind register map reconciled against the DUT's TDS v0.6, §11) |
| Status       | **Tagged v0.4.2** (`changelog.md`) — Phase 1 (Libraries) + Phase 2 (FreeRTOS Tasks) + Phase 2.5 (Machine API) complete; Phase 3 (Integration Test) eight-of-nine done (`whatsNext.md` §3); wind speed/direction split + GUI restructure done (§10 below). Firmware reports its own version (`fw_version`, WS status + `GET /api/v1/status` + web UI footer). v0.4.1: removed a dead NVS key + a dead function found by a full dead-code audit, closed a real gap where `GET /api/v1/log` never actually used real timestamps even when NTP was synced. v0.4.2: root-caused and fixed the first-transaction-after-boot CRC error (`mb_transport_arduino_init()` now flushes a bench-confirmed stray RX byte instead of leaving it to corrupt the first real read) — closes a `memory/gotcha-log.md` entry that had sat unconfirmed for months. Since v0.4.2 (not yet its own tag): the DUT's TDS matured to v0.6 (FR-MB27) with a materially different register model than the tester assumed — §11 documents the reconciliation across code, tests, GUI, and docs. |
| Related docs | `design/completeRealisationPlan.md` (Parts A/B this reports against), `design/realisationPlan.md` (MB-1/MB-2 detail), `design/scratchbook.md` (design source of truth), `design/whatsNext.md` (what follows this) |

---

## 1. Where things stand

Every library and every FreeRTOS task in `completeRealisationPlan.md` Parts A
and B is implemented, unit-tested, and individually verified against real
hardware. Part C — Integration Test (INT-01…INT-09) is eight-of-nine done
against a real FG6485A bus peer; only INT-05 (real-DUT wind decode) remains,
blocked on the DUT's own firmware. See `whatsNext.md`.

**143/143 native unit tests pass** (`pio test -e native`, ~6s, reconfirmed
today) — up from 142 after §11's `test_wind_poll` rewrite against the DUT's
TDS v0.6 register model (17 cases, up from the pre-reconciliation count),
which itself followed Phase 2.5's tests plus the wind speed/direction split
and log-broadcast fix (§10).
Everything through the Phase 3 integration-test fixes is committed
(`5affff4`); §10's log-broadcast fix, §11's TDS v0.6 reconciliation, and
this documentation pass are staged, not yet committed — see §8.

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

Native test breakdown (count at the time Part B wrapped up, before Phase
2.5/the wind split added more — see §1 for the current total):
`test_mb_core` 20, `test_mb_log` 4, `test_led_status`
7, `test_cfg` 8, `test_mb_master` 6, `test_wifi_manager` 6,
`test_ntp_manager` 12, `test_bus_scan` 8, `test_wind_poll` 10,
`test_web_core` 10 — **91 total**.

**Important caveat (Phase 2's own limit at the time — resolved by Phase 3,
see `whatsNext.md` §3):** none of the hardware verification above exercised a
real Modbus bus peer — every check above proved the plumbing (queue, task,
UART, WebSocket, HTTP) works, either with nothing listening on the bus or
(the very first bring-up test only) the tester's own TX looped back into its
own RX. Actually talking Modbus to another device is Phase 3's job — see
`whatsNext.md`.

---

## 4. GUI

`firmware/data/` (`index.html`, `style.css`, `app.js`) started as the
template's GUI (`greenhouse-Controller-Modbus-sensor-emulator`), ported and
extended per `completeRealisationPlan.md`'s guiding principle — vanilla
HTML/CSS/JS, no framework, no build step. Current shape (after the
2026-07-02 restructure, §10):

- **Status** (top, always visible) and **Modbus Log** (bottom, always
  visible) bracket a tab bar — **Bus Scanner / Wind Speed / Wind Direction /
  Register Explorer / System Settings** — rather than the original one
  long scrollable page. `app.js` helpers (`post()`, `setText()`,
  `setBadge()`, WebSocket connect/reconnect, the `type`-keyed message
  router) are unchanged by the restructure — it's a CSS/layout change plus
  a small `switchTab()` helper, not a rewrite.
- **Removed at the start:** FG6485A/S200/Replay sections — not part of this
  project's scope. **Removed 2026-07-02:** Bus Scanner's quick-preset row
  (§10).
- **Added:** Bus Scanner, Wind Speed, Wind Direction (originally one
  combined "Wind Test", split 2026-07-02 — §10), Register Explorer — new
  sections/tabs, new `type` cases in the WebSocket router, backed by
  `web_core`'s JSON builders.
- **System Settings is real, not a stub:** `POST /config/wifi` writes
  `wifi_ssid`/`wifi_pass` to NVS via `cfg_set_str`, then calls
  `ESP.restart()` so the new credentials actually take effect (originally
  shipped as "takes effect on next reboot only" — that was a real INT-02
  bug, not a documented limitation; fixed during Phase 3, see
  `whatsNext.md` §3.2's INT-01/02 finding). Modbus Timeout/Retries joined
  this section 2026-07-02 and pre-populate from the live status stream
  instead of loading empty.

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

## 7. Phase 2.5 — Machine API (`design/api.md`)

All 7 endpoints implemented and hardware-verified: `POST /api/v1/modbus`,
`GET /api/v1/spec`, `GET /api/v1/status`, `GET /api/v1/wind`,
`GET /api/v1/log`, `POST`/`GET /api/v1/scan`.

**Two correctness fixes to existing code, surfaced while wiring this in —
neither was a new bug in Phase 2.5's own code, both were latent gaps
Phase 2.5 was the first thing to actually exercise:**

- `mb_result_t` didn't carry raw TX/RX frames or attempt counts — a caller
  reading `mb_get_last_tx()`/`_rx()`/`_attempts()` after a FreeRTOS queue
  hop could get a *different* transaction's bytes if another request had
  already been processed in between. Fixed by copying them into the result
  struct inside `mb_master_process()`, while still fresh. Covered by a new
  native test that specifically proves a held `mb_result_t` survives a
  second, different transaction running afterward.
- `modbus_master_task` unconditionally overwrote `mb_timeout_ms`/
  `mb_retries` from NVS on every transaction — `/api/v1/modbus`'s
  documented per-request `timeout_ms`/`retries` override would have been
  silently ignored. Fixed with an opt-in override field on
  `mb_task_request_t`, defaulting to the existing NVS-sourced behaviour for
  every other caller. Verified on hardware: `timeout_ms:1000,retries:2` →
  measured 3 real attempts, ~3.3s wall time, not the NVS default.

**One real bug in `/api/v1/scan` itself, found and fixed via hardware
testing (native tests couldn't have caught it — the bug is in
`web_server_task.cpp`'s Arduino-only polling loop, not in the tested
`bus_scan` core):** the `wait:true` poll loop treated any non-running state
as "done," including a *stale* `SCAN_COMPLETE` left over from the
*previous* scan — `scan_task_request_start()` only enqueues a command,
it doesn't block until `scan_task` actually dequeues it and calls
`bus_scan_start()`. Every scan after the first one returned instantly with
the prior scan's result. Fixed by keying "done" off the *current* scan's
own range showing up on a COMPLETE/CANCELLED status, not just the state
value in isolation.

**Real bus peer connected mid-session**: an FG6485A at address 1 (Modbus
RTU, FC03 only). Used to verify `/api/v1/modbus` against genuine sensor
data (humidity 48.9%, temperature 27.2°C, both physically plausible) and
`/api/v1/scan` actually finding it (`functions_ok:[4]` — see note below).
Also surfaced that `bus_scan`'s FC04-only probe strategy depends on the
target replying with a Modbus exception to an unsupported function code
rather than staying silent — the FG6485A does (non-standard exception code
129, not one of the standard 1–11, decoded as `"unknown"` — expected,
`web_core_exception_name()` only covers the standard table), so it's
detected, but a device that ignores unsupported FCs outright would be
missed by a scan even though `/api/v1/modbus` could still talk to it
directly. Also observed: the very first scan run immediately after a fresh
boot missed the device (found it on every run since) — not yet
root-caused, plausibly related to the already-documented
UART-glitch-right-after-init class of issue in `memory/gotcha-log.md`.

---

## 8. Repo state

**Updated — no longer accurate to say nothing is committed.** Phase 1/2,
Phase 2.5, the Phase 3 integration-test fixes, and §10's wind
speed/direction split + GUI restructure are all committed
(`git log`: `5affff4 Add Phase 2.5 machine API, split wind sensors into
speed/direction, and refactor the GUI into tabs`, on top of `9393b53`,
`d9c7189`, `aa9af37`, `3316f53`, `8edf18a`, `09f6567`). §10's log-broadcast
TX-drop fix and this documentation pass are staged, not yet committed — per
this project's workflow, changes are prepared and handed off, never
committed automatically; everything up to `5affff4` reflects the user
having done that handoff's follow-through themselves.

While gathering context for an earlier version of this document, a test
fixture in `firmware/test/test_web_core/test_web_core.cpp` was found to
contain the real WiFi SSID used earlier that session to provision NVS
credentials for hardware verification (§3). The SSID itself was never a
secret needed for protocol testing — any placeholder string exercises the
same JSON-building code path — so it was replaced with `"test-network"`;
tests re-ran clean. The WiFi password was never written to any tracked
file. (Historical note — this finding predates §10 and is already
reflected in the committed test suite.)

---

## 9. What's next

Everything in Parts A and B of `completeRealisationPlan.md`, Phase 2.5
(`design/api.md`), and §10's wind split/GUI work is done. What's left:
INT-05 (Part C — Integration Test), blocked on the real DUT's firmware, and
a short list of known refinements. See `design/whatsNext.md`.

---

## 10. Wind speed/direction split + GUI restructure (2026-07-02)

Done after Phase 3's integration testing (§9) wrapped up. Full detail:
`design/whatsNext.md` §2.5 (the canonical writeup — this section is a
shorter pointer so this document doesn't go stale relative to that one
again). Summary:

- Wind speed and wind direction turned out to be physically separate DUT
  units at separate addresses, not one combined device — `wind_poll`'s
  core, its FreeRTOS task, the `/wind/*` and `/api/v1/wind` endpoints, and
  the NVS keys (`wind_speed_addr`/`wind_dir_addr`, replacing one shared
  `wind_test_addr`) are all now parameterised on sensor type.
- GUI restructured into Status (top, pinned) → tab bar (Bus Scanner / Wind
  Speed / Wind Direction / Register Explorer / System Settings) → Modbus
  Log (bottom, pinned). Bus Scanner presets removed. Modbus Log timestamps
  now `HH:MM:SS`, cap raised 30→50. System Settings' Modbus Timeout/Retries
  pre-populate from live status instead of loading empty.
- Two real bugs found and fixed along the way, neither part of the
  original ask: the WebSocket log broadcaster silently dropped TX entries
  for fast one-off transactions (only ever looked at the single newest
  entry per tick), and `GET /api/v1/spec`'s DUT register snapshot still
  described the pre-split combined register map.
- **142/142 native tests pass**, hardware build/flash/live-verification
  done for every piece.

---

## 11. Wind register map reconciled against DUT TDS v0.6 (2026-07-02)

The DUT's (`windmeters-modbus-interface`) Technical Design Specification had
matured to v0.6 since §10's wind split was built, and settled on a
materially different register model (FR-MB27): both Wind Speed and Wind
Direction firmware builds now implement one *identical* 12-input
(`0x0000`–`0x000B`) + 4-holding (`0x0000`–`0x0003`) register map at the same
addresses — a register the active build's sensor doesn't have just reads 0,
rather than each type having its own differently-addressed map as §10
assumed. Requested as "verify the register maps (regenerate them) against
the TDS"; touched code, tests, GUI, and every design doc that described the
old per-type layout.

- **`wind_poll` core rewritten**: one `wind_poll_decode()` for both types
  reading the full 12-register block into a unified `wind_reading_t`
  (instant/avg dir+speed, raw diagnostic, gust, seconds-since-pulse, a
  direction-fault flag for the FR-S38 sensor-fault sentinel `65535`);
  `wind_config_field_register()` dropped its `type` parameter — all 4
  holding registers exist at the same addresses on both builds now, so
  there's nothing left for `type` to disambiguate. `test_wind_poll` rewritten
  to 17 cases (full-block decode both types, 3 fault-sentinel cases, fixed
  register counts, flat config mapping, low-speed-cutoff encode/decode).
- **Device-address register removed entirely** (FR-MB07/FR-MB26) — the
  Modbus slave address is hardware-configured only (build define + PC4
  solder jumper) and was never actually readable/writable on the real DUT;
  the tester's "Device address" write field is gone from both Wind tabs.
- **New fields surfaced**: gust and seconds-since-pulse (Wind Speed raw
  card); a low-speed-cutoff holding register (0.1 m/s, 0–50, default 4 —
  previously just a "planned" TODO, now a real config field with its own
  Write button); the direction-fault sentinel (red badge on Wind Direction
  when the pot wiper floats for >2s).
- **Scoping decision**: measurement-relevant fields got full decode + GUI
  support; pure protocol/device diagnostics that also live in the same
  12-register block (status bitfield, identification, uptime, CRC-error
  count, request count) stay documented and reachable via Register Explorer
  but without dedicated decode/GUI, to avoid the task growing into a full
  diagnostics dashboard.
- **Real bug found and fixed along the way**: `GET /api/v1/spec`'s response
  buffer (`char buf[2048]`) truncated once `DUT_REGISTER_SNAPSHOT_JSON` grew
  to describe the full 12+4-register map with `active_on` tags — caught via
  a real `ConvertFrom-Json` parse error on hardware at byte 2047. Bumped to
  `char buf[4096]`; re-verified valid JSON with correct register counts live
  on the device.
- Docs brought in line with the new model: `manual/windTesters.md` (full
  rewrite), `scratchbook.md` §5/§6.2/§7, `api.md` §4.2/§5.1/§5.5/§6 (worked
  examples use real computed CRC16s, per this project's practice of never
  leaving fabricated-looking hex in docs), `completeRealisationPlan.md`
  TASK-WIND, `whatsNext.md` §4.
- **143/143 native tests pass.** No version bump yet — this is a
  correctness fix to match the DUT's spec, not new tester capability, so it
  would land as a patch (0.4.2 → 0.4.3) if/when tagged; not yet requested.

---

*End of Progress Snapshot*
