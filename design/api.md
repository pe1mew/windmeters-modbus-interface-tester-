# Machine API — Modbus-over-JSON for automated test clients

| Field        | Value                                                        |
|--------------|--------------------------------------------------------------|
| Document     | API Specification                                            |
| Project      | Windmeters Modbus Interface Tester                           |
| Status       | Draft / proposed — not yet implemented                       |
| Date         | 2026-07-02                                                   |
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

Read example (`FC04`, 5 registers from slave 31 — the DUT's full input map):

```json
{
  "ok": true,
  "status": "ok",
  "slave": 31,
  "function": 4,
  "register": 0,
  "count": 5,
  "registers": [1834, 42, 1810, 39, 27],
  "raw_tx": "1F 04 00 00 00 05 33 F0",
  "raw_rx": "1F 04 0A 07 2A 00 2A 07 12 00 27 00 1B 61 4C",
  "round_trip_ms": 38,
  "attempts": 1,
  "ts": "2026-07-02T14:30:45Z"
}
```

Write example (`FC06`, set direction calibration offset):

```json
{
  "ok": true,
  "status": "ok",
  "slave": 31,
  "function": 6,
  "register": 1,
  "written": [150],
  "raw_tx": "1F 06 00 01 00 96 5B 94",
  "raw_rx": "1F 06 00 01 00 96 5B 94",
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
  "raw_tx": "23 04 00 00 00 05 32 8D",
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
  "raw_tx": "1F 03 00 04 00 01 C4 4C",
  "raw_rx": "1F 83 02 61 30",
  "attempts": 1,
  "detail": "Slave 31 answered function 3 with exception 2 (illegal data address).",
  "hint": "The slave is alive but says holding register 4 does not exist. The DUT's register 40005 (low-speed cutoff) is planned but not yet implemented in its firmware — see the DUT's own scratchBook.md."
}
```

`detail` states what happened; `hint` suggests what to do next. Hints are
best-effort prose and may change wording between firmware versions —
**clients must branch on `status`, never on `detail`/`hint` text.**

---

## 5. Supporting endpoints

### 5.1 `GET /api/v1/spec` — self-description

Returns a JSON summary of this API: version, endpoint list with methods,
request/response field tables, the `status` vocabulary (§7), and the
current DUT register-map snapshot the Wind Test panel uses. Intent: an LLM
given only `http://<ip>/api/v1/spec` can bootstrap a full test session
without human-provided documentation.

```json
{
  "api": "windmeters-modbus-interface-tester",
  "version": "1",
  "endpoints": [ { "method": "POST", "path": "/api/v1/modbus", "summary": "...", "request": { }, "response": { } } ],
  "statuses": { "ok": "…", "timeout": "…", "crc_error": "…" },
  "dut_register_snapshot": {
    "input_registers":   [ { "addr": 0, "name": "wind_dir_instant", "unit": "0.1 deg" } ],
    "holding_registers": [ { "addr": 0, "name": "device_addr", "range": [1, 247] } ]
  }
}
```

### 5.2 `GET /api/v1/status` — tester and bus health

```json
{
  "ok": true,
  "uptime_s": 4021,
  "wifi": { "mode": "STA", "ssid": "bench", "ip": "192.168.1.42", "rssi": -61 },
  "ntp_synced": true,
  "modbus": { "baud": 9600, "timeout_ms": 200, "retries": 1,
              "crc_errors": 0, "timeouts": 2, "last_exception": null },
  "busy": { "scan_running": false, "wind_poll_active": false }
}
```

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

Returns the last `n` (default 20, max = ring capacity, currently 32)
TX/RX frames from the traffic log, newest last:

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

---

## 6. Worked examples (curl)

Read the DUT's five wind input registers (raw addressing):

```sh
curl -s -X POST http://192.168.4.1/api/v1/modbus \
  -H 'Content-Type: application/json' \
  -d '{"slave": 31, "function": 4, "register": 0, "count": 5}'
```

Write the averaging window using a Modicon register number:

```sh
curl -s -X POST http://192.168.4.1/api/v1/modbus \
  -H 'Content-Type: application/json' \
  -d '{"slave": 31, "function": 6, "register": "40004", "register_format": "modicon", "values": [10]}'
```

Change the DUT's slave address (FC06 to holding register 0), then verify at
the new address:

```sh
curl -s -X POST http://192.168.4.1/api/v1/modbus \
  -d '{"slave": 31, "function": 6, "register": 0, "values": [36]}'
curl -s -X POST http://192.168.4.1/api/v1/modbus \
  -d '{"slave": 36, "function": 3, "register": 0, "count": 1}'
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

- [ ] **Batching** — accept an array of transactions in one POST and return
      an array of results? Deferred: one-transaction-per-call keeps error
      handling trivial for LLM clients; revisit if round-trip latency ever
      dominates a workflow.
- [ ] **Implementation placement** — new `api_server` library vs. extending
      `web_server_task` + `web_core` (where `/explorer/query` lives).
      Leaning to the latter: same AsyncWebServer instance, JSON builders in
      `web_core` so they stay host-testable.
- [ ] **`/explorer/query` convergence** — once `/api/v1/modbus` exists, the
      web UI's Register Explorer could call it instead and `/explorer/query`
      be retired. Decide during implementation.
- [ ] **FC05/FC01 (coils)** — the DUT has no coils; add only if a future
      DUT needs them.
- [ ] **Streaming poll endpoint** — a long-poll or SSE variant of the Wind
      Test poller for machine clients. Deferred; `POST /api/v1/modbus` in a
      client-side loop covers v1 needs.
