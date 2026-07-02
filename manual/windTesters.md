# Wind Speed / Wind Direction

The device under test ships as **two physically separate units**, not one
combined sensor: a cup anemometer (wind speed) and a wind-vane
potentiometer (wind direction), each its own board with its own Modbus
slave address. The tester has one tab per unit — **Wind Speed** and
**Wind Direction** — rather than a single combined panel, because that's
what actually exists on the bench.

**Only one of the two can poll at a time.** Starting Wind Speed while Wind
Direction is running silently stops Wind Direction's polling (and vice
versa) — the underlying firmware task only ever targets one address. If
you switch tabs and see Start enabled / Stop disabled even though you
thought polling was still running, that's the tester correctly reporting
that the *other* tab took over; the GUI corrects both tabs' buttons
automatically within a second of either one's next update.

**Both units implement the same register map.** Per the DUT's own
Technical Design Specification, wind speed and wind direction firmware
share one identical 12-input/4-holding register layout at the same
addresses — a register the active unit's sensor doesn't have just reads 0,
rather than each unit having its own differently-shaped map. Each tab below
shows only the registers that are actually meaningful for that unit; the
rest (the other unit's fields, plus device-level diagnostics like uptime
and error counters) exist at the same addresses and are reachable via
[Register Explorer](registerExplorer.md), just not surfaced on these tabs.
**There is no device-address register on either unit** — the Modbus address
is set by a hardware solder jumper only and can't be read or changed over
Modbus.

## Wind Speed tab

Default slave address: **30** (or **35** if the unit is jumpered to its
alternate address).

**Register map** (shown as a reference table at the top of the tab):

| Register | Addr | Field | Scale |
|---|---|---|---|
| Input (FC04) | `0x0001` | speed_instant | raw ÷10, m/s |
| Input (FC04) | `0x0003` | speed_avg | raw ÷10, m/s |
| Input (FC04) | `0x0004` | raw_pulses (last window) | unscaled |
| Input (FC04) | `0x000A` | seconds_since_pulse | unscaled, s |
| Input (FC04) | `0x000B` | gust (max in current averaging window) | raw ÷10, m/s |
| Holding (FC03/06) | `0x0001` | measurement_window | unscaled, ms (100–60000) |
| Holding (FC03/06) | `0x0002` | averaging_window | unscaled, s (1–600) |
| Holding (FC03/06) | `0x0003` | low_speed_cutoff | raw ÷10, m/s (0–50) |

**Live polling:**

1. Set **Slave address** (default 30) and **Poll interval (ms)** (default
   1000).
2. Click **▶ Start**. The Speed and Raw cards below update on every poll:
   instantaneous and averaged speed (m/s), raw pulse count, gust (the
   fastest single window within the current averaging period), seconds
   since the last anemometer pulse, and the age (ms) of the last successful
   reading — a growing age means the device has stopped answering, not that
   the display froze.
3. Click **■ Stop** to suspend polling (no further bus traffic from this
   tab until started again).

**Config (holding registers):** each field has its own **Write** button —
writes are always single-field (FC06), never a bulk write of everything at
once, so a typo in one field can't clobber the others.

| Field | What it changes |
|---|---|
| Measurement window (ms) | How long the anemometer's pulse-counting window is |
| Averaging window (s) | The window used for the "average" reading |
| Low-speed cutoff (m/s) | Below this speed, the device reports 0 m/s instead of a noisy near-zero value |

Click **Refresh config from device** to read the current values back (FC03)
and populate the fields above with what's actually stored on the unit right
now.

## Wind Direction tab

Default slave address: **31** (or **36** if the unit is jumpered to its
alternate address).

**Register map:**

| Register | Addr | Field | Scale |
|---|---|---|---|
| Input (FC04) | `0x0000` | dir_instant | raw ÷10, ° (65535 = sensor fault) |
| Input (FC04) | `0x0002` | dir_avg | raw ÷10, ° (65535 = sensor fault) |
| Input (FC04) | `0x0004` | raw_adc (last conversion) | unscaled, 0–1023 |
| Holding (FC03/06) | `0x0000` | dir_offset | raw ÷10, ° (0–3599) |
| Holding (FC03/06) | `0x0002` | averaging_window | unscaled, s (1–600) |

**Live polling:** same Slave address / Poll interval / Start / Stop
controls as Wind Speed, above. The Direction card shows instantaneous and
averaged heading in degrees; the Raw card shows the last raw ADC conversion
and the age of the last successful reading.

**Sensor fault:** if the potentiometer wiper is floating (disconnected, or
a bad connection) for more than 2 seconds, the unit reports a fault instead
of a reading — the Direction card shows a red **Sensor fault** badge, and
the instant/average values hold their last real reading rather than
updating. This clears automatically once a valid reading resumes.

**Config (holding registers):**

| Field | What it changes |
|---|---|
| Direction offset (°) | Calibration offset applied to the raw potentiometer reading — use this to correct for how the vane is physically mounted |
| Averaging window (s) | The window used for the "average" reading |

Same **Write** (single-field, FC06) and **Refresh config from device**
(FC03 read-back) behaviour as Wind Speed.

## If a register doesn't behave as documented

The tables above mirror the DUT's Technical Design Specification, which is
more settled than earlier in the project but can still change. If a write
or read doesn't match what's documented here, use
[Register Explorer](registerExplorer.md) to probe the raw register
directly; it doesn't assume any fixed map and will keep working even if the
DUT's registers move.
