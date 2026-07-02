# Windmeters Modbus Interface Tester

A standalone Modbus RTU **master** bench tool used to develop and validate
[`pe1mew/windmeters-modbus-interface`](https://github.com/pe1mew/windmeters-modbus-interface) —
a CH32V003-based board that bridges a cup anemometer and wind-vane potentiometer
onto Modbus RTU. The tester runs on an M5Stack **AtomS3** with the **Atomic RS485 Base**
and is operated entirely through a built-in web UI.

It was forked from the non-Modbus scaffolding of
[`pe1mew/greenhouse-Controller-Modbus-sensor-emulator`](https://github.com/pe1mew/greenhouse-Controller-Modbus-sensor-emulator),
with the Modbus role reversed: that template answers requests as a slave, this
tool initiates them as a master.

## What it does

The web UI has Status pinned at the top and the Modbus Log pinned at the
bottom; everything else lives in a tab bar between them.

- **Bus Scanner** — sweep a Modbus slave address range (default 1–247) over
  RS-485 and report which addresses respond, with which function codes, and
  round-trip time. Live progress is streamed to the web UI over WebSocket.
- **Register Explorer** — manual single-shot FC03/FC04/FC06/FC16 requests
  against any address and register, entered as either a raw 0-based address
  or a Modicon-style number (30001/40001). Raw hex and the decoded register
  values are shown side by side. This is the tool that survives DUT
  register-map changes without a firmware rebuild on the tester side.
- **Wind Speed** / **Wind Direction** — two tabs, not one: the DUT ships as
  two physically separate units (cup anemometer vs. wind-vane potentiometer)
  at separate slave addresses, so each gets its own tab with a register-map
  reference table, live decoded values, and a form to write that type's own
  calibration/config holding registers. Only one of the two polls at a time.
- **System Settings** — WiFi (SSID/password), NTP server, manual time, and
  Modbus timeout/retries (pre-populated from the live device config, not
  left empty).
- **Modbus Log** — scrolling TX/RX frame log (hex + decoded, `HH:MM:SS`
  timestamps, last 50 entries) with a clear button.
- **Status** — WiFi mode/SSID/IP/RSSI, NTP sync state, bus health counters,
  uptime, plus an RGB status LED on the device itself (idle / scanning /
  valid-frame / error).
- **Machine API** (`/api/v1/*`, [design/api.md](design/api.md)) — a
  parallel JSON-over-HTTP API for scripts, CI, or an LLM to drive the same
  functionality without a browser: one self-contained request/response per
  Modbus transaction, raw TX/RX frames in every response, and a
  self-describing `GET /api/v1/spec` endpoint.

## Hardware

| Part | Role |
|---|---|
| M5Stack AtomS3 (ESP32-S3) | Controller; native USB-CDC for flashing and logs |
| M5Stack Atomic RS485 Base | SP3485EN half-duplex RS-485 transceiver, hardware auto-direction (no DE/RE GPIO) |

Bench-confirmed wiring: **RX = G5, TX = G6**, RGB LED (WS2812B) at **GPIO35**.
The bus runs at 9600 8N1 by default (configurable and persisted in NVS).
Power the tester from USB-C and tap only A/B/GND onto the bench bus — the
base's 12 V buck input is not used.

The device under test defaults to slave address **30/35** (wind speed variant)
or **31/36** (wind direction variant). The DUT's register map is still
evolving; see [design/scratchbook.md](design/scratchbook.md) §5 for the
current snapshot and use the Register Explorer for anything newer.

## Repository layout

| Path | Contents |
|---|---|
| `firmware/` | PlatformIO project (Arduino framework, ESP32-S3) |
| `firmware/lib/` | Feature libraries, one per concern (`mb_core`, `bus_scan`, `wind_poll`, `web_server`, …), each with a FreeRTOS task wrapper |
| `firmware/data/` | Web UI assets (vanilla HTML/CSS/JS, served from SPIFFS) |
| `firmware/test/` | Host-side (native) unit tests, one suite per library |
| `design/` | Design documents: `scratchbook.md` (design source of truth), realisation plans, progress notes |
| `documentation/` | Hardware reference material: AtomS3 and Atomic RS485 Base schematics and datasheets |

## Building and flashing

Requires [PlatformIO](https://platformio.org/).

```sh
cd firmware
pio run -t upload      # build and flash the firmware (env: windmeterTester)
pio run -t uploadfs    # upload the web UI assets (SPIFFS)
pio device monitor     # 115200 baud, native USB-CDC
```

On first boot (or when no known WiFi is reachable) the tester starts a WiFi
access point; open `http://192.168.4.1`. Once STA credentials are configured
it joins your network and is also reachable via mDNS.

## Running the tests

All protocol and task logic is unit-tested on the host — no hardware needed:

```sh
cd firmware
pio test -e native
```

The native environment compiles the libraries against mock transports and
backends (142 tests across 10 suites at the time of writing).

## Architecture notes

One FreeRTOS task per concern; exactly one task (`modbus_master_task`) ever
touches the RS-485 UART — the scanner and wind poller submit transactions to
it through a queue. Shared state is mutex-guarded and pushed to the browser
over WebSocket at ~1 s cadence using `type`-routed JSON messages. Register
addresses are handled as raw 0-based wire addresses throughout the code;
Modicon-style numbers (30001/40001) are accepted as UI input only and
converted at the boundary.

See [design/scratchbook.md](design/scratchbook.md) for the full design
rationale and [design/progress.md](design/progress.md) for implementation
status.

## Related repositories

- [`pe1mew/windmeters-modbus-interface`](https://github.com/pe1mew/windmeters-modbus-interface) — the device under test
- [`pe1mew/greenhouse-Controller-Modbus-sensor-emulator`](https://github.com/pe1mew/greenhouse-Controller-Modbus-sensor-emulator) — the template this tool was forked from

## License

Software is provided under a Source-Available Non-Commercial License;
documentation and images under CC BY-NC-ND 4.0. See [LICENSE](LICENSE) and
[license.md](license.md). Third-party datasheets in `documentation/` remain
the property of their respective manufacturers.

## Author

Remko Welling (PE1MEW)
