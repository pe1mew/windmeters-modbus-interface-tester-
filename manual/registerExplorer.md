# Register Explorer

A generic, single-shot Modbus request tool: pick an address, a function
code, a register, and (for writes) values, then send it. Unlike
[Wind Speed / Wind Direction](windTesters.md), this doesn't assume any
fixed register map — it's the tool that keeps working when the device
under test's register layout changes, or against any device that isn't
one of the two wind units at all (e.g. while bringing up a new sensor, or
probing something the Bus Scanner just found).

## Fields

| Field | Meaning |
|---|---|
| Address | Slave address to talk to, 1–247 |
| Function | `FC03 read holding`, `FC04 read input`, `FC06 write single`, `FC16 write multiple` |
| Register format | **Raw (0x..)** — a 0-based wire address, e.g. `0x0002` — or **Modicon** — a 5-digit register number in the DUT's own documentation style, e.g. `40003`. Pick one with the radio buttons; the text field next to them is the register value itself. |
| Count / value | **Count**: how many registers to read (FC03/FC04), ignored for FC06/FC16. **Values**: comma-separated numbers to write (FC06 uses exactly one, FC16 up to 123) |
| Send | Submits the request and shows the result below |

## Raw vs. Modicon addressing

Modbus register documentation commonly uses two different numbering
conventions for the same physical register — this tool accepts either as
*input*, but always reports back the raw address that was actually used:

- **Raw**: the address as it appears on the wire, 0-based. This is what
  the tester's own register-map tables (in the
  [Wind Speed / Wind Direction](windTesters.md) tabs, and the
  [API spec](api.md)) always use.
- **Modicon**: the 5-digit convention some datasheets use (30001, 40001,
  …). Converted at input time via `wire = (modicon_number % 10000) − 1` —
  so `40003` becomes raw `0x0002`. Never stored or carried further as the
  Modicon number; the result panel always shows the raw address that was
  actually sent.

If you're not sure which style a datasheet you're reading uses: raw
addresses are small numbers starting at 0 (`0x0000`, `0x0001`, …); Modicon
numbers are always 5 digits starting with 3 (input registers) or 4
(holding registers).

## Reading the result

After clicking **Send**, the result line shows one of:

- **`OK (raw addr 0x....) — values: ...`** — a successful read; the raw
  address actually used, and the decoded register value(s) in order.
- **`OK (raw addr 0x....) — hex: ...`** — for some responses, the raw
  frame hex is shown alongside or instead of decoded values.
- **`Error: <status>`** — the request reached the device but failed (e.g.
  `timeout`, `exception`, `crc_error`) — see the
  [API manual](api.md#status-values) for what each status means.
- **`Request failed.`** — the tester itself couldn't process the request
  (malformed input, or the tester isn't reachable).

Every request — success or failure — also shows up in the
[Modbus Log](gui.md#modbus-log) at the bottom of the page, with the exact
raw hex frame sent and received.

## Example: reading the Wind Speed unit's raw pulse count directly

Instead of using the Wind Speed tab, you can read the same register
manually: Address `30`, Function `FC04 read input`, Register format
**Raw**, Register `0x0002`, Count `1`, then **Send**. This is useful for
confirming the Wind Speed tab is decoding correctly, or for reading a
register the tab doesn't expose at all.

## Example: writing a single holding register

Address `31`, Function `FC06 write single`, Register format **Modicon**,
Register `40001` (device address, holding register 0 in Modicon numbering),
Values `36`, then **Send** — this changes that unit's own slave address to
36. After a successful write, the unit answers at the *new* address; the
old address will stop responding.
