# Machine API — Modbus-over-JSON for automated test clients

| Field        | Value                                                        |
|--------------|--------------------------------------------------------------|
| Document     | API Specification                                            |
| Project      | Windmeters Modbus Interface Tester                           |
| Status       | **Implemented** — all 7 endpoints below are live and hardware-verified (`design/progress.md` §7, `design/whatsNext.md` §2) |
| Date         | 2026-07-02 (implemented same day; wind endpoints updated 2026-07-02 for the speed/direction split) |
| Audience     | Any HTTP-capable tool; designed specifically so an LLM (Claude) can drive the tester during bench sessions |
| Related docs | `design/scratchbook.md` (§6.3 master core API, §7 web UI endpoints), `firmware/lib/web_server/web_server_task.cpp` (existing UI endpoints this generalises) |

---

## 1. Purpose and scope

The tester's existing REST endpoints (`/explorer/query`, `/scan/start`, …)
exist to serve the human web UI: they assume a browser holding a WebSocket
open for results, they always answer HTTP 200, and failures come back as a
bare `{"ok":false}` with no explanation.

This document specifies a parallel **machine-first API** under `/api/v1/`
that lets any external tool — a shell script, a CI job, or an LLM such as
Claude driving a bench session — send Modbus requests and receive the reply
in a **single, self-contained HTTP round trip**:

- The client POSTs one JSON object carrying everything needed to send a
  Modbus message: slave **address**, function code, **register**, count,
  and **data** (for writes).
- The response is one JSON object carrying everything about the outcome:
  decoded register values, raw TX/RX frames in hex, timing, and — on
  failure — a machine-readable status plus a human/LLM-readable
  explanation of what went wrong and what to try.

Out of scope: the WebSocket push channel, the human web UI, firmware/OTA
concerns, and authentication (§9).

### 1.1 Design principles (why the API looks like this)

These follow directly from the "LLM as client" requirement:

1. **One round trip, no session state.** An LLM issuing tool calls cannot
   cheaply hold a WebSocket open or poll. Every request is stateless and
   returns the complete outcome — never "accepted, check back later"
   (exception: bus scans, which genuinely take tens of seconds; §5.3).
2. **Self-describing.** `GET /api/v1/spec` returns a machine-readable
   summary of every endpoint, so a client that has only been told the
   base URL can discover the rest (§5.1).
3. **Errors explain themselves.** Every failure carries `status` (stable
   enum for programs) **and** `detail` + `hint` (prose for humans/LLMs),
   e.g. `"hint": "No response from slave 31. Check bus wiring/termination,
   or run POST /api/v1/scan to see which addresses respond."`
4. **Raw frames always included.** `raw_tx` / `raw_rx` hex strings let the
   client independently verify CRC, framing, and byte order — essential
   when the DUT itself is the thing being debugged.
5. **Same conventions as the rest of the repo.** Raw 0-based wire
   addresses are canonical; Modicon numbers (30001/40001-style) are
   accepted as input only and converted at the boundary
   (scratchbook.md §5). Status strings reuse the existing
   `mb_status_t`-derived vocabulary (§7).

---

## 2. Transport and general behaviour

- **Base URL:** `http://<tester-ip>/api/v1` (AP mode: `http://192.168.4.1/api/v1`;
  STA mode: the DHCP address or mDNS name).
- **Content type:** requests and responses are `application/json; charset=utf-8`.
- **Methods:** `POST` for anything that touches the RS-485 bus or mutates
  state; `GET` for pure reads of tester-internal state.
- **HTTP status codes** distinguish *API-level* from *bus-level* problems:

| Code | Meaning |
|---|---|
| `200` | Request understood and executed. The **bus** outcome (success, timeout, Modbus exception, …) is in the body's `ok`/`status` fields — a slave timeout is still HTTP 200. |
| `400` | Malformed request: invalid JSON, missing/out-of-range field, unsupported function code. Body is an error object (§7); nothing was sent on the bus. |
| `409` | Bus busy and `wait` was set to `false` (§4.1), or a scan is already running. Nothing was sent on the bus. |
| `503` | Tester not ready (Modbus master task not started). |

- **Serialisation:** the tester has one RS-485 UART owned by
  `modbus_master_task`; API transactions enter the same queue used by the
  scanner, wind poller, and web UI, and execute strictly one at a time.
  Concurrent API calls are safe — they queue. Clients should still prefer
  sequential calls: queueing behind a running bus scan can delay a
  transaction by seconds.
- **Limits:** `count` ≤ 125 registers per read (Modbus PDU limit),
  ≤ 123 registers per FC16 write. One transaction per request — no
  batching in v1 (§10).

---

## 3. Data conventions

| Concept | Convention |
|---|---|
| Slave address | Integer 1–247. Broadcast (0) is not supported in v1. |
| Register address | **Raw 0-based wire address is canonical.** Accepted as a JSON number (`2`) or a string (`"2"`, `"0x0002"`). |
| Modicon input | A 5-digit register number may be given instead by setting `"register_format": "modicon"` — e.g. `"register": "40003"` → wire address `0x0002`. Converted at the boundary via `wire = (modicon % 10000) - 1`; responses always report the raw address. |
| Register values | Unsigned 16-bit integers (0–65535), exactly as on the wire. **The API does not scale or sign-interpret values** — interpretation (e.g. the DUT's ×10 wind encoding) is the client's job. This keeps the tester useful when the DUT's register map changes. |
| Hex frame strings | Uppercase hex bytes separated by single spaces, CRC included, e.g. `"1F 04 00 00 00 05 33 F0"`. |
| Timestamps | `ts` fields are ISO-8601 UTC when NTP is synced, otherwise milliseconds since boot as a string tagged `"clock": "uptime"`. |

---

## 4. Core endpoint — `POST /api/v1/modbus`

One Modbus transaction: build the frame, send it, wait for the reply (with
the configured timeout/retries), return the outcome.

### 4.1 Request

```json
{
  "slave": 31,
  "function": 4,
  "register": 0,
  "count": 5,
  "values": [],
  "register_format": "raw",
  "timeout_ms": 200,
  "retries": 1,
  "wait": true
}
```

| Field | Type | Required | Default | Description |
|---|---|---|---|---|
| `slave` | int 1–247 | yes | — | Modbus slave address to talk to. |
| `function` | int | yes | — | Function code: `3` (read holding), `4` (read input), `6` (write single), `16` (write multiple). Aliases accepted: `"read_holding"`, `"read_input"`, `"write_single"`, `"write_multiple"`. |
| `register` | int or string | yes | — | Start register. Raw 0-based wire address by default; see `register_format`. |
| `count` | int 1–125 | for FC03/04 | 1 | Number of registers to read. Ignored for FC06; derived from `values` length for FC16. |
| `values` | array of int 0–65535 | for FC06/16 | — | Data to write. FC06 uses exactly one element; FC16 accepts 1–123. |
| `register_format` | `"raw"` \| `"modicon"` | no | `"raw"` | How to interpret `register`. |
| `timeout_ms` | int 10–5000 | no | NVS `mb_timeout_ms` (200) | Per-request response timeout override. Not persisted. |
| `retries` | int 0–5 | no | NVS `mb_retries` (1) | Per-request retry override. Not persisted. |
| `wait` | bool | no | `true` | `true`: if the bus is busy, queue and wait. `false`: return HTTP 409 immediately instead of queueing. |

### 4.2 Response — success (HTTP 200)

Read example (`FC04`, all 12 input registers from slave 30 — Wind Speed and
Wind Direction share one identical 12-register input map, `design/scratchbook.md`
§5; a register the active build's sensor doesn't have reads 0, which is why
registers 0/2 — the direction fields — are 0 in this speed-build response):

```json
{
  "ok": true,
  "status": "ok",
  "slave": 30,
  "function": 4,
  "register": 0,
  "count": 12,
  "registers": [0, 42, 0, 39, 27, 0, 322, 120, 0, 500, 8, 65],
  "raw_tx": "1E 04 00 00 00 0C F2 60",
  "raw_rx": "1E 04 18 00 00 00 2A 00 00 00 27 00 1B 00 00 01 42 00 78 00 00 01 F4 00 08 00 41 51 49",
  "round_trip_ms": 38,
  "attempts": 1,
  "ts": "2026-07-02T14:30:45Z"
}
```

Write example (`FC06`, set direction calibration offset — holding register
0, `design/scratchbook.md` §5; there is no device-address register to write
instead, as of the DUT's TDS v0.6):

```json
{
  "ok": true,
  "status": "ok",
  "slave": 31,
  "function": 6,
  "register": 0,
  "written": [150],
  "raw_tx": "1F 06 00 00 00 96 0A 1A",
  "raw_rx": "1F 06 00 00 00 96 0A 1A",
  "round_trip_ms": 22,
  "attempts": 1,
  "ts": "2026-07-02T14:31:02Z"
}
```

| Field | Presence | Description |
|---|---|---|
| `ok` | always | `true` iff the transaction completed with a valid, non-exception reply. |
| `status` | always | `"ok"` or an error status (§7). |
| `slave`, `function`, `register` | always | Echo of the request, with `register` normalised to the raw wire address. |
| `registers` | reads | Decoded 16-bit values, in register order. |
| `written` | writes | Values the slave acknowledged. |
| `raw_tx`, `raw_rx` | always (when frames exist) | Exact frames on the wire, hex (§3). `raw_rx` is present even for CRC/framing failures, so the client can inspect the bad frame; absent on pure timeouts. |
| `round_trip_ms` | always | Time from first TX byte to last RX byte of the successful attempt. |
| `attempts` | always | 1 + number of retries actually consumed. |
| `ts` | always | Completion timestamp (§3). |

### 4.3 Response — bus-level failure (still HTTP 200)

```json
{
  "ok": false,
  "status": "timeout",
  "slave": 35,
  "function": 4,
  "register": 0,
  "raw_tx": "23 04 00 00 00 03 B6 89",
  "attempts": 2,
  "detail": "No response from slave 35 within 200 ms (2 attempts).",
  "hint": "Nothing answered at address 35. Run POST /api/v1/scan to list responding addresses; the wind-speed DUT variant defaults to 30 (or 35 jumpered), the direction variant to 31 (or 36 jumpered). Also check A/B wiring polarity and that exactly the bus ends are terminated."
}
```

Modbus exception example:

```json
{
  "ok": false,
  "status": "exception",
  "exception_code": 2,
  "exception_name": "illegal_data_address",
  "slave": 31,
  "function": 3,
  "register": 4,
  "raw_tx": "1F 03 00 04 00 01 C6 75",
  "raw_rx": "1F 83 02 A0 F7",
  "attempts": 1,
  "detail": "Slave 31 answered function 3 with exception 2 (illegal data address).",
  "hint": "The slave is alive but says holding register 4 does not exist. The DUT's register 40005 (low-speed cutoff) is planned but not yet implemented in its firmware — see the DUT's own scratchBook.md."
}
```

`detail` states what happened; `hint` suggests what to do next. Hints are
best-effort prose and may change wording between firmware versions —
**clients must branch on `status`, never on `detail`/`hint` text.**

### 4.4 Implementation note — reply-wait timeout must scale with the request

The existing `/explorer/query` handler (`web_server_task.cpp`) waits on its
reply queue with a hardcoded `EXPLORER_REPLY_TIMEOUT_MS = 2000` — safe today
only because that endpoint can't override timeout/retries. `/api/v1/modbus`
allows `timeout_ms` up to 5000 and `retries` up to 5 (§4.1) — worst case
~30s of legitimate in-progress work. The handler for this endpoint **must**
compute its own queue-wait budget as `timeout_ms × (retries + 1) + margin`
per request, not reuse a fixed constant, or a caller using the high end of
its own advertised range gets a spurious `status: "no_reply"` (§7) instead
of a real result.

---

## 5. Supporting endpoints

### 5.1 `GET /api/v1/spec` — self-description

Returns a JSON summary of this API: version, endpoint list with methods,
request/response field tables, the `status` vocabulary (§7), and the
current DUT register-map snapshot the Wind Speed/Direction tabs use. Intent:
an LLM given only `http://<ip>/api/v1/spec` can bootstrap a full test
session without human-provided documentation.

`dut_register_snapshot` holds one `"wind"` register map (2026-07-02 — was
briefly keyed by sensor type, `wind_speed`/`wind_direction`, right after the
physical-separation finding, before the DUT's TDS matured enough to specify
that both builds actually share one identical map, `design/scratchbook.md`
§5/§9). Each input register carries `active_on`, listing which build(s) it
carries real data on — a register not listed for the current build reads 0.
There is no device-address holding register (TDS v0.6, FR-MB07/FR-MB26):

```json
{
  "api": "windmeters-modbus-interface-tester",
  "version": "1",
  "endpoints": [ { "method": "POST", "path": "/api/v1/modbus", "summary": "...", "request": { }, "response": { } } ],
  "statuses": { "ok": "…", "timeout": "…", "crc_error": "…" },
  "dut_register_snapshot": {
    "wind": {
      "input_registers": [
        { "addr": 0, "name": "dir_instant", "unit": "0.1 deg", "active_on": ["direction"] },
        { "addr": 1, "name": "speed_instant", "unit": "0.1 m/s", "active_on": ["speed"] }
      ],
      "holding_registers": [
        { "addr": 0, "name": "dir_offset", "unit": "0.1 deg", "range": [0, 3599] },
        { "addr": 1, "name": "measurement_window_ms", "range": [100, 60000] }
      ]
    }
  }
}
```

### 5.2 `GET /api/v1/status` — tester and bus health

```json
{
  "ok": true,
  "fw_version": "0.4.0",
  "uptime_s": 4021,
  "wifi": { "mode": "STA", "ssid": "bench", "ip": "192.168.1.42", "rssi": -61 },
  "ntp_synced": true,
  "modbus": { "baud": 9600, "timeout_ms": 200, "retries": 1,
              "crc_errors": 0, "timeouts": 2, "last_exception": null },
  "busy": { "scan_running": false, "wind_poll_active": false }
}
```

`fw_version` is the tester's own firmware version (SemVer, `platformio.ini`'s
`FIRMWARE_VERSION` build flag) — not to be confused with `GET /api/v1/spec`'s
`"version"` field (§5.1), which is this *API's* version, not the firmware's.

Mirrors the WebSocket `type:"status"` payload; lets a stateless client
check "is a scan already hogging the bus?" before transacting.

### 5.3 `POST /api/v1/scan` — bus scan (the one non-instant call)

A full 1–247 sweep takes tens of seconds, so this endpoint supports both a
blocking and a polling style:

Request:

```json
{ "start": 1, "end": 247, "wait": true }
```

- `"wait": true` (default) — the HTTP response is held open until the sweep
  completes and returns the full result. Client timeout advice:
  `(end - start + 1) × (timeout_ms × (retries + 1) + frame_time)` plus
  margin; a full default sweep is ~100 s. Recommended for LLM clients —
  one call, one complete answer.
- `"wait": false` — returns `202` immediately with `{"ok":true,"state":"scanning"}`;
  poll `GET /api/v1/scan` for progress and results.

**Implementation note:** `wait: true` must be implemented as a yielding
poll loop (`bus_scan_get_status()` checked against a short `delay()` /
`vTaskDelay()` between checks), not a single monolithic blocking wait.
Holding an `AsyncWebServer` callback for up to ~100s is a different risk
class than the ~2s blocks already proven safe in this codebase (a
long solid block can starve other requests and the WebSocket broadcast for
the whole duration). This project already proved the yielding-loop pattern
works — the pre-cleanup `main.cpp` bring-up smoke test used exactly this
shape to wait out a real scan (see git history / `memory/gotcha-log.md`).

Completed-scan result (returned by both styles):

```json
{
  "ok": true,
  "state": "done",
  "range": [1, 247],
  "found": [
    { "slave": 31, "functions_ok": [3, 4], "round_trip_ms": 36 }
  ],
  "duration_ms": 98400
}
```

`GET /api/v1/scan` while running returns
`{"ok":true,"state":"scanning","current": 87,"range":[1,247],"found":[...]}`.
A scan request while one is already running returns HTTP 409.

### 5.4 `GET /api/v1/log?n=20` — recent Modbus traffic

Returns the last `n` (default 20, max = ring capacity, currently 50 —
raised from 32 on 2026-07-02 once bench use showed that was too short to
keep a whole scan or short session in view) TX/RX frames from the traffic
log, newest last:

```json
{
  "ok": true,
  "entries": [
    { "ts": "2026-07-02T14:30:45Z", "dir": "TX", "hex": "1F 04 00 00 00 05 33 F0", "summary": "FC04 read 5 @ 0x0000 -> slave 31" },
    { "ts": "2026-07-02T14:30:45Z", "dir": "RX", "hex": "1F 04 0A ...", "summary": "5 registers" }
  ]
}
```

Useful to a client for post-hoc debugging ("show me what actually went
over the wire while the wind poller was running").

### 5.5 `GET /api/v1/wind?type=speed|direction` — cached wind reading

Added during spec review (§10) — not in the original draft. Without this,
a machine client wanting wind data would have to re-issue `POST
/api/v1/modbus` reads directly against the bus, bypassing
`wind_poll_task`'s cache (doubling real bus traffic against the same
registers whenever a Wind tab is also open) and re-deriving the DUT's ×10
decode client-side even though `wind_poll.cpp` already does it.

**Updated 2026-07-02 for the wind speed/direction split** (`design/scratchbook.md`
§5, §9): wind speed and wind direction are separate physical units (sharing
one identical register map as of the DUT's TDS v0.6, but still two
different devices at two different addresses), so this endpoint takes a
`type` query parameter (`"speed"` default if omitted, or `"direction"`) and
returns only the fields meaningful for that type — it no longer returns
both sensors' data in one combined object.

```json
// GET /api/v1/wind?type=speed
{
  "ok": true,
  "has_data": true,
  "target": 30,
  "sensor_type": "speed",
  "speed_instant_ms": 4.2,
  "speed_avg_ms": 3.9,
  "raw_pulses": 27,
  "gust_ms": 6.5,
  "seconds_since_pulse": 8,
  "age_ms": 420
}
```

```json
// GET /api/v1/wind?type=direction
{
  "ok": true,
  "has_data": true,
  "target": 31,
  "sensor_type": "direction",
  "dir_instant_deg": 183.4,
  "dir_avg_deg": 181.0,
  "dir_fault": false,
  "raw_adc": 512,
  "age_ms": 420
}
```

Same fields as the WebSocket `type:"wind"` payload for that sensor type
(`web_core_build_wind_json`, `web_core_build_api_wind_json` — already
implemented and tested) — this is a route over existing logic, not new
decode logic. `{"ok":true,"has_data":false}` when the requested `type`
isn't the one currently polling (only one of the two can be active at a
time — `busy.wind_poll_active` in §5.2 reflects whichever is) or nothing
has been read yet, matching the WebSocket shape's existing convention.
`target` is the requested type's NVS-stored (or default) address —
`wind_speed_addr`/`wind_dir_addr`, not the old single `wind_test_addr` —
which may differ from the address actually in flight if a caller started
polling with an ad hoc `addr` override via `POST /wind/start` rather than
the stored default.

---

## 6. Worked examples (curl)

Read the Wind Direction unit's instantaneous heading (raw addressing; both
wind units share one identical 12-register input map, `design/scratchbook.md`
§5 — register 0 is meaningful on a direction build, reads 0 on a speed one):

```sh
curl -s -X POST http://192.168.4.1/api/v1/modbus \
  -H 'Content-Type: application/json' \
  -d '{"slave": 31, "function": 4, "register": 0, "count": 1}'
```

Write the averaging window using a Modicon register number (holding
register 2 — `40003` — identical on both wind units, `design/scratchbook.md`
§5):

```sh
curl -s -X POST http://192.168.4.1/api/v1/modbus \
  -H 'Content-Type: application/json' \
  -d '{"slave": 31, "function": 6, "register": "40003", "register_format": "modicon", "values": [10]}'
```

Write the low-speed cutoff, then verify by reading it back (there is no
device-address register to demonstrate instead, as of the DUT's TDS v0.6 —
the Modbus address is hardware-jumper only and can't be changed over the
wire):

```sh
curl -s -X POST http://192.168.4.1/api/v1/modbus \
  -d '{"slave": 30, "function": 6, "register": 3, "values": [40]}'
curl -s -X POST http://192.168.4.1/api/v1/modbus \
  -d '{"slave": 30, "function": 3, "register": 3, "count": 1}'
```

Full bus sweep, blocking:

```sh
curl -s --max-time 180 -X POST http://192.168.4.1/api/v1/scan -d '{"wait": true}'
```

---

## 7. Status vocabulary

`status` extends the firmware's `mb_status_t` and the strings already used
by `/explorer/query`, so the two layers never disagree on names:

| `status` | HTTP | `ok` | Meaning |
|---|---|---|---|
| `ok` | 200 | true | Valid reply received. |
| `timeout` | 200 | false | No reply within `timeout_ms` after all retries (`MB_ERR_TIMEOUT`). |
| `crc_error` | 200 | false | Reply received but CRC check failed (`MB_ERR_CRC`). `raw_rx` included. |
| `exception` | 200 | false | Slave returned a Modbus exception (`MB_ERR_EXCEPTION`); see `exception_code`/`exception_name` (1 = illegal_function, 2 = illegal_data_address, 3 = illegal_data_value, 4 = slave_device_failure, …). |
| `framing_error` | 200 | false | Malformed reply — wrong length, wrong echo, wrong function (`MB_ERR_FRAMING`). `raw_rx` included. |
| `param_error` | 400 | false | Request rejected before transmission (`MB_ERR_PARAM`) — bad count, bad function code, missing `values`, … |
| `bad_request` | 400 | false | JSON malformed or a field failed validation at the HTTP layer. |
| `busy` | 409 | false | `wait:false` and the bus/scanner is occupied. |
| `no_reply` | 200 | false | Internal: master task did not answer within its own watchdog (should not happen in normal operation; report as a tester bug). |

---

## 8. Using the API from an LLM (Claude)

The API is deliberately shaped so no special client is needed — any
mechanism that can issue an HTTP POST with a JSON body works:

- **Direct tool use / scripting:** Claude Code on a bench PC on the same
  network can drive the tester with `curl` (Bash tool) exactly as in §6.
  A typical session: `GET /api/v1/spec` → `POST /api/v1/scan` → iterate
  `POST /api/v1/modbus` against found addresses → `GET /api/v1/log` when
  something looks wrong.
- **MCP wrapper (optional, later):** a thin MCP server exposing three
  tools (`modbus_transact`, `modbus_scan`, `tester_status`) that forward
  to these endpoints would give schema-validated tool calls; the JSON
  bodies above are already shaped to serve as the MCP tool input/output
  schemas unchanged. Not required for v1 — curl suffices.

Client guidance embedded in the design (worth restating to any LLM system
prompt that drives this API):

1. Branch on `status`, read `hint` for recovery ideas, never parse prose.
2. Check `GET /api/v1/status` → `busy` before long operations.
3. Register values are raw uint16 — apply the DUT's ×10 scaling yourself,
   and don't assume the register map: fetch the snapshot from `/api/v1/spec`
   or probe with reads.
4. Writes are live writes to real hardware config (including the slave
   address itself) — after writing holding register 0, the device answers
   at the *new* address.

---

## 9. Security posture

None — by design, and worth stating explicitly. The API has no
authentication, no TLS, and write access to the bus and the DUT's
configuration. This matches the tool's threat model: a bench instrument on
an isolated bench/AP network, same trust level as the existing web UI
(which is equally open). **Do not** expose the tester to an untrusted
network; if that ever becomes a requirement, an API-key header
(`X-Api-Key`, stored in NVS) is the intended minimal extension, reserved
here but not specified.

---

## 10. Open questions / deferred

Elaborated 2026-07-02, cross-referenced against the actual `mb_master` /
`web_server_task` code rather than just the API shape on paper.

- [x] **Batching** — accept an array of transactions in one POST and return
      an array of results? **Decision: stays deferred.** The bus is
      serialised through one queue regardless (`modbus_master_task` is the
      sole UART owner) — batching would save HTTP round-trips, not bus
      time, and a local bench network's HTTP overhead is small next to a
      ~20–40ms Modbus transaction. The real cost would be partial-failure
      semantics (transaction 3 of 5 times out — abort the rest, or
      continue?), which cuts against design principle #1's "no session
      state" goal. Revisit only if a genuine multi-device fleet workflow
      appears — same reasoning as the multi-device Wind dashboard
      already deferred to v2 (`scratchbook.md` §9).
- [x] **Implementation placement** — new `api_server` library vs. extending
      `web_server_task` + `web_core`. **Decision: extend `web_server_task` +
      `web_core`.** `web_core` already exists to hold host-testable
      JSON-building logic separate from the Arduino-only orchestration
      (`web_core_build_scan_json`/`_wind_json`/`_status_json`, 10 native
      tests) — the new `/api/v1/*` response shapes are the same kind of
      thing, not a new concern. A second library would mean either a
      redundant second `AsyncWebServer` instance or cross-library calls
      back into `web_core` anyway. New `web_core` builders (e.g.
      `web_core_build_api_modbus_json()`, `_api_error_json()`,
      `_api_spec_json()`) plus new routes in `web_server_task.cpp` is the
      whole implementation.
- [x] **`/explorer/query` convergence** — once `/api/v1/modbus` exists,
      should the web UI's Register Explorer keep its own route or call the
      new one directly? **Decision: share the core, keep both routes.**
      Both endpoints call the same underlying transact-and-build-JSON
      helper in `web_core`; `/explorer/query` stays as a thin, UI-shaped
      wrapper around it. No behaviour change to the web UI (already
      hardware-verified, `progress.md` §3), no route retirement, logic
      duplication eliminated anyway. The existing `explorer_transact()`
      helper (`web_server_task.cpp`) can't be reused unmodified for this —
      see §4.4's reply-timeout note; it needs to become parameterised on
      timeout/retries rather than a fixed constant.
- [x] **FC05/FC01 (coils)** — the DUT has no coils; add only if a future
      DUT needs them. **Decision: closed, no action.** Consistent with this
      project's "don't design for hypothetical future requirements"
      principle — nothing currently in scope needs coils.
- [x] **Streaming poll endpoint** — a long-poll or SSE variant of the Wind
      Test poller for machine clients. **Decision: still deferred.**
      SSE/long-poll would add real complexity to `AsyncWebServer` for a
      bench tool that doesn't need it. What was missing instead of
      streaming was a way to read the *already-polled* value without
      re-transacting the bus — added as §5.5 `GET /api/v1/wind`.

### 10.1 Implementation notes added during this review

Not gaps in the original design so much as things that only became visible
by cross-checking against the actual code:

- **§4.4** — the reply-wait timeout must be computed from the request's own
  `timeout_ms`/`retries`, not reused from `/explorer/query`'s fixed 2000ms
  constant.
- **§5.3** — `wait: true` must be a yielding poll loop, not a single
  blocking wait, given the ~100s worst case.
- **§5.5** — new `GET /api/v1/wind` endpoint, so machine clients get the
  cheap/cached path to wind data instead of only the bus-re-querying one.

### 10.2 Sequencing

Per project discussion: this API is implemented **before** Phase 3
(`design/whatsNext.md`) — it becomes one of the tools used to actually run
Phase 3's integration tests (INT-01…09) against the sensor emulator and,
later, the real DUT, rather than being built after that bench work is
already done by other means.
