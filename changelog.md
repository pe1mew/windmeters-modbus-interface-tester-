# Changelog

All notable changes to the Windmeters Modbus Interface Tester are documented
in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/), and
this project adheres to [Semantic Versioning](https://semver.org/).

---

## [Unreleased]

### Changed

- Wind register map reconciled against the DUT's Technical Design
  Specification, which matured to v0.6 (FR-MB27) since the wind
  speed/direction split: both sensor types now share one identical
  12-input-register (`0x0000`–`0x000B`) + 4-holding-register
  (`0x0000`–`0x0003`) map at the same addresses, rather than each type
  having its own differently-shaped map as previously assumed.
  `wind_poll_decode()` now reads the full register block into a unified
  `wind_reading_t` for both types; `wind_config_field_register()` dropped
  its `type` parameter since there's no longer a per-type address to
  disambiguate.
- `manual/windTesters.md`, `design/scratchbook.md`, `design/api.md`,
  `design/completeRealisationPlan.md`, and `design/whatsNext.md` updated to
  match — including `api.md`'s worked examples, recomputed with real CRC16
  values.

### Added

- Low-speed-cutoff holding register (`0x0003`, 0.1 m/s, range 0–50, default
  4) — previously documented as a "planned, not yet implemented" TODO, now
  a real config field with its own Write button on the Wind Speed tab.
- Gust and seconds-since-pulse input registers surfaced on the Wind Speed
  tab's Raw card.
- Direction sensor-fault sentinel (`65535` on `dir_instant`/`dir_avg`, per
  FR-S38 — a floating potentiometer wiper for >2 s) surfaced as a red
  "Sensor fault" badge on the Wind Direction tab.
- Comprehensive Doxygen documentation across all of `firmware/lib/` and
  `firmware/src/` (comments only, no behavioural change) — every public
  header declaration, struct/enum member, `#define` constant, and internal
  `static` helper now has a complete `@brief`/`@param`/`@return`. A new
  root-level `Doxyfile` (output to `docs/doxygen/`, gitignored) builds it
  with `WARN_IF_UNDOCUMENTED`/`WARN_NO_PARAMDOC` enabled as the completeness
  gate — the tree now builds with zero Doxygen warnings.

### Removed

- "Device address" holding-register write field on both Wind tabs. TDS v0.6
  (FR-MB07/FR-MB26) confirmed the Modbus slave address is hardware-configured
  only (compile-time build define + PC4 solder jumper) and was never
  actually readable or writable over Modbus — the field the tester exposed
  never worked against the real DUT.

### Fixed

- `GET /api/v1/spec`'s response buffer (`char buf[2048]`) truncated once
  the DUT register snapshot grew to describe the full 12+4-register map —
  caught via a real `ConvertFrom-Json` parse failure on hardware at byte
  2047. Bumped to `char buf[4096]`.
- Two pre-existing comments contained a literal `/*` (`main.cpp`'s
  "`lib/*`") or an unescaped `@file` mention in prose, which Doxygen's
  parser misreads as a nested comment opener / a broken `\file` command —
  found while building the Doxygen pass above, reworded to plain text.

---

## [0.4.2] - 2026-07-02

### Fixed

- The first Modbus transaction(s) after every boot could fail with a CRC
  or framing error (or, depending what's on the bus, an empty scan
  result) — `mb_transport_arduino_init()` called `Serial2.begin()` with
  no RX flush, so a bench-confirmed stray byte left sitting in the buffer
  got consumed as the leading byte of the first real read, shifting every
  byte after it by one position. Root-caused and fixed by flushing the RX
  buffer once, right after `begin()`. Verified by direct before/after
  reproduction on real hardware — see `memory/gotcha-log.md`.

---

## [0.4.1] - 2026-07-02

### Removed

- `CFG_KEY_TZ_POSIX`/`CFG_DEFAULT_TZ_POSIX` NVS key — declared but never
  actually read or written anywhere, found via a full dead-code audit.
  `completeRealisationPlan.md`'s own TASK-NTP writeup already called
  timezone handling vestigial; the code just hadn't caught up.
- `ntp_to_epoch()` (the Arduino-only wrapper in `ntp_task.h`) — also found
  dead by the same audit. Rather than wire callers through it, the fix
  below calls the pure `ntp_manager_millis_to_epoch()` it wrapped directly
  from `web_core.cpp`, which made the wrapper itself redundant.

### Fixed

- `GET /api/v1/log` entries now use real ISO-8601 UTC timestamps once NTP
  is synced, matching `design/api.md` §3's already-documented contract —
  previously always reported raw uptime-ms with `"clock":"uptime"`
  regardless of sync state, a gap the dead `ntp_to_epoch()` above turned
  out to have been built for and never finished wiring in.

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
- Firmware version tracking: `platformio.ini`'s `[env:windmeterTester]`
  sets a `FIRMWARE_VERSION` build flag, surfaced as `fw_version` in both
  the WebSocket `type:"status"` payload and `GET /api/v1/status`, and
  displayed in the web UI's footer (e.g. `v0.4.0`) — useful for confirming
  an OTA/reflash landed the build you expected. This is the project's
  first tagged version.

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
- Web UI footer: the firmware version is now separated from the app name
  with a middot (`Windmeters Modbus Interface Tester · v0.4.0`) instead of
  just a space.
- Wind Speed / Wind Direction tabs: the "only one sensor can poll at a
  time" hint now has proper vertical spacing before the live-value tiles
  below it (previously butted directly against them with no gap) —
  matches the spacing rhythm used elsewhere in the GUI.

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
