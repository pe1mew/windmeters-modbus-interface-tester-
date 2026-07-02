# Changelog

All notable changes to the Windmeters Modbus Interface Tester are documented
in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/), and
this project adheres to [Semantic Versioning](https://semver.org/).

---

## [Unreleased]

### Added

- Firmware version tracking: `platformio.ini`'s `[env:windmeterTester]`
  now sets a `FIRMWARE_VERSION` build flag, surfaced as `fw_version` in
  both the WebSocket `type:"status"` payload and `GET /api/v1/status`,
  and displayed in the web UI's footer (e.g. `v0.4.0`) — useful for
  confirming an OTA/reflash landed the build you expected.

### Changed

- Web UI footer: the firmware version is now separated from the app name
  with a middot (`Windmeters Modbus Interface Tester · v0.4.0`) instead of
  just a space.
- Wind Speed / Wind Direction tabs: the "only one sensor can poll at a
  time" hint now has proper vertical spacing before the live-value tiles
  below it (previously butted directly against them with no gap) —
  matches the spacing rhythm used elsewhere in the GUI.

---

## [0.4.0] - 2026-07-02

### Added

- Initial firmware implementation for M5Stack AtomS3 + Atomic RS485 Base
  (PlatformIO, Arduino framework):
  - Modbus RTU master core (`mb_core`): CRC16, frame build/parse,
    FC03/FC04/FC06/FC16, timeout and retry handling.
  - Single-owner `modbus_master_task` serialising all RS-485 transactions
    through a queue.
  - Bus scanner (`bus_scan` + `scan_task`): address-range sweep with live
    progress over WebSocket.
  - Wind speed/direction poller (`wind_poll` + task) — see the 2026-07-02
    speed/direction split below for its current, split shape.
  - Modbus traffic ring-buffer log (`mb_log`) streamed to the web UI.
  - Web server (HTTP + WebSocket) serving the vanilla HTML/CSS/JS UI from
    SPIFFS.
  - WiFi manager (AP/STA auto-switch, mDNS), NTP time sync, RGB status LED
    (WS2812B at GPIO35), and typed NVS config persistence (`cfg`).
- Design documentation in `design/` (scratchbook, realisation plans,
  progress snapshot) and hardware reference material in `documentation/`
  (AtomS3, Atomic RS485 Base).
- Repository documentation: README, changelog, contributing guidelines,
  code of conduct, and license information.
- **Machine API** (`design/api.md`, `/api/v1/*`) — a machine-first
  JSON-over-HTTP API alongside the human web UI, so external tools and LLM
  clients (e.g. Claude) can send Modbus requests and receive replies in a
  single self-contained round trip. All 7 endpoints implemented and
  hardware-verified: `POST /api/v1/modbus` (the core transaction endpoint —
  raw TX/RX frames, machine-readable `status` plus a human/LLM-readable
  `hint` on failure), `GET /api/v1/spec` (self-description, so a client
  given only the base URL can bootstrap a session), `GET /api/v1/status`,
  `POST`/`GET /api/v1/scan`, `GET /api/v1/log`, `GET /api/v1/wind`.
  Verified against a real FG6485A sensor connected to the bench (humidity/
  temperature reads, Modicon-address conversion, function aliases, a full
  bus sweep) as well as native unit tests.
- **Wind speed/direction split (2026-07-02).** Wind speed (cup anemometer)
  and wind direction (potentiometer) turned out to be physically separate
  DUT units at separate slave addresses (30/35 and 31/36), not one combined
  device — confirmed against the DUT's own hardware documentation. The web
  UI now has separate **Wind Speed** and **Wind Direction** tabs, each with
  its own register-map reference table, live values, and config form;
  `wind_poll`'s decode/config core, `wind_poll_task`, and the `/wind/*` +
  `GET /api/v1/wind` endpoints are all parameterised on sensor type, and
  each type only ever touches the registers its own firmware implements.
  Only one type polls at a time. NVS gained separate `wind_speed_addr`/
  `wind_dir_addr` keys, replacing the original single `wind_test_addr`.
- **GUI restructure (2026-07-02).** Status pinned to the top of the page
  and Modbus Log pinned to the bottom (both always visible); Bus Scanner,
  Wind Speed, Wind Direction, Register Explorer, and System Settings
  (renamed from WiFi Settings, now also covering Modbus timeout/retries)
  moved into a tab bar between them.
- Host-side (native) unit tests for every library — `pio test -e native`,
  **142 tests across 10 suites** (up from 91 at initial implementation).

### Changed

- Modbus traffic log: GUI-facing timestamps changed from raw milliseconds
  to `HH:MM:SS`; displayed/retained entry count raised from 30/32 to 50
  (`MB_LOG_CAPACITY`) — the old cap was too short to keep a whole scan or a
  short bench session in view at once.
- System Settings' Modbus Timeout/Retries fields now pre-populate from the
  live (NVS-backed) device configuration instead of loading empty.
- Bus Scanner's quick-preset row (1/30/31/35/36/44) removed — S200/address
  44 is permanently out of scope (will never be connected), and the
  remaining windmeter presets stopped earning their keep once Wind Speed
  and Wind Direction each got their own tab with the right default address
  already filled in.
- `GET /api/v1/wind` now takes a `?type=speed|direction` query parameter
  and returns only that sensor's fields (was one object carrying both);
  `GET /api/v1/spec`'s `dut_register_snapshot` is now keyed by
  `wind_speed`/`wind_direction` for the same reason.

### Fixed

- Two correctness fixes to existing code, surfaced while implementing the
  machine API (neither a new bug in the API's own code — both were latent
  gaps it was the first thing to actually exercise): `mb_result_t` could
  return a *different* transaction's raw TX/RX bytes if another request
  ran before a caller on the far side of a FreeRTOS queue hop got
  scheduled; `modbus_master_task` unconditionally overwrote a caller's
  per-request `timeout_ms`/`retries` override with the NVS defaults.
- Real bug in `POST /api/v1/scan`'s `wait:true` poll loop: it treated any
  non-running state as "done," including a stale `SCAN_COMPLETE` left over
  from the *previous* scan — every scan after the first returned instantly
  with the prior scan's result.
- WebSocket log broadcaster silently dropping TX log entries: it only ever
  checked the single newest ring-buffer entry once per second, and a
  single Modbus transaction logs its TX entry immediately followed by an
  RX entry sharing the same timestamp — for a fast one-off request (the
  common case for Register Explorer) the TX was almost always overtaken by
  RX as "newest" before the next broadcast tick looked. Not a data-loss
  bug (`GET /api/v1/log` reads the full ring buffer and was unaffected) —
  only the live WebSocket tail the GUI watches. Fixed with a monotonic
  `mblog_total_appended()` counter so the broadcaster catches up on every
  entry since its last tick, not just the newest one.
- `GET /api/v1/spec`'s `dut_register_snapshot` still described the
  pre-split combined 5-input/4-holding register map after the wind split
  landed — a hand-maintained constant that nothing regenerates
  automatically, missed at the time. Any client (including an LLM)
  bootstrapping a session from `/api/v1/spec` alone was getting a register
  layout that no longer existed.
- `design/api.md`'s worked examples carried fabricated-looking (not
  actually computed) Modbus CRC16 bytes in several `raw_tx`/`raw_rx`
  fields — corrected to real computed values.
- `/config/wifi` only saved new credentials to NVS without ever triggering
  a reboot to apply them, so submitting WiFi settings from the AP-mode web
  UI silently did nothing until an unrelated reboot happened to pick them
  up — found via the documented Phase 3 integration-test flow, not a
  synthetic test. Now calls `ESP.restart()` after saving.
- NVS key-length bug: `CFG_KEY_SCAN_RANGE_START` (16 chars) and
  `CFG_KEY_WIND_POLL_INTERVAL` (21 chars) both exceeded the Arduino
  `Preferences` 15-character key limit, so writes to either silently never
  persisted — every mock-backed native test passed throughout, since only
  a real `Preferences` backend enforces the limit. Fixed by shortening
  both keys and adding a native test that checks every `cfg_keys.h`
  constant's length directly.

### Notes

- Bench integration testing against a real Modbus bus peer: **done**, 8 of
  9 tests (INT-01–04, 06–09) against a real FG6485A sensor connected to
  the bench — see `design/whatsNext.md` §3. Only INT-05 (wind decode
  accuracy against the real `windmeters-modbus-interface` DUT) remains,
  blocked on that board's own firmware implementing a register map.
