# Changelog

All notable changes to the Windmeters Modbus Interface Tester are documented
in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/), and
this project adheres to [Semantic Versioning](https://semver.org/).

---

## [Unreleased]

### Added

- Initial firmware implementation for M5Stack AtomS3 + Atomic RS485 Base
  (PlatformIO, Arduino framework):
  - Modbus RTU master core (`mb_core`): CRC16, frame build/parse,
    FC03/FC04/FC06/FC16, timeout and retry handling.
  - Single-owner `modbus_master_task` serialising all RS-485 transactions
    through a queue.
  - Bus scanner (`bus_scan` + `scan_task`): address-range sweep with live
    progress over WebSocket.
  - Wind Test poller (`wind_poll` + task): periodic decode of the DUT's wind
    speed/direction input registers and config write-back to its holding
    registers.
  - Modbus traffic ring-buffer log (`mb_log`) streamed to the web UI.
  - Web server (HTTP + WebSocket) serving the vanilla HTML/CSS/JS UI from
    SPIFFS: Status, Bus Scanner, Wind Test, Register Explorer, Modbus Log,
    and WiFi Settings sections.
  - WiFi manager (AP/STA auto-switch, mDNS), NTP time sync, RGB status LED
    (WS2812B at GPIO35), and typed NVS config persistence (`cfg`).
- Host-side (native) unit tests for every library — `pio test -e native`,
  91 tests across 10 suites.
- Design documentation in `design/` (scratchbook, realisation plans,
  progress snapshot) and hardware reference material in `documentation/`
  (AtomS3, Atomic RS485 Base).
- Repository documentation: README, changelog, contributing guidelines,
  code of conduct, and license information.
- `design/api.md` — specification (draft, not yet implemented) of a
  machine-first JSON-over-HTTP API (`/api/v1/`) so external tools and LLM
  clients (e.g. Claude) can send Modbus requests and receive replies in a
  single self-contained round trip: transaction endpoint, bus scan,
  status/log endpoints, self-description endpoint, and an error model with
  machine-readable status plus recovery hints.

### Notes

- Bench integration testing against a real `windmeters-modbus-interface`
  board (two-peer RS-485 setup) has not been performed yet — see
  `design/whatsNext.md`.
