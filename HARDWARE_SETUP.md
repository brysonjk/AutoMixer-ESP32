# Hardware Setup Guide

## Components

- **Display**: ESP32-8048S070 (7" 800x480 TFT with ESP32-S3)
- **ADC**: ADS1115 16-bit ADC for sensor readings
- **Valve drivers**: optoisolated AOD4184 MOSFET breakout boards (low-side switch)
- **Sensors**: Two galvanic O2 cells (helium is derived from the second) and two
  pressure transducers (bank and fill)

## Wiring Diagrams

![Wiring overview](docs/wiring/overview.svg)

Wire colours: red +12 V supply and valve rail, orange +5 V, amber +3.3 V, grey ground,
blue I2C, green valve PWM, purple sensor and input signals. Every ground
symbol ends at the one star point below.

| Valve driver (one per valve) | Star grounding |
|---|---|
| ![Valve driver](docs/wiring/valve-driver.svg) | ![Star grounding](docs/wiring/grounding.svg) |

![Pressure divider](docs/wiring/pressure-divider.svg)

The same diagrams with the pin table and the pre-gas checklist are in
[docs/wiring.html](docs/wiring.html) (download and open in a browser).

## Wiring Connections

### ADS1115 Connections
```
ADS1115          ESP32-S3
---------        --------
VDD      <-->    3.3V
GND      <-->    GND
SDA      <-->    GPIO 19
SCL      <-->    GPIO 20
```

On the board, GPIO 19/20 are on the 4-pin **P3** header (pins 3 and 4; pins 1-2 are
GPIO 17/18). P3 carries no power, so take 3.3 V and GND from the **P4** header.

P4 and P5 are wired in parallel: both carry `IO18`, `IO17`, `3.3V` and `GND`. They're used
to keep the sensor side and the valve side on separate plugs:

| Header | Use | Cut back and insulate |
|--------|-----|-----------------------|
| **P4** | `3.3V`, `GND`: ADS1115 power only | `IO17`, `IO18` |
| **P5** | `IO17`: O2 valve PWM; `GND`: both valve boards' input ground | `IO18`, `3.3V` |

`IO18` is the touch controller's interrupt line, and anything on it can break touch. P3's
own `IO17`/`IO18` pins are the same nets, so leave those unconnected too.

The GT911 touch controller shares this bus at 0x5D; the ADS1115 sits at 0x48.

### ADS1115 Channel Assignments
```
A0 (Channel 0)  <-->  O2 cell after helium injection (helium is derived from it)
A1 (Channel 1)  <-->  O2 cell, final mix
A2 (Channel 2)  <-->  Bank pressure transducer (via divider)
A3 (Channel 3)  <-->  Fill pressure transducer (via divider)
```

### Pressure Transducers

The ADS1115 runs from 3.3 V, and an input must never go above its supply + 0.3 V.
A typical 5 V transducer puts out 0.5-4.5 V, so each one needs a divider:

```
Transducer OUT --10k--+-- ADS1115 A2 (or A3)
                      |
                     20k
                      |
                     GND
```

That scales 4.5 V to 3.0 V. The scaling lives in `include/sensors.h`
(`PRESSURE_V_ZERO`, `PRESSURE_V_FULL`, `PRESSURE_PSI_FULL`, `PRESSURE_DIVIDER`). Both
transducers fitted are 0-5000 PSI gauge with a 0.5-4.5 V output ratiometric to a
**5 V (±5%)** supply, taken from the buck, never the board's 3.3 V, so one set of
constants serves both:

| Transducer | Accuracy | Connection | Notes |
|---|---|---|---|
| TE **M7139-05KPG-5-00000** (M7100) | ±0.25% | 3-pin Packard (Metri-Pack) | take the supply / ground / output pins from TE's M7100 datasheet |
| TE **M3031-000005-05KPG** (MSP300) | ±1% span; zero and span each ±2% as shipped | 2 ft cable: **red** +5 V, **black** common, **white** output, **green** unused (insulate) | calibrate it (below); not oxygen cleaned |

The MSP300 can read ~200 PSI out before calibration; zero and span bring it to about its
±1% linearity. If one reading matters more, put the M7139 there. The MSP300 as ordered
is not oxygen cleaned (check the M7139's datasheet too): fine for mixes up to 40% O2 by
common practice, but for richer gas use the O2-cleaned MSP300, **M3031-010005-05KPG**. A transducer with a different range or output
means changing these constants.
A signal outside 0.25-4.75 V at the sensor reads as a fault (`----` on screen) rather
than a pressure, so a broken wire can't show up as 0 PSI.

### Valve Control Connections

Each valve is driven by an **optoisolated AOD4184 breakout board** (e.g. ProtoSupplies'
"D4184 MOSFET Control Module"): a 3-way power terminal `+ / LOAD / -` and a 2-pin input
header `PWM / GND`. The MOSFET switches the valve on the low side. `PWM` drives an opto LED
through a 1k on the board, and the header `GND` is not connected to the power side. The
opto pulls the gate to about half the supply (~6 V from 12 V) through 4.7k.

| Terminal | Connect to |
|----------|------------|
| `+` | fused +12 V, the valve's + lead, and the flyback diode's band |
| `LOAD` | the valve's − lead and the flyback diode's other end (this is the drain) |
| `-` | its own wire to the star point (the source; it carries the valve current) |
| valve | both leads to the board, as a twisted pair: + to `+`, − to `LOAD` |
| `PWM` | the ESP32 GPIO, **directly**: the board has its own 1k |
| header `GND` | the ESP32 board's ground: **P5's GND pin** |

| Valve  | GPIO | Header | At boot |
|--------|------|--------|---------|
| Oxygen | 17   | P5 (pin labelled `IO17`) | not pulled up |
| Helium | 11   | P2 (pin labelled `IO11`) | pulled HIGH by the board: needs the 1k pull-down |

The valves are Kelly Pneumatics Miniature Proportional Valves, **0-10 VDC coil**
version: two flying leads, 1.8 W max (about 180 mA at 10 V, a ~55 ohm coil), flow
proportional to coil voltage. Standard versions are rated to **100 psig** inlet, so the
supply regulators must be set below that. The firmware drives them with 1 kHz PWM, so
the duty sets the average coil voltage. Kelly is out of business; see
[Replacement valves](#replacement-valves).

**Running 10 V coils from 12 V.** The coils are driven straight from the 12 V supply, so
the firmware caps the duty (`VALVE_MAX_DUTY` in `include/valve_control.h`): a valve
command of 100% is that duty, never full on. Held fully on at 12 V a coil would take
~2.6 W against its 1.8 W rating.

| `VALVE_MAX_DUTY` | Coil power | When it's safe |
|---|---|---|
| 0.69 | <= 1.8 W | Always, even if the coil's inductance doesn't smooth the PWM |
| **0.80 (default)** | ~1.7 W (9.6 V average) | Only if the inductance smooths the PWM into a near-steady current |
| 0.83 | 1.8 W (10 V average) | The same, with no headroom |

The default was raised from 0.69 to 0.80 after a real blend needed about 94% of the 0.69
range to hold 32% O2 at 75 psi: the cap was limiting flow. **Confirm it on your valves:**
hold one valve at 100% on Setup > Valve Test for 30 minutes (touch +1/-1 now and then, or
the page closes it after 2 minutes) and compare its coil temperature with the same valve
on a steady 10 V. If it runs hotter, go back to 0.69. With Diagnostics on (Setup > Blender
Setup), the status panel shows each valve's opening and average coil voltage while
blending; a valve near 100% is out of flow, not out of tuning.

**The cap assumes a regulated 12 V supply.** An unregulated adapter that idles at 16 V,
or a later swap to 24 V, overdrives the coils. Change the supply only together with
`VALVE_MAX_DUTY`.

- **Flyback diode is required.** Fit a 1N5819 at the board, across `+` and `LOAD`, band to
  `+`; the board doesn't have one. Without it the turn-off spike of the PWM-driven coil
  will eventually kill the MOSFET. Fitted backwards it shorts the supply through the
  MOSFET the moment the valve turns on.
- **Take the valve's + lead from the board's `+` terminal**, not from the 12 V rail
  elsewhere. With the diode at the board, the coil's current keeps flowing through the
  valve wires during the PWM off-time, so they carry a near-steady current and the 1 kHz
  switching current stays in the short loop on the board, away from the O2 cell leads.
  If the + lead came from the rail elsewhere, the diode current would return through that
  rail wiring and undo the benefit. `+` then holds three conductors and `LOAD` two: use
  ferrules or a short pigtail so each screw clamps reliably.
- **Header `GND` must go to the ESP32**: it's the opto LED's return. Take it from **P5**,
  the same plug as the O2 valve's `IO17`, not from the P4 wire that feeds the ADS1115, so
  the pulsed LED current doesn't share the sensor ground wire. One P5 `GND` wire can serve
  both boards.
- **No series resistor** between the GPIO and `PWM`: the board has its own 1k, and another
  1k in front of the 1k pull-down would halve the drive.
- **The optocoupler is slow** (tens of microseconds), which at 1 kHz PWM slightly stretches
  very short pulses. The control loop corrects for it; if a valve won't respond at small
  openings, the PWM frequency can be lowered.

#### Safety: the helium pin is pulled HIGH until the firmware runs

GPIO 11 is shared with the board's SD-card slot (MOSI) and has a **10k pull-up to
3.3 V** on the board, so it sits HIGH through every reset, the bootloader, and the whole
of every flash (15-80 s). The firmware drives both valve pins LOW as the first thing it
does (`ValveController::holdClosed()`), but it cannot act before it starts.

The **1k pull-down from `PWM` to header `GND`** covers that window. Without it, the 10k
pull-up drives about 0.2 mA through the opto LED, and a high-gain opto can pass enough of
that to turn the MOSFET on. Against the 10k the pull-down holds the pin near 0.3 V, below
the LED's turn-on voltage. A weaker pull-down is not enough. Fit it on **both** boards: it
costs nothing on GPIO 17 and protects against a floating input. While a valve is driven
the GPIO supplies ~3.3 mA to the pull-down plus ~2 mA to the LED, well within its rating.

**Verify it:** with the 12 V supply off and the board powered from USB, flash it and
confirm each board's `PWM` pin stays below 0.5 V throughout.

Don't put an SD card in the slot while the valves are wired to these pins.

The alternative is to drive the valves from a PCA9685 PWM controller on the existing I2C
bus: it powers up with its outputs off and frees these GPIOs. Its PWM tops out around
1.5 kHz, so check that suits the valves.

### Power

**Board input.** Without USB-C, feed **regulated 5 V** into the 4-pin **P1** connector on
the back, beside the USB-C port: silkscreen `GND / RXD / TXD / +5V`. Use only `+5V` and
`GND`; leave `RXD`/`TXD` unconnected. Budget **2 A**: peak draw is roughly 0.7 A (the
backlight is ~1.2 W and WiFi transmit bursts add ~0.35 A), and a marginal supply shows up
as backlight flicker. USB and P1 are isolated from each other on the board (a diode on
USB, a MOSFET on P1), so USB can stay plugged in for flashing while P1 powers it.

**Layout.** One regulated 12 V DC supply, feeding the valves directly and the board
through a 5 V buck converter:

```
12 V DC supply, regulated, 2 A, fused at 2 A
 |-- buck -> 5 V, 2.5-3 A -> board P1 (+5V/GND), pressure transducer excitation
 '-- both valve coils (+), duty-limited by the firmware
```

| Load | Current at 12 V |
|---|---|
| Board, display, WiFi via the 5 V buck (~3.5 W peak) | ~0.35 A |
| Pressure transducers | ~0.01 A |
| Two valves at full command | ~0.36 A average, ~0.44 A peak |
| **Total** | **~0.8 A**; a 2 A supply leaves room for switch-on inrush |

**Buck wiring.** The 5 V module has four terminals. `IN+` from the fused +12 V, `IN-` on
its own wire to the star ground point, `OUT+` to P1 `+5V` (and the transducers' +5 V),
`OUT-` to P1 `GND`. On these small non-isolated modules `IN-` and `OUT-` are the same
copper, so the board's ground returns to the star through the module: **don't also wire
P1 `GND` to the star**, which would make a second ground path. If the module has a
3.3 V / 5 V output selector (a solder jumper or pad), set it to 5 V and measure before
connecting the board.

The supply must be **regulated** DC (a switching supply, LED driver or DIN-rail unit).
A bare transformer gives AC, and an unregulated adapter can idle well above 12 V; see
the valve section above. Measure it under load once installed.

The 5 V buck must accept 12 V in. A fixed-output module suits it (no trimpot to drift),
e.g. Pololu D36V28F5 (5 V 3.2 A) or D24V22F5 (5 V 2.5 A), or a Mean Well DDR-15G-5 on
DIN rail. Check the datasheet before buying. Pressure transducers with a 0.5-4.5 V
output are usually ratiometric, so the 5 V rail's accuracy is the pressure reading's
accuracy: measure it and adjust the scaling in `include/sensors.h` if needed.

**Sensor grounds.** Every sensor ground lands on the **ADS1115's GND terminal**: both O2
cells' − leads, both pressure transducers' ground (M7139 ground pin, MSP300 black), and the
compressor switch's return. The ADS1115 measures each input against that pin, so a sensor
grounded anywhere else would pick up the voltage across whatever wire lies between. The
transducers take +5 V straight from the buck but return to the ADS1115, not the buck. A
small terminal strip at the ADS1115, fed by one wire from P4 `GND`, keeps this tidy. Run
O2 cell leads longer than ~30 cm in shielded twisted pair, shield grounded at the ADS1115
end only.

**Grounding.** All grounds must be common, but join them at **one point**, and run the
valve return wires straight back to the supply rather than through the board or ADS1115
ground wiring. The O2 cells put out ~10 mV, and PWM valve current flowing in a shared
ground wire shows up directly as O2 reading error.

### Compressor running input

Use a **current-sensing switch**: a clamp-on current transformer with a built-in relay
contact that closes when the compressor's current passes a set threshold. Wire the
contact between **GPIO 12** (header P2, pin labelled `IO12`) and **GND**. P2 has no GND
pin, so take the contact's return to the ADS1115's GND terminal; it carries microamps
and doesn't disturb the sensor ground. The board
already pulls GPIO 12 up (it's the SD slot's clock line), so the input reads LOW while
the compressor runs. The contact has to hold a new state for 200 ms before it counts.

**The valves stay closed unless the compressor is running.** With no air being drawn
through there is nothing to blend into. That also fails safe: with the switch unplugged
the pin reads HIGH, which is "stopped". The status box reads `blending` while the
compressor runs and the valves are allowed to open.

Clamp it around **one conductor** of the compressor's supply, not the whole cable, or
the currents cancel and it reads nothing. A bare CT (e.g. SCT-013) outputs an AC signal
that needs conditioning circuitry, all four ADS1115 channels are already used, and the
ESP32's own ADC on these pins is unreliable with WiFi on; the switch avoids all three.
Work on the mains side belongs with someone qualified.

### Panel jacks

Every field connection except the 12 V supply comes in through a panel jack: six **3.5 mm
4-pin jacks** for the O2 cells, transducers and valves, and one **2.5 mm jack** for the
compressor sense. All take **TRS plugs**. On the 3.5 mm jacks pin 2 is the **tip**, pin 3
the **ring**, pin 4 the **sleeve**; pin 1 is not used.

One rule throughout: **tip = supply, ring = signal, sleeve = ground.**

| Jack | Tip (pin 2) | Ring (pin 3) | Sleeve (pin 4) |
|------|-------------|--------------|----------------|
| O2 cell, after He (3.5 mm) | - | ADS1115 **A0**, direct | quiet ground (ADS1115 GND) |
| O2 cell, final mix (3.5 mm) | - | ADS1115 **A1**, direct | quiet ground |
| Bank transducer (3.5 mm) | +5 V from the buck | divider 10k end (divider out to **A2**) | quiet ground |
| Fill transducer (3.5 mm) | +5 V from the buck | divider 10k end (divider out to **A3**) | quiet ground |
| He valve (3.5 mm) | He board **+** (12 V) | He board **LOAD** | **not connected** |
| O2 valve (3.5 mm) | O2 board **+** (12 V) | O2 board **LOAD** | **not connected** |
| Compressor sense (**2.5 mm**) | - | **IO12** (P2) | quiet ground |

On the cable side: O2 cell + to ring, - to sleeve, tip unused. Transducer +5 V to tip,
output to ring, ground to sleeve. Valve + to tip, - to ring. Current switch contacts to
ring and sleeve.

Why it's laid out this way:

- **Supplies on the tip make hot-plugging safe.** The jack's tip contact sits deepest and
  only ever touches the plug's tip, which leaves it before sliding past the ring and sleeve
  contacts. With +5 V on the ring instead, pulling a transducer plug shorted the 5 V rail
  and reset the display.
- **A plug in the wrong 3.5 mm jack does almost no harm:**

  | Plug | O2 cell jack | Transducer jack | Valve jack |
  |------|--------------|-----------------|------------|
  | O2 cell | - | cell across the divider: harmless | no circuit |
  | Transducer | unpowered (tip unused) | - | 12 V on its supply pin and no ground; its only return is its output pin, when that valve is driven. Avoid it. |
  | Valve | + on the unused tip: nothing | coil in series with the 30k divider: about 0.16 mA, **the valve stays shut** | - |

- **The valve jack's sleeve stays unconnected.** A valve only has two wires. Grounding the
  sleeve would give a cell or transducer plugged in there a return path, and 12 V across it.
- **The compressor sense has its own connector type.** Each plug type joins a different
  pair of contacts (valve coil tip-ring, O2 cell ring-sleeve, transducer supply
  tip-sleeve), so on a 3.5 mm jack some wrong plug could always pull IO12 low and fake
  "compressor running", defeating the interlock that keeps the valves shut with no air
  flowing. Nothing else fits the 2.5 mm jack.
- **Compressor bypass plug.** A 2.5 mm plug with ring and sleeve joined makes the controller
  read "compressor running". Keep it for bench tests, label it **TEST ONLY**, and never
  leave it in for a fill.
- **Each quiet-ground wire is its own wire** to the ADS1115's GND terminal (the quiet bus),
  never to the supply's star point. The valve's tip comes from that board's `+` terminal,
  not from the 12 V rail elsewhere, and the flyback diode stays at the board across `+` and
  `LOAD`, which also protects the MOSFET when a valve is unplugged while running.
- Label the jacks anyway: the one mix-up that can still hurt something is a transducer in a
  valve jack, and every wrong plug simply doesn't work.

### Replacement valves

When sourcing replacements for the Kelly valves, what the firmware needs is a
**proportional solenoid valve with a coil drive** (two wires, flow set by coil voltage or
current), so the AOD4184/PWM wiring carries over unchanged. Valves with a separate
0-10 V or 4-20 mA *control input* instead need a PWM-to-analog converter in place of the
AOD4184s.

For the oxygen valve specifically, look for:
- **Oxygen-cleaned** and rated for oxygen service
- **Viton/FKM** seals (not Buna-N)
- Rated above your O2 regulator's set pressure
- A coil voltage you can supply, and the recommended PWM frequency, which goes in
  `PWM_FREQ` in `include/valve_control.h`

## Sensor Calibration

### Oxygen Cells
Both channels carry galvanic O2 cells. Helium is injected **first** and oxygen
**second**. Helium is not measured directly: the A0 cell sits between the two
injections, and helium shows up as missing oxygen:

```
He% at A0 = (20.9 - O2 at A0) / 20.9 * 100        e.g. 10.5% O2 -> 49.8% He
```

The oxygen added afterwards dilutes that helium, so the displayed helium is corrected
using the final O2 reading at A1 (the helium's share of the non-oxygen gas is unchanged):

```
He% final = He% at A0 * (100 - O2 at A1) / (100 - O2 at A0)
            e.g. 49.8% He, 10.5% O2 at A0, 21% O2 final -> 44.0% He
```

The helium valve is steered on the same formula with the O2 *target* in place of the
O2 reading, i.e. on where the helium will land once the oxygen is on target. That keeps
the helium loop from reacting to the oxygen loop's swings. If either cell faults, both
valves close.

A galvanic cell is linear through zero, so each cell is calibrated by its output in
ambient air. Use the **Calibrate** button under the Fill reading: with both cells in
plain air and no gas flowing, it averages them over 10 s and saves the result, which
survives a restart. Because helium comes from a small O2 deficit, calibration error is
multiplied about 5x in the helium reading, so both cells are always calibrated together.

A calibration is rejected, and nothing saved, if the two halves of the 10 s window
differ by more than 1% (a cell moved from a mix into air is still settling), if a cell's
readings are noisy (standard deviation over 1% of its reading, or a spread over 4%:
wiring, grounding, a draft, or a failing cell), or if a cell reads outside 5-25 mV (not
plausibly air, or not connected).

**The valves stay closed until the cells have been calibrated once**, and throughout a
calibration. The status box reads `not calibrated` until then.

A cell reading under 1 mV is treated as disconnected: its value shows `--.-` and its
valve is held closed. Without that, an unplugged A0 cell would read as 100% helium.

### Pressure Transducers
Set the `PRESSURE_*` constants in `include/sensors.h` to the transducer's spec and to the
divider actually fitted; see [Pressure Transducers](#pressure-transducers) above. Those
are the starting point; then calibrate each transducer on the unit under
**Setup > Pressure Calibration**:

1. **Zero:** vent the transducer to air and tap **Set zero**. It must read within 0.15 V
   of the datasheet's 0 PSI output, or it's refused (probably not vented).
2. **Span:** pressurise it, ideally near a normal fill, type what a reference gauge
   reads (in the units shown) and tap **Set span**. The reference must be at least
   ~500 PSI and the resulting scale within 20% of the datasheet's, which catches a
   reading entered in the wrong units.

Each step averages 16 readings and is refused if they disagree by more than 0.02 V
(pressure still settling). The result is saved per transducer and survives a restart;
**Reset** returns that transducer to the datasheet scaling.

Calibrating corrects the transducer's own offset and span error, the 5 V excitation's
error (the output is proportional to it), and the divider's resistor tolerance, leaving
roughly the transducer's repeatability. Still fit 0.1% resistors in the dividers and a
100 nF capacitor from each pressure input to GND at the ADS1115.

## PWM Configuration

Current settings in `include/valve_control.h`:
- Frequency: 1 kHz. The coil's inductance smooths this into a near-steady current, and
  the remaining ripple dithers the valve, reducing its hysteresis. Tune it against real
  flow if a valve buzzes or responds unevenly.
- Resolution: 10-bit (0.1% duty steps)
- Maximum duty: `VALVE_MAX_DUTY` = 0.80 (9.6 V average), so the 10 V coils stay within rating on 12 V

## Valve Start Points and Control

**Start points.** A proportional valve passes nothing until its coil is strong enough to
lift the plunger off the seat, often a third of the way up its range or more. Without
knowing that, the control loop sits in the dead band while its integral slowly climbs,
then flow starts all at once: slow to leave 21%, then fast. Each valve's **start point**
is the opening where its gas first flows. Any control command above zero starts there,
and 100% is still fully open. 0% (the default) turns it off.

Find them on **Setup > Valve Test**, one valve at a time:

1. Calibrate the O2 cells, connect the gas at its normal regulator pressure (60-80 psig,
   below the valves' 100 psig rating) and start the compressor. The page refuses to open a
   valve otherwise, like blending.
2. Pick the valve, then raise the opening with **+ 1** a step at a time, waiting a few
   seconds at each step. The page shows the opening, the average coil voltage it gives,
   and the live O2 and He readings.
3. When the reading first starts to move, tap **Save as start point**. Saving at 0%
   clears it. The start points survive a restart (NVS namespace `valves`).

The valve closes when you leave the page, after 2 minutes untouched, or on E-STOP. A
bench supply finds the same point: the DC voltage on the coil where gas first flows,
divided by 12 V and by 0.80 (the duty cap), is the start point. For example,
4.2 V / 12 / 0.80 = 44%. Start points are stored as absolute duty, so changing the cap
doesn't move the voltage a saved start point stands for (one saved under the old 0.69 cap
is converted on the first boot).

**Target ramp.** Each control loop chases a target that climbs from the current reading
at 1 point per second (`TARGET_RAMP_PER_S` in `src/main.cpp`) rather than jumping
straight to the set target, so the oxygen cell's lag can't let the mix run far past it.
A lower target is taken at once. In the simulator, air to 32% with a 35% start point
takes about 10 s and overshoots by under 1 point.

**Learned holding opening (feedforward).** Holding a rich mix needs the valve well open
(32% O2 took about 87% on the first rig), and an integral building that from zero is slow,
because near the top of a valve's range each extra percent adds little flow. So once a
blend has held within 0.3 points of its target for 20 s with the valve steady, the loop
saves the opening it needed per unit of gas demand (NVS `valves`, keys `o2_ff` / `he_ff`).
Later blends start from 85% of that (`FEEDFORWARD_SHARE`), scaled to their own target,
and the integral closes the rest; while the target is still ramping the integral holds
still, so it doesn't wind up on the cell's lag. The first blend after a change of pressure
or valve is slow again and re-learns; the serial log notes each "learned" value. In the
simulator, with a valve whose flow flattens near the top, a second blend to 32% reached
31.5% in 27 s instead of 69 s, peaking at 32.1%.

**Safeguards for a learned opening that no longer fits** (a smaller compressor, a lower
regulator pressure, a different valve):

- **Overshoot guard.** If a reading passes its target by 0.3 points while a learned opening
  is in use, the loop drops it for the rest of that blend (the integral finishes from
  below) and halves the saved value. That blend teaches nothing; the next one starts lower
  and learns properly. In the simulator, halving the airflow under a controller that had
  learned on the larger rig gave peaks of 31.4% and 30.6% on a 30% target, then a clean
  third blend that re-learned.
- **O2 ceiling.** If the O2 reading goes more than 2 points over its target for any reason,
  the O2 valve is held shut until the mix is back at target. The status panel shows
  "O2 over target" meanwhile. If the reading hasn't dropped by half a point within 15 s
  of the valve shutting, it's an emergency stop: see [Limits and Safety](#limits-and-safety).
- **Forget** on Setup > Valve Test clears the learned openings. Use it after changing the
  compressor, regulator pressure or valves, and the next blend learns from scratch with no
  overshoot. The same page lists what has been learned ("O2 82% held 32%").
- **Valve learning: Auto / Locked** on Setup > Blender Setup. Locked keeps the learned
  values and stops updating them; the overshoot guard still drops the learned opening for
  that blend, without saving the cut.

**Targets at air.** An O2 target of 21% or less with no helium, or a helium target of 0,
keeps that valve shut: there is nothing to add, and with a start point set a loop chasing
a tenth of a percent would otherwise crack the valve open.

## Limits and Safety

**Setup > Limits & Safety** sets the highest target for each gas: **O2 40%** and
**helium 50%** by default. The O2 limit can be set from 22 to 100%, helium from 5 to 100%.
Tap + or − for one step; hold for five. The wheels stop at the limits, and presets and the
web page can't set more. A preset over a limit is pulled down to it, and a notice says
so ("12/65 is over the limits ... so the targets are 12/50"). Lowering a limit below the
current target lowers the target too. The limits are stored on the unit (NVS `limits`)
and can only be changed there.

Two emergency stops guard the oxygen while gas is flowing. They work whatever the
limits are set to:

| Condition | What happens |
|---|---|
| O2 more than 2 points over target | O2 valve held shut until back at target (the O2 ceiling) |
| ...and not down half a point within 15 s | **EMERGENCY STOP**: the oxygen is getting in some other way, such as a stuck valve or a leak past it |
| O2 more than 2 points over the O2 limit for 2 s | **EMERGENCY STOP** |

Both are latched: the valves stay shut until the operator taps the red button to
resume. The status panel and the serial log give the reason, for example "O2 reached
44.5%, over the 40% limit. Check the O2 valve." Lowering a target mid-blend doesn't trip
them: the O2 falls once its valve shuts. In the simulator, a target cut from 32% to 26%
was held and released with no stop, and a valve leaking 12% oxygen tripped 15 s after
the ceiling. Lowering the O2 limit below the current mix during a blend does trip the
second check.

Both checks only work while the firmware is running and the O2 cell reads correctly. For
a stop that doesn't depend on either, see
[Optional external emergency stop](#optional-external-emergency-stop).

### Optional external emergency stop

A hardware switch that cuts the valves' power, whatever the firmware is doing. **It's
optional.** Without it, wire the valve boards as above; there's nothing to set in the
firmware.

Fit a **normally-closed, latching mushroom-head E-stop switch** (twist or key to
release) in series with the +12 V feed to the two valve boards' `+` terminals, after the
fuse:

```
12 V supply (+) --- fuse --+-- buck IN+ (the board and sensors stay powered)
                           |
                           '-- [E-STOP, NC] --+-- O2 valve board  +
                                              '-- He valve board  +
```

- Pressing it cuts the coil current, so both proportional valves close on their springs,
  in any state. The display, sensors and web page stay up, so the readings can still be
  watched.
- Put it **after the buck's tap**. Breaking the whole 12 V supply would also reset the
  controller.
- It carries only the valve current (under 0.5 A), so any panel E-stop with an NC contact
  will do. A latching switch stays off until someone resets it deliberately.
- The firmware doesn't see the switch. A blend that was running carries on trying, and
  with no oxygen arriving its loop keeps opening further, so releasing the switch
  mid-blend would give a burst of oxygen (the O2 checks above would then catch it). After
  using the switch, tap EMERGENCY STOP on the screen too, and resume only once the
  switch is reset.
- The flyback diodes stay at the valves, across each coil, so opening the switch under
  load is safe.

## Maintenance Counters

**Setup > Maintenance** is a compressor hour meter. It counts only while the
compressor-running input is on, so it measures pumping time whatever the unit is
connected to.

| Counter | Reset | Reminder |
|---|---|---|
| Compressor hours | Never (lifetime total) | - |
| Filter cartridge | Reset button, after changing it | Limit in hours; 0 = off |
| Oil change | Reset button, after changing it | Limit in hours; 0 = off |

Set each limit with - / + (tap for 1 h, hold to step 10 h). When a counter reaches its
limit the status panel shows "Filter change due" or "Oil change due" while the compressor
is stopped. It is a reminder only and doesn't block blending. Counts are saved every
5 minutes while the compressor runs and whenever it stops, in NVS namespace `maint`,
so a power cut loses at most 5 minutes.

Flow (CFM or L/min) needs the volume being filled, so it lives with the Blending and
Filling modes below, where the operator says what that volume is.

## Blending and Filling Modes

With pressure sensors fitted, a **Mode** button sits left of Setup in the banner; it offers
**Blending** or **Filling**, the current one ticked. The mode is remembered across restarts (NVS namespace `fill`, with the bank and cylinder
choices).

| | Blending | Filling |
|---|---|---|
| Main screen | O2, helium, targets and blending, as before | Source and Fill cards |
| Blending | Runs as set | **Off: both valves held shut** |
| Pressure readouts | Bank, with its rate | Source (the bank transducer) and Fill, each with its rate |
| Flow | Into the **bank**: tap **Bank size** (where the Fill reading was) | Into the **cylinder**: tap **Cylinder** |
| Also shown | | Time until the cylinder reaches its working pressure; how far the source is above the fill, then "Equalized: open the next bottle" |

**Pressure rate.** Under each pressure: `PSI  +420/min`, or bar per minute in BAR mode.
A straight-line fit over the last 30 s of once-a-second readings, rounded to 5 PSI
(0.5 bar); "steady" when it isn't moving, negative while the bank is drawn down, "..."
for the first 10 s. It needs no volume, so it's always right.

**Cascade filling.** Filling mode calls the bank transducer **Source**: on a cascade it
reads whichever bottle is open. The card shows how far that bottle is above the cylinder,
and once they're within 50 PSI, "Equalized: open the next bottle". Bank size and bank flow
belong to Blending mode, where the compressor is filling the whole bank.

**Flow and time to full.** Free air in = bottle volume x pressure rise. Volumes come from
how bottles are sold, rated volume at a working pressure: a 444 cu ft @ 4500 psi bottle
holds 444/4500 cu ft of air per PSI, so four of them rising 600 PSI a minute take about
237 CFM. Shown as "about": a cylinder warms as it fills, which reads a little high
mid-fill. Time to full is what's left to the cylinder's working pressure at the current
rate.

The presets follow the pressure units on Blender Setup: PSI shows sizes in cu ft, BAR in
litres of water. Each unit has its own selection and its own custom sizes, so switching
units never leaves an odd size selected.

The cylinder page shows the presets in tabs, one group at a time. Ratings are from the
makers' spec sheets (Catalina, Luxfer, Faber, Worthington); LP steels are rated at their
+10% fill pressure.

| PSI tab | Cylinders |
|---|---|
| Aluminum | AL30, AL40, AL50, AL63, AL72 @ 3000 psi; AL80 (77.4 cu ft) @ 3000; AL100 @ 3300 |
| Steel LP | Faber LP50, LP85, LP95, LP108, LP120 @ 2640 psi |
| Steel HP | HP65, HP80, HP100, HP119, HP120, HP130 @ 3442 psi |

| BAR tab | Cylinders |
|---|---|
| Single | 3 L @ 200 bar; 5, 7, 10, 12, 15, 18 L @ 232; 12 L @ 300 |
| Twin | twin 7 (14 L), twin 12 (24 L) @ 232 |

**My cylinders**: three custom slots per unit for anything not listed, such as a twinset
or a stage. Tap a slot to use it; its name (up to 11 characters, set with the on-screen
keyboard), size and rating appear below it. Slots are kept after restart.

| Bank bottles (1-12) | PSI | BAR |
|---|---|---|
| Presets | 444 cu ft @ 4500 psi, 300 cu ft @ 4500 psi | 50 L @ 300 bar, 50 L @ 200 bar, 80 L @ 300 bar |

The bank page has one custom size, plus the number of bottles.

Defaults: an AL80 or 12 L cylinder, and a bank of 4 x 444 cu ft or 4 x 50 L. The
selection is saved by name, so adding presets doesn't change it.

## Safety Notes

1. **Valve Fail-Safe**: On startup, all valves are closed
2. **Pressure Monitoring**: Add appropriate pressure relief valves
3. **Emergency Stop**: the screen and web page stop the valves in firmware, with automatic
   O2 stops (see [Limits and Safety](#limits-and-safety)). An optional hardware switch is
   described in [Optional external emergency stop](#optional-external-emergency-stop)
4. **Gas Handling**: Follow proper safety procedures for oxygen and helium
5. **Electrical Safety**: Use proper fusing and isolation for valve power

## Pin Summary

| Function | GPIO | Notes |
|----------|------|-------|
| ADS1115 SDA | 19 | I2C Data |
| ADS1115 SCL | 20 | I2C Clock |
| ADS1115 power | - | 3.3 V and GND from header P4 (nothing else on P4) |
| O2 Valve | 17 | PWM output, header P5 (with the valve boards' input GND) |
| He Valve | 11 | PWM output, header P2 (pulled up on the board) |
| Compressor running | 12 | Input, header P2; LOW = running |
| Board power | - | 5 V into header P1 (`+5V`/`GND`) |
| Display BL | 2 | Backlight |
| Touch SDA | 19 | Shared with ADS1115 |
| Touch SCL | 20 | Shared with ADS1115 |
| Touch RST | 38 | GT911 reset |
| Touch INT | 18 | Must be driven low while RST is released, or the GT911 never scans |

## Testing

1. **Display Test**: Should show GUI on startup
2. **Sensor Test**: Monitor Serial output for sensor readings
3. **Valve Test**: Use button interface to test valve operation
4. **Safety Test**: Verify valves close on power loss
