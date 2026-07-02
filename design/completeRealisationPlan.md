# Complete Realisation Plan

## Windmeters Modbus Interface Tester

| Field        | Value                                    |
|--------------|-------------------------------------------|
| Document     | Complete Realisation Plan                 |
| Project      | Windmeters Modbus Interface Tester (entire project) |
| Version      | 0.1 (draft)                               |
| Date         | 2026-07-01                                |
| Status       | Draft                                     |
| Related docs | `design/scratchbook.md` (design source of truth — hardware, register map, decisions log), `design/realisationPlan.md` (Modbus Master deep-dive — MB-1/MB-2 are summarised here, detailed there) |

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [Part A — Libraries](#2-part-a--libraries)
   - [LIB-NVS — Configuration Persistence](#lib-nvs--configuration-persistence)
   - [LIB-LED — RGB Status LED](#lib-led--rgb-status-led)
   - [LIB-LOG — Modbus Traffic Log](#lib-log--modbus-traffic-log)
   - [LIB-MB — Modbus Master Core](#lib-mb--modbus-master-core-see-realisationplanmd)
3. [Part B — FreeRTOS Tasks](#3-part-b--freertos-tasks)
   - [Group 1 — Connectivity](#group-1--connectivity)
   - [Group 2 — Modbus Core](#group-2--modbus-core)
   - [Group 3 — Application Logic](#group-3--application-logic)
   - [Group 4 — Web / UI](#group-4--web--ui)
4. [Part C — Integration Test](#4-part-c--integration-test)
5. [Dependency Overview](#5-dependency-overview)
6. [Open Issues That Block Implementation](#6-open-issues-that-block-implementation)

---

## 1. Introduction

This plan sequences the **entire** tester project — everything in
`scratchbook.md` — into buildable, testable units. It sits one level below
the scratchbook (which is the design record: hardware, register map,
decisions and their reasoning) and one level above `realisationPlan.md`
(which is the deep-dive on the Modbus master specifically). Where this
document and `realisationPlan.md` overlap (MB-1, MB-2), this document gives
only a summary and a pointer — implement against the detailed version, not
this one, to avoid the two drifting apart.

### Guiding principles

- **The non-Modbus infrastructure is a straight port, not a redesign.**
  WiFi, mDNS, NTP, NVS, the RGB LED convention, and the FreeRTOS
  task-per-concern shape all come from
  `greenhouse-Controller-Modbus-sensor-emulator` largely unchanged
  (`scratchbook.md` §3). Implementation effort belongs on the parts that
  actually changed: the Modbus master engine and the new web sections (Bus
  Scanner, Wind Speed, Wind Direction, Register Explorer).
- **The GUI is the template's, ported — not redesigned.** `firmware/data/`
  (`index.html`, `style.css`, `app.js`) is vanilla HTML/CSS/JS: one
  scrollable page with a `<section>` per feature, no framework, no build
  step. New tester features are new sections in that same file, styled
  with the existing CSS and wired up through the existing `app.js` helpers
  and `type`-keyed WebSocket router (`scratchbook.md` §7) — not a parallel
  frontend.
- **Library first, unit test before integration** — same discipline as
  greenhouse-Controller's own `implementationPlan.md`: encapsulate each
  concern in a library with a clean API, test what can be tested on the
  host (pure logic, no hardware), and only reach for target hardware where
  the test genuinely needs it (UART timing, WiFi, the LED).
- **One task owns the UART, full stop.** `modbus_master_task` is the only
  code path that touches the RS-485 UART. Every other task or web request
  that needs the bus goes through its queue (`scratchbook.md` §6.2,
  `realisationPlan.md` §1).
- **Register map is provisional; don't build around today's values.** The
  DUT's register map is 4 commits old and still moving
  (`scratchbook.md` §5). The Register Explorer — generic, not hard-coded —
  is what survives that; the Wind Speed/Direction tabs' fixed decode is a
  convenience layer on top of it, expected to need updates as the DUT
  evolves.

---

## 2. Part A — Libraries

### LIB-NVS — Configuration Persistence

**Description:** Typed wrapper around ESP32 NVS via the Arduino
`Preferences` library (this project is `framework = arduino`, unlike
greenhouse-Controller's ESP-IDF `nvs_config` — same underlying flash
storage, different wrapper). Persists every key in `scratchbook.md` §7's
NVS table: `wifi_ssid`, `wifi_pass`, `ntp_server`, `mb_baud`,
`mb_timeout_ms`, `mb_retries`, `scan_range_start`, `scan_range_end`,
`wind_speed_addr`, `wind_dir_addr`, `wind_poll_interval_ms`. (The last two
wind keys were one shared `wind_test_addr` before the 2026-07-02 speed/
direction split — see TASK-WIND below. `tz_posix` was dropped the same
day — declared but never actually read/written anywhere; genuinely dead
per the note below TASK-NTP.)

**API surface:**

```c
void     cfg_init();                                    // opens the Preferences namespace, seeds defaults on first boot
uint32_t cfg_get_u32(const char *key, uint32_t def);
void     cfg_set_u32(const char *key, uint32_t val);
uint16_t cfg_get_u16(const char *key, uint16_t def);
void     cfg_set_u16(const char *key, uint16_t val);
uint8_t  cfg_get_u8 (const char *key, uint8_t def);
void     cfg_set_u8 (const char *key, uint8_t val);
String   cfg_get_str(const char *key, const String &def);
void     cfg_set_str(const char *key, const String &val);
void     cfg_reset_defaults();
```

**Task compatibility:** No internal locking — `Preferences`/NVS is safe for
single-threaded access per key, but callers writing from multiple tasks
(e.g. web server writing `wind_speed_addr`/`wind_dir_addr` while
`wind_poll_task` reads them)
must not assume atomicity across a get+modify+set sequence. In practice
only the web server writes; every other task only reads.

**Unit tests** (host, stub the `Preferences` backend with an in-memory map):

- `test_cfg_defaults_on_first_boot` — empty backing store; verify every key
  above returns its documented default
- `test_cfg_round_trip_u32` / `_u16` / `_u8` / `_str` — write then read back,
  verify equality
- `test_cfg_reset_restores_defaults` — write non-default values, reset,
  verify defaults return
- `test_cfg_persists_across_reinit` — write a value, destroy and recreate
  the wrapper against the same backing store, verify the value survives
  (simulates a reboot)

---

### LIB-LED — RGB Status LED

**Description:** FastLED wrapper for the single WS2812B at **GPIO35**
(`scratchbook.md` §4.1, confirmed via `blinkyS3`). Ports the template's
exact colour convention: idle = steady blue, valid Modbus frame = brief
green pulse, recoverable error (CRC/timeout) = brief red pulse,
unrecoverable fault = solid red latched until explicitly cleared. Adds one
state the template didn't need: **scanning**, shown while the Bus Scanner
sweep is active, so the LED distinguishes "idle and waiting" from "actively
sweeping 247 addresses."

**API surface:**

```c
void led_init();
void led_set_idle();        // steady blue
void led_set_scanning();    // distinct pattern while a bus scan is running
void led_pulse_valid();     // brief green blink, then returns to current base state
void led_pulse_error();     // brief red blink (CRC/timeout), then returns to current base state
void led_set_fault();       // solid red, latched — only led_set_idle() clears it
```

**Task compatibility:** Called from `modbus_master_task` (valid/error
pulses), `scan_task` (scanning/idle), and boot code (init/idle). No shared
mutable state beyond the FastLED buffer itself, which is only ever written
from these calls — not written concurrently since MB-2 serialises all
Modbus transactions (§3 below).

**Unit tests** (host-runnable against a stubbed FastLED backend that
records the last colour written, since the actual WS2812B timing can only
be checked on target):

- `test_led_idle_is_blue`
- `test_led_valid_pulses_green_then_returns_to_base_state`
- `test_led_error_pulses_red_then_returns_to_base_state`
- `test_led_fault_latches_solid_red_until_idle_called`
- `test_led_scanning_distinct_from_idle`

**Target-hardware check (visual, not automated):** confirm all five states
are visually distinguishable at actual brightness (`FastLED.setBrightness`)
under normal bench lighting.

---

### LIB-LOG — Modbus Traffic Log

**Description:** Fixed-capacity ring buffer of Modbus frame log entries
(TX and RX), decoupled from `modbus_master_task` by a FreeRTOS queue so a
slow WebSocket client can never stall the bus — same decoupling principle
as greenhouse-Controller's own event logger (T9: "decouples all log I/O
from real-time tasks"). Feeds the Modbus Log web page.

**API surface:**

```c
typedef struct {
    uint32_t timestamp_ms;   // from ntp_task-synced clock
    bool     is_tx;          // true = we sent it, false = response received
    uint8_t  raw[256];
    uint8_t  raw_len;
    char     summary[64];    // e.g. "FC04 addr31 start0x0000 cnt5 -> OK"
} mb_log_entry_t;

void   mblog_init(size_t capacity);
void   mblog_append(const mb_log_entry_t *entry);
size_t mblog_get_recent(mb_log_entry_t *out, size_t max_count);  // newest first
void   mblog_clear();
```

**Task compatibility:** `mblog_append()` is called only from
`modbus_master_task`; `mblog_get_recent()` / `mblog_clear()` only from
`web_server_task`. Single-producer/single-consumer via an internal queue —
no external mutex required at the call site.

**Unit tests** (host):

- `test_mblog_append_and_read_order` — append 3 entries, verify
  `get_recent` returns them newest-first
- `test_mblog_ring_wrap_drops_oldest` — fill beyond capacity, verify the
  oldest entry is gone and the newest `capacity` entries remain
- `test_mblog_clear_empties_buffer`

---

### LIB-MB — Modbus Master Core (see `realisationPlan.md`)

**Description (summary only):** CRC16, FC03/04/06/16 frame build/parse,
timeout/retry, exception decoding. Runs on `G5`(RX)/`G6`(TX), 9600 8N1, no
DE/RE GPIO (hardware auto-direction — `scratchbook.md` §4.2). Raw 0-based
addressing throughout (`scratchbook.md` §5/§9).

**Full API surface, unit tests, and the `modbus_master_task` (MB-2) that
wraps it: see `realisationPlan.md` §2–§3.** Not reproduced here — this
entry exists so the dependency diagram in §5 below is complete without
requiring a second document open side by side.

---

## 3. Part B — FreeRTOS Tasks

### Group 1 — Connectivity

No dependencies on the Modbus side; can be built and bench-tested before
any RS-485 hardware is wired up.

---

#### TASK-WIFI — WiFi Manager

**Depends on:** LIB-NVS
**Depends on tasks:** none

**Implementation** (straight port, `scratchbook.md` §3/§6.2):

- On boot with no stored credentials: start AP `WindmeterTester-<last2MACbytes>`
  (open, matching the template's `SensorEmulator-AABB` pattern).
- If `wifi_ssid`/`wifi_pass` are set in NVS: attempt STA connect; on
  success, disable the AP and register mDNS as `windmeter-tester.local`; on
  failure, remain in AP mode so the device is never unreachable.
- Web interface (Group 4) is reachable in either mode.
- Publishes connection state (mode, SSID, IP, RSSI) for the Status page and
  the WebSocket push.

**Acceptance tests** (target):

- `test_wifi_starts_ap_without_credentials`
- `test_wifi_connects_sta_with_valid_credentials`
- `test_wifi_falls_back_to_ap_on_bad_credentials`
- `test_wifi_mdns_resolves_in_sta_mode`

---

#### TASK-NTP — Time Sync

**Depends on:** TASK-WIFI (STA connected)
**Depends on tasks:** TASK-WIFI running

**Implementation:** NTP sync against the configurable `ntp_server` (default
`pool.ntp.org`), manual time-set fallback via the web UI. Used only to
timestamp `LIB-LOG` entries and display local time on the Status page —
narrower scope than the template, since the template also needed
DST-aware local time for CSV Replay comparison, and Replay mode was
dropped (`scratchbook.md` §3). No timezone/lat-lon handling of any kind
exists here — everything is UTC (`configTime(0, 0, server)`, no DST). The
`tz_posix` NVS key inherited from the template's key list was a remnant of
that dropped scope; removed 2026-07-02 once a dead-code audit confirmed it
was declared but never read or written anywhere.
`test_log_timestamps_use_synced_time` below is now implemented:
`GET /api/v1/log` entries use real ISO-8601 UTC timestamps
(`ntp_manager_millis_to_epoch()`) once synced, uptime-ms with a
`"clock":"uptime"` tag otherwise — see `web_core_build_api_log_json()`.

**Acceptance tests** (target):

- `test_ntp_syncs_on_sta_connect`
- `test_ntp_manual_time_set_via_api`
- `test_log_timestamps_use_synced_time`

---

### Group 2 — Modbus Core

#### TASK-MB — Modbus Master Task (MB-2)

**Depends on:** LIB-MB, LIB-LOG, LIB-LED
**Depends on tasks:** none (foundation task — Group 3 depends on this, not
the reverse)

Full detail in `realisationPlan.md` §3. Summary: owns the UART exclusively,
serves a request queue so `scan_task`, `wind_poll_task`, and the Register
Explorer never open the port themselves, appends every frame to `LIB-LOG`,
drives `LIB-LED`'s valid/error pulses, and re-reads `mb_baud` /
`mb_timeout_ms` / `mb_retries` from `LIB-NVS` before each transaction.

---

### Group 3 — Application Logic

Both tasks below are consumers of TASK-MB's queue — neither touches the
UART directly.

---

#### TASK-SCAN — Bus Scanner

**Depends on:** TASK-MB, LIB-LED (drives the "scanning" state)
**Depends on tasks:** TASK-MB running

**Implementation:**

- On a start request: sweep the configured address range (`scan_range_start`
  … `scan_range_end`, NVS default 1–247), submitting one lightweight probe
  per address to TASK-MB — an FC04 read of 1 register at address `0x0000`,
  treating a Modbus exception reply as "something answered" (a device is
  there, just not happy with that request) rather than as a scan failure.
- Uses the same `mb_timeout_ms` as everything else (no separate faster scan
  timeout in v1) — worst case, an empty 1–247 sweep takes ~247 ×
  (200 ms + 1 retry) ≈ under a minute. Acceptable for v1; a shorter
  scan-specific timeout is a candidate refinement if that proves annoying
  in practice, not a v1 requirement.
- After every address: updates shared scan state under mutex and streams
  progress (`scan.current_addr`, `scan.found[]`) to the WebSocket
  (`scratchbook.md` §7 JSON shape).
- Cancellable mid-sweep via a stop request.
- Calls `led_set_scanning()` on start, `led_set_idle()` on completion or
  cancellation.

**Acceptance tests** (target — bench peer required, see §4):

- `test_scan_finds_known_address` — one known-responding device on the bus;
  verify it appears in `found[]`
- `test_scan_reports_progress_incrementally` — verify the WebSocket
  receives a monotonically increasing `current_addr`
- `test_scan_cancellable_mid_sweep`
- `test_scan_empty_bus_completes_cleanly` — no devices present; verify the
  scan finishes (doesn't hang) with an empty `found[]`
- `test_scan_treats_exception_reply_as_found` — a device that replies with
  a Modbus exception to the probe is still reported as present

---

#### TASK-WIND — Wind Speed / Wind Direction Poller

**Depends on:** TASK-MB
**Depends on tasks:** TASK-MB running

**Split 2026-07-02:** originally specified as one "Wind Test" poller
targeting a single combined 5-input/4-holding register block. Reading the
DUT's own hardware docs more closely showed wind speed and wind direction
are physically separate units at separate addresses, each implementing
only its own registers — not one device exposing both sensors together.
The task below is now parameterised on a `wind_sensor_type_t`
(`WIND_SENSOR_SPEED` / `WIND_SENSOR_DIRECTION`); only one type is ever
polling at a time (same "one target" constraint as originally planned, now
expressed per-type rather than per-field). Full register maps:
`scratchbook.md` §5.

**Implementation:**

- While active for a given type and address (`wind_speed_addr` or
  `wind_dir_addr`, NVS): polls that type's input registers — 3 registers
  (`0x0000`–`0x0002`: speed_instant, speed_avg, raw_pulses) for
  WIND_SENSOR_SPEED, 2 registers (`0x0000`–`0x0001`: dir_instant, dir_avg)
  for WIND_SENSOR_DIRECTION — one FC04 call every `wind_poll_interval_ms`
  (NVS, default 1000 ms, shared by both types).
- Decodes with the DUT's ×10 scale factor — **not** the S200's ×1000
  (`scratchbook.md` §5 scaling gotcha) — into the fields that type's
  firmware actually has; the other type's fields in the shared
  `wind_reading_t` struct are left zeroed rather than split into two
  struct types, to avoid cascading the split through every JSON
  builder/GUI element for no behavioural benefit.
- Updates shared wind state under mutex; feeds the WebSocket `wind.*`
  fields (tagged `sensor_type`), including `age_ms` since the last
  successful poll so the UI can show staleness.
- On a config-read request: FC03 reads that type's 3 holding registers
  (`0x0000`–`0x0002`). On a config-write request: FC06 write-back of the
  single changed field — device address and averaging window exist on
  both types; direction offset is Direction-only, measurement window is
  Speed-only (`wind_config_field_register()` returns "field doesn't exist
  for this type," never touching the wire, if asked for the wrong one) —
  never a blind FC16 of everything, so a typo in one field can't clobber
  the others.
- Suspended (no polling, no bus traffic) when neither Wind tab is active;
  switching which type is active (e.g. starting Speed while Direction was
  running) stops the previous type's polling — there is exactly one
  `s_active_type`, not two independently-running pollers.

**Acceptance tests** (target):

- `test_wind_poll_decodes_speed_registers_correctly` /
  `test_wind_poll_decodes_direction_registers_correctly` — known register
  values on the bench target (or a temporary mock slave, see §4); verify
  e.g. raw `1834` → `183.4°`, and that decoding one type leaves the other
  type's fields at zero
- `test_wind_poll_respects_interval`
- `test_wind_config_write_fc06_single_field` — change only the offset;
  verify exactly one FC06 frame is sent, for that register only, and that
  writing a field that doesn't exist for the active type never touches
  the wire
- `test_wind_poll_suspended_when_no_tab_active` — verify zero bus traffic
  from this task while both tabs are closed

---

### Group 4 — Web / UI

#### TASK-WEB — Web Server

**Depends on:** LIB-NVS, LIB-LOG, TASK-MB (for Register Explorer queries),
TASK-SCAN, TASK-WIND
**Depends on tasks:** TASK-WIFI (must be reachable to serve anything),
TASK-MB, TASK-SCAN, TASK-WIND all running

**Implementation:**

- **Port `firmware/data/` from the template first**: copy `index.html`,
  `style.css`, `app.js` as the starting point, before writing any new
  section. Strip the FG6485A/S200/Replay sections (dropped, `scratchbook.md`
  §3); keep the page shell, the Status section skeleton, the Modbus Log
  section, the WiFi Settings section, and every helper function in
  `app.js` (`post()`, `setText()`, `setBadge()`, `setSliderInput()`, the
  WebSocket connect/reconnect logic, the `type`-keyed message router).
- Serves one `index.html` with a `<section>` per feature from
  `scratchbook.md` §7 — **not** six separate routes:

  | Section | Backing |
  |---|---|
  | Status (Home) | WiFi state (TASK-WIFI), NTP sync state (TASK-NTP), uptime. Pinned to the top of the page (2026-07-02 GUI restructure) — not one of the tabs below |
  | Bus Scanner | start/stop → TASK-SCAN; results table from scan state. One of the tabs between Status and Modbus Log |
  | Wind Speed / Wind Direction | start/stop/target-address → TASK-WIND, one tab each (2026-07-02 split — was a single "Wind Test" tab); live values + config form scoped to that type's own holding registers |
  | Register Explorer | one-shot request handler — submits directly to TASK-MB's queue, applies the Modicon→raw conversion (`scratchbook.md` §5 formula) before submission, returns decoded result + raw hex |
  | Modbus Log | reads `LIB-LOG` via `mblog_get_recent()`; clear button calls `mblog_clear()`. Pinned to the bottom of the page, not a tab. `HH:MM:SS` GUI timestamps, last 50 entries |
  | System Settings | SSID scan/select, password entry, NTP server, manual time, Modbus timeout/retries — writes via LIB-NVS. Grew from "WiFi Settings" once Modbus settings needed a home; timeout/retries pre-populate from the live status stream instead of loading empty |

- WebSocket at `/ws` pushes `type`-tagged JSON matching `app.js`'s existing
  router (`scratchbook.md` §7): `status` at ~1 s cadence, `scan` while a
  sweep runs, `wind` while the poller is active, `log`/`log_clear`
  unchanged from the template. New sections add a `case` to the existing
  router rather than a second dispatch mechanism.
- Register Explorer is the one section *not* driven by a background task —
  it's a direct request/response against TASK-MB's queue, since there's
  nothing to poll between one-shot queries.

**Acceptance tests** (target):

- `test_index_html_renders_all_sections` — one GET `/`; verify the response
  contains every section id from the table above (not six separate routes)
- `test_websocket_status_type_pushed_at_1hz`
- `test_websocket_scan_type_during_sweep`
- `test_websocket_wind_type_during_poll`
- `test_scan_start_stop_endpoints`
- `test_wind_start_stop_write_endpoints`
- `test_explorer_query_raw_address`
- `test_explorer_query_modicon_address` — submit `"40003"`, verify TASK-MB
  receives a request for raw address `0x0002`
- `test_wifi_settings_get_post`
- `test_log_clear_endpoint`

---

## 4. Part C — Integration Test

Requires the AtomS3 + Atomic RS485 Base bench setup from
`realisationPlan.md` §4. The real DUT (`windmeters-modbus-interface`) has
no register firmware yet, so a real FG6485A sensor stands in as a working
Modbus slave wherever a test doesn't specifically need the DUT's own
register map (originally planned as `greenhouse-Controller-Modbus-sensor-emulator`
— superseded by real hardware, see the scope decision below).

**Scope decision (2026-07-02):** S200 (address 44) is out of scope —
will never be connected, real or emulated. INT-03/04 below cover FG6485A
(address 1) only; this isn't a gap waiting on hardware, it's the full
intended scope. Address 1 also ended up being a genuine physical FG6485A
sensor rather than the sensor-emulator originally planned — strictly
stronger evidence for the FC03/Modicon mechanics INT-04 validates, since
it exercises real commercial firmware rather than a software stand-in.

| Test ID | Scenario | Bench target | Validation |
|---------|----------|---------------|------------|
| INT-01 | Cold boot, no NVS config | — | Device starts in AP mode; `index.html` loads with every section from TASK-WEB's table present |
| INT-02 | Configure WiFi via AP-mode web UI | — | Device reconnects in STA mode; `windmeter-tester.local` resolves |
| INT-03 | Full bus scan | FG6485A (address 1) | Bus Scanner reports the address; progress visible throughout |
| INT-04 | Generic register read via Register Explorer | FG6485A | FC03 read of FG6485A registers returns values matching an independent read — validates FC03 mechanics and the Modicon-conversion path, independent of the DUT's register map |
| INT-05 | Wind Speed / Wind Direction decode accuracy | Real DUT | **Blocked** (§6) — the FG6485A's registers use the wrong scale/layout to validate either DUT variant's decode (×10; 3 registers for Speed, 2 for Direction, `scratchbook.md` §5); this test can only be meaningful against the real DUT or a purpose-built mock of its exact register maps |
| INT-06 | Register Explorer write + read-back | FG6485A (alarm config, `0x000C`–`0x0013`) as a mechanics-only stand-in | FC16 write followed by FC03 read-back returns the written values — validates the write path generically; DUT-specific holding-register write-back (via the Wind Speed/Direction tabs' own config forms) is blocked the same way as INT-05 |
| INT-07 | Modbus Log ordering | Either | TX and RX frames from a scan appear correctly interleaved and timestamped |
| INT-08 | LED state transitions | Either | Idle → scanning → valid-frame/error states are visually correct and return to idle when the scan ends |
| INT-09 | Settings survive power-cycle | — | WiFi credentials, last scan range, and last Wind Speed/Direction addresses are all retained after a power-cycle |

Results for INT-03/04/06/07/09: `design/whatsNext.md` §3.2.

---

## 5. Dependency Overview

```
Libraries
├── LIB-NVS   Configuration persistence (Arduino Preferences)
├── LIB-LED   RGB status (FastLED, GPIO35)
├── LIB-LOG   Modbus traffic log (ring buffer + queue)
└── LIB-MB    Modbus Master Core          ← see realisationPlan.md (MB-1)

FreeRTOS Tasks
├── Group 1 (Connectivity)
│   ├── TASK-WIFI  WiFi Manager   ← LIB-NVS
│   └── TASK-NTP   Time Sync      │ needs TASK-WIFI
│
├── Group 2 (Modbus Core)
│   └── TASK-MB    Modbus Master  ← LIB-MB, LIB-LOG, LIB-LED   (= MB-2, realisationPlan.md)
│
├── Group 3 (Application Logic)
│   ├── TASK-SCAN  Bus Scanner         ← LIB-LED | needs TASK-MB
│   └── TASK-WIND  Wind Speed/Direction │ needs TASK-MB (one task, type-parameterised — §3 TASK-WIND)
│
└── Group 4 (Web / UI)
    └── TASK-WEB   Web Server     ← LIB-NVS, LIB-LOG | needs TASK-WIFI, TASK-MB, TASK-SCAN, TASK-WIND
           ├── Status (pinned top), Bus Scanner, Wind Speed, Wind Direction,
           │   Register Explorer, System Settings tabs, Modbus Log (pinned
           │   bottom) — one ported+restructured index.html
           ├── Register Explorer  → direct request/response against TASK-MB (no dedicated task)
           └── WebSocket status push (~1 Hz)
```

Groups 1 and 2 have no dependency on each other and can be built and bench
tested in parallel — WiFi/NTP need no RS-485 hardware, and TASK-MB (via
`realisationPlan.md` §4 steps 1–4) needs no WiFi.

---

## 6. Open Issues That Block Implementation

| Issue | Blocks | Status |
|-------|--------|--------|
| `windmeters-modbus-interface` firmware has no register implementation yet (`main.c` scaffolding only) | INT-05 (Wind Speed/Direction decode accuracy against the real DUT) and the DUT-specific half of INT-06 | **Open** — does not block any library, any task, or INT-01–04, 07–09 |
| ~~AtomS3 RS485 base RX/TX pin assignment~~ | ~~TASK-MB / LIB-MB UART init~~ | **Closed** — `G5`=RX, `G6`=TX (`scratchbook.md` §4.1/§4.2/§9) |
| ~~AtomS3 RGB LED GPIO~~ | ~~LIB-LED~~ | **Closed** — GPIO35, confirmed via `blinkyS3` |
| ~~PlatformIO board id~~ | ~~project scaffolding~~ | **Closed** — `m5stack-atoms3` |
| ~~Addressing convention (raw vs. Modicon)~~ | ~~LIB-MB API, Register Explorer~~ | **Closed** — raw canonical, applied in both this repo and upstream `windmeters-modbus-interface` |
| ~~Combined vs. split Wind Speed/Direction register maps~~ | ~~TASK-WIND~~ | **Closed 2026-07-02** — split into two type-parameterised maps, `scratchbook.md` §5/§9 |
| Multi-device Wind dashboard (several devices live on screen at once) | TASK-WIND (v2 scope only) | **Deferred to v2** per `scratchbook.md` §9 — not a v1 blocker. Not the same thing as the 2026-07-02 Wind Speed/Direction tab split, which is two *tabs* for two *sensor types*, still one active poll target at a time |
| Register Explorer saved queries | TASK-WEB (v2 scope only) | **Backlog** per `scratchbook.md` §9 — not a v1 blocker |

---

*End of Complete Realisation Plan v0.1*
