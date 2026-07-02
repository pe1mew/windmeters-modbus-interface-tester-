# Machine API

Everything the web UI can do is also reachable as plain JSON over HTTP,
under `/api/v1/`. This is a **separate, parallel API** from the routes the
browser UI uses internally — designed so a shell script, a CI job, or an
LLM (e.g. Claude, driving a bench session) can send Modbus requests and get
a complete answer back in one self-contained round trip, with no session
state and no need to hold a WebSocket open.

This page is a practical quick-start. For the full field-by-field
specification — every parameter, every response shape, the complete error
model — see [`design/api.md`](../design/api.md).

## Base URL

- AP mode (first boot / no WiFi configured): `http://192.168.4.1/api/v1`
- STA mode (WiFi configured): `http://windmeter-tester.local/api/v1` or the
  device's DHCP IP — see [System Settings](systemSettings.md)

There is no authentication — this matches the tool's threat model (a bench
instrument on an isolated network, same trust level as the web UI itself).
Don't expose it to an untrusted network.

## The core endpoint: `POST /api/v1/modbus`

One Modbus transaction — build the frame, send it, wait for the reply,
return the outcome. Every other endpoint below is either a convenience
wrapper around this or a read of tester-internal state.

```sh
curl -s -X POST http://192.168.4.1/api/v1/modbus \
  -H 'Content-Type: application/json' \
  -d '{"slave": 30, "function": 4, "register": 0, "count": 3}'
```

```json
{
  "ok": true,
  "status": "ok",
  "slave": 30,
  "function": 4,
  "register": 0,
  "count": 3,
  "registers": [42, 39, 27],
  "raw_tx": "1E 04 00 00 00 03 B2 64",
  "raw_rx": "1E 04 06 00 2A 00 27 00 1B 05 65",
  "round_trip_ms": 38,
  "attempts": 1,
  "ts": "2026-07-02T14:30:45Z"
}
```

Key fields worth knowing up front:

- `ok` — `true` only for a valid, non-exception reply. A **timeout is
  still HTTP 200** — check `ok`/`status`, not the HTTP status code, to
  know whether the transaction actually succeeded.
- `raw_tx` / `raw_rx` — the exact bytes on the wire, so you can
  independently verify CRC and framing without trusting the tester's own
  decode.
- `register` — always echoed back as the raw 0-based wire address, even
  if you sent a Modicon-style number (below).
- On failure, `detail` (what happened) and `hint` (a suggestion for what
  to try next) are included — see [Status values](#status-values).

### Writing a register

```sh
curl -s -X POST http://192.168.4.1/api/v1/modbus \
  -d '{"slave": 31, "function": 6, "register": 1, "values": [1800]}'
```

Function `6` = write single register (FC06). `values` takes exactly one
element for FC06, or 1–123 for `16` (write multiple, FC16).

### Modicon-style register numbers

Prefer raw 0-based addresses, but if you're working from a datasheet that
uses Modicon numbering (30001/40001-style), pass it as a string with
`register_format`:

```sh
curl -s -X POST http://192.168.4.1/api/v1/modbus \
  -d '{"slave": 31, "function": 6, "register": "40003", "register_format": "modicon", "values": [10]}'
```

`40003` converts to raw `0x0002` — the response's `register` field always
reports the converted raw address, not the Modicon number you sent.

## Supporting endpoints

| Endpoint | Purpose |
|---|---|
| `GET /api/v1/spec` | Self-description — endpoint list, status vocabulary, and the current DUT register-map snapshot. Point an LLM at just this URL to let it bootstrap a session without any other documentation. |
| `GET /api/v1/status` | Tester + bus health: WiFi state, NTP sync, Modbus config, error/timeout counters, whether a scan or wind poll is currently running |
| `POST /api/v1/scan` | Bus scan — `{"start":1,"end":247,"wait":true}` blocks until the full sweep completes and returns every address found; `"wait":false` returns immediately and you poll `GET /api/v1/scan` for progress |
| `GET /api/v1/log?n=20` | The last `n` (max 50) TX/RX frames from the traffic log |
| `GET /api/v1/wind?type=speed` (or `type=direction`) | The wind poller's last cached reading for that sensor type — cheaper than re-reading the bus yourself, since it reuses whatever the tester is already polling |

### A typical session

```sh
# 1. Discover what's available
curl -s http://192.168.4.1/api/v1/spec

# 2. Find what's on the bus
curl -s --max-time 180 -X POST http://192.168.4.1/api/v1/scan -d '{"wait": true}'

# 3. Talk to something the scan found
curl -s -X POST http://192.168.4.1/api/v1/modbus \
  -d '{"slave": 30, "function": 4, "register": 0, "count": 3}'

# 4. If something looks wrong, check the raw traffic
curl -s "http://192.168.4.1/api/v1/log?n=10"
```

## Status values

Every response includes a `status` field — branch on this, not on the
`detail`/`hint` prose, which may change wording between firmware versions.

| `status` | Meaning |
|---|---|
| `ok` | Valid reply received |
| `timeout` | No reply within the timeout, after all retries |
| `crc_error` | A reply came back but failed its CRC check |
| `exception` | The slave answered with a Modbus exception — see `exception_code`/`exception_name` |
| `framing_error` | Malformed reply (wrong length, wrong echo, wrong function) |
| `param_error` | Request rejected before anything was sent — bad count, bad function code, etc. |
| `bad_request` | The request JSON itself was malformed |
| `busy` | You asked not to wait (`"wait":false`) and the bus/scanner was occupied |

## Wind data has two variants — say which one you want

Because wind speed and wind direction are separate physical units at
separate addresses (see [Wind Speed / Wind Direction](windTesters.md)),
`GET /api/v1/wind` needs a `type` query parameter (`speed` or
`direction`) — it returns only that sensor's fields, and `{"ok":true,
"has_data":false}` if that type isn't the one currently polling (only one
polls at a time). The full register maps for both types — the raw
addresses `POST /api/v1/modbus` would use to read/write them directly —
are in [Wind Speed / Wind Direction](windTesters.md) and in
`GET /api/v1/spec`'s `dut_register_snapshot`.

## Full reference

Every field, every endpoint's complete request/response shape, retry/
timeout semantics, and the reasoning behind each design choice:
[`design/api.md`](../design/api.md).
