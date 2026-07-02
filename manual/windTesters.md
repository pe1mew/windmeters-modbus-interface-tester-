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

## Wind Speed tab

Default slave address: **30** (or **35** if the unit is jumpered to its
alternate address).

**Register map** (shown as a reference table at the top of the tab):

| Register | Addr | Field | Scale |
|---|---|---|---|
| Input (FC04) | `0x0000` | speed_instant | raw ÷10, m/s |
| Input (FC04) | `0x0001` | speed_avg | raw ÷10, m/s |
| Input (FC04) | `0x0002` | raw_pulses | unscaled |
| Holding (FC03/06) | `0x0000` | device_addr | unscaled, 1–247 |
| Holding (FC03/06) | `0x0001` | measurement_window | unscaled, ms |
| Holding (FC03/06) | `0x0002` | averaging_window | unscaled, s |

**Live polling:**

1. Set **Slave address** (default 30) and **Poll interval (ms)** (default
   1000).
2. Click **▶ Start**. The Speed and Raw cards below update on every poll:
   instantaneous and averaged speed (m/s), raw pulse count, and the age
   (ms) of the last successful reading — a growing age means the device
   has stopped answering, not that the display froze.
3. Click **■ Stop** to suspend polling (no further bus traffic from this
   tab until started again).

**Config (holding registers):** each field has its own **Write** button —
writes are always single-field (FC06), never a bulk write of everything at
once, so a typo in one field can't clobber the others.

| Field | What it changes |
|---|---|
| Device address | The unit's own Modbus slave address (1–247) — after writing this, the unit answers at the *new* address from then on |
| Measurement window (ms) | How long the anemometer's pulse-counting window is |
| Averaging window (s) | The window used for the "average" reading |

Click **Refresh config from device** to read the current values back (FC03)
and populate the three fields above with what's actually stored on the
unit right now.

## Wind Direction tab

Default slave address: **31** (or **36** if the unit is jumpered to its
alternate address).

**Register map:**

| Register | Addr | Field | Scale |
|---|---|---|---|
| Input (FC04) | `0x0000` | dir_instant | raw ÷10, ° |
| Input (FC04) | `0x0001` | dir_avg | raw ÷10, ° |
| Holding (FC03/06) | `0x0000` | device_addr | unscaled, 1–247 |
| Holding (FC03/06) | `0x0001` | dir_offset | raw ÷10, ° |
| Holding (FC03/06) | `0x0002` | averaging_window | unscaled, s |

**Live polling:** same Slave address / Poll interval / Start / Stop
controls as Wind Speed, above. The Direction card shows instantaneous and
averaged heading in degrees; the Raw card shows only Age (there's no pulse
count for a potentiometer-based sensor).

**Config (holding registers):**

| Field | What it changes |
|---|---|
| Device address | The unit's own Modbus slave address (1–247) |
| Direction offset (°) | Calibration offset applied to the raw potentiometer reading — use this to correct for how the vane is physically mounted |
| Averaging window (s) | The window used for the "average" reading |

Same **Write** (single-field, FC06) and **Refresh config from device**
(FC03 read-back) behaviour as Wind Speed.

## If a register doesn't behave as documented

The DUT's firmware is still evolving — treat the tables above as the
current snapshot, not a permanent contract. If a write or read doesn't
match what's documented here, use [Register Explorer](registerExplorer.md)
to probe the raw register directly; it doesn't assume any fixed map and
will keep working even if the DUT's registers move.
