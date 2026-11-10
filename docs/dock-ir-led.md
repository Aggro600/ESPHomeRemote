🇬🇧 English · 🇩🇪 [Deutsch](dock-ir-led.de.md)

# Dock Rev6: external IR LED and extra IR LEDs

The dock (board **OpenRemote Dock Rev6**) sends infrared through four 940 nm LEDs (D1–D4) at the board edge.
Two solder pads labelled **J4 / "IR LED"**, right next to D1, let you add an IR LED on a cable – for example into
a closed cabinet. No firmware change is needed for anything on this page.

## How the IR output works

```
 +5V ──R5 39Ω──┬── D1 ──┐
               │        │
              J4/1    J4/2 ─── IR_K ─── Q1 (MOSFET, FS8205A) ─── GND
                                          │
 +5V ──R6 39Ω──── D2 ───── IR_K           GPIO0 (IR_PWM)
 +5V ──R7 39Ω──── D3 ───── IR_K
 +5V ──R8 39Ω──── D4 ───── IR_K
```

- Each LED has its **own 39 Ω resistor** from +5 V. That gives about **95 mA** per LED while it is lit.
- All cathodes meet at **IR_K**, which the MOSFET Q1 switches to ground.
- The firmware only pulses: 38 kHz carrier at **33 % duty**, and only while a command is being sent. On average
  an LED draws about 30 mA during a command and nothing otherwise.
- The whole dock is protected by a **1 A** resettable fuse (F1).

## J4: the "IR LED" pads

J4 is **not a separate output**. It sits **in parallel to D1** and shares D1's resistor R5:

- **J4 pin 1 = LED anode (+)**, connected to R5 and the anode of D1
- **J4 pin 2 = LED cathode (−)**, connected to IR_K

Check the polarity with a multimeter (continuity mode) before soldering: the **+** pad beeps against the lead
of **R5** that does *not* go to +5 V; the **−** pad beeps against the corresponding lead of D2, D3 and D4.

### Option A – external LED instead of D1 (recommended)

Desolder **D1** and connect an IR LED on a cable to J4. Connect **only the LED** – the resistor (R5) is already on
the board. The external LED gets the full ~95 mA. A few metres of cable are fine; twisted pair is best.

### Option B – external LED in addition to D1

Possible, but D1 and the external LED then share R5's current – unevenly, because no two LEDs are identical.
Both get weaker. Prefer option A or option C.

### What fits on J4

Only an **IR LED** (940 nm, e.g. a 5 mm LED rated for at least 100 mA pulsed). A visible LED as a "sending"
indicator would also light up. Nothing else works there: the pads carry short 38 kHz pulses only while sending,
not a power supply.

## Option C – more IR LEDs (more power)

More power does not come from J4 but from **additional LEDs, each with its own resistor**:

```
 +5V ── 39 Ω (0.5 W) ── LED + … LED − ── J4 pin 2 (IR_K)
```

- **Every extra LED needs its own 39 Ω resistor.** 0.25 W would do on paper, 0.5 W leaves margin.
- **Never** connect an LED between +5 V and IR_K without a resistor – it burns out immediately and can trip the fuse.
- Some ready-made IR LEDs on a cable have a resistor built in. Those go to J4 (instead of D1) or straight to
  +5 V/IR_K without an extra resistor – check the product description.

### How many?

| | Peak while sending | |
|---|---|---|
| ESP32-C3 with Wi-Fi + Bluetooth | up to ~250 mA | |
| 4 built-in LEDs | ~380 mA | |
| **+ 2–3 extra LEDs** | +190–285 mA | **fine with any decent USB power supply** |
| **+ 4 extra LEDs (8 in total)** | ~+380 mA | use a **2 A** power supply |

The 1 A fuse reacts to heat, i.e. to the average current, which is far below the peaks (33 % duty, only while
sending). The 100 µF capacitor next to the IR LEDs smooths the pulses. The MOSFET Q1 handles several amperes and
is not the limit.

## Tip

Several LEDs pointing the same way add little range. They help most when they point in **different directions**
or sit **right in front of a device** on a cable – e.g. inside a cabinet, under a TV, next to a soundbar.

---

Board data: [OpenRemote-Hardware, Dock Rev6](https://github.com/LORDSn1per/OpenRemote-Hardware) (schematic and layout,
GPL-3.0). IR transmitter settings: `remote_transmitter` in [`esphomeremote-dock-board.yaml`](../esphomeremote-dock-board.yaml).
