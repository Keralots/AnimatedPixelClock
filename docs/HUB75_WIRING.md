# HUB75 RGB Matrix - Wiring Guide (Phase 1 bring-up)

Step-by-step bench wiring for the AnimatedPixelClock HUB75 port: a Waveshare
ESP32-S3-Zero driving **2x Waveshare P2.5 64x64 HUB75E** panels chained into a
single **128x64** RGB canvas.

> Read the whole sheet once before connecting anything. Power-on **order** and
> a common ground matter (Section 5). Keep brightness low for the first light.

---

## 1. Bill of materials

| Qty | Item | Notes |
|----:|------|-------|
| 2 | Waveshare P2.5 64x64 HUB75E panel | 1/32 scan, driver likely FM6126A (verify, Section 8) |
| 1 | Waveshare ESP32-S3-Zero | ESP32-S3FH4R2, 4MB flash, native USB (no USB-UART chip) |
| 1 | 5V / 10A PSU | powers both panels |
| 3 | SN74AHCT125N (quad buffer) | OPTIONAL - only if ghosting appears (Section 6) |
| - | Dupont / ribbon jumpers, 16-pin HUB75 ribbon (panel-to-panel) | |
| - | Thick 5V + GND wire for power injection | per-panel, not through the ribbon |

---

## 2. HUB75E connector pinout (16-pin, 2x8 IDC)

Looking at the panel's **JIN** (input) header. The E line is present because
this is a 64x64 / 1/32-scan panel (plain HUB75 would have GND there instead).

```
   pin  signal        pin  signal
   ---  ------        ---  ------
    1   R1             2   G1
    3   B1             4   GND
    5   R2             6   G2
    7   B2             8   E
    9   A             10   B
   11   C             12   D
   13   CLK           14   LAT (STB)
   15   OE            16   GND
```

> **Verify against the Waveshare silkscreen on arrival.** Orientation is set by
> the connector notch / the pin-1 arrow on the PCB. If labels differ, trust the
> panel silkscreen over this table.

---

## 3. ESP32-S3-Zero -> panel1 JIN wiring

Connect each S3-Zero GPIO to the matching HUB75E pin on **panel 1's JIN**:

| Signal | S3-Zero GPIO | HUB75E pin |
|--------|:-----------:|:----------:|
| R1     | 1  | 1  |
| G1     | 2  | 2  |
| B1     | 4  | 3  |
| R2     | 5  | 5  |
| G2     | 6  | 6  |
| B2     | 7  | 7  |
| A      | 8  | 9  |
| B      | 9  | 10 |
| C      | 10 | 11 |
| D      | 11 | 12 |
| E      | 12 | 8  |
| CLK    | 13 | 13 |
| LAT    | 14 | 14 |
| OE     | 38 | 15 |
| GND    | GND | 4, 16 |

This pin map avoids the S3-Zero strapping pins (0/3/45/46), USB (19/20),
UART (43/44), the onboard WS2812 (GPIO21), and the PSRAM pins (33-37).

> The HUB75 library README's S3 example uses GPIO33-37/45/21 - **those do NOT
> fit the S3-Zero.** Use the table above. The same map is hard-coded in
> `bringup/hello_matrix.cpp`; if you change wiring, change both.

---

## 4. Chaining the two panels

```
  ESP32-S3-Zero ──16-pin──> [ Panel 1 ] JOUT ──16-pin ribbon──> JIN [ Panel 2 ]
                              x = 0..63                              x = 64..127
```

- ESP ribbon goes to **panel 1 JIN** (input).
- **Panel 1 JOUT -> panel 2 JIN** with the 16-pin ribbon.
- Data flows left-to-right: panel 1 is the left half (x 0..63), panel 2 the
  right half (x 64..127). The bring-up **seam test** (pattern 6: left red /
  right blue) confirms this ordering and a clean boundary at x=64. If the
  colors are swapped sides, the chain order is reversed.

---

## 5. Power (read the ORDER carefully)

- Inject **5V to each panel's own power terminals separately** (screw terminals
  / power pads on the panel). ~4A peak per panel; the 10A PSU covers the pair
  for a mostly-dark clock.
- **Do NOT power panel 2 through panel 1's ribbon** - the ribbon cannot carry
  panel current. Run dedicated 5V/GND wires from the PSU to each panel.
- During bring-up, power the **S3-Zero from USB** (for flashing + serial); the
  **panels from the PSU**.

**Power-on order:**
1. **Bond all grounds FIRST** - PSU GND <-> panel 1 GND <-> panel 2 GND <->
   ESP32 GND - before any 5V is applied. One common ground point.
2. Apply panel **5V** from the PSU.
3. Plug in the ESP **USB** last.

**Ground-loop caution:** the S3 is fed from USB while the panels are fed from
the PSU, so their grounds *must* meet. Never run a panel with its ground
floating relative to the ESP. Do not hot-unplug the PSU ground while USB is
still attached.

---

## 6. Level shifting (only if needed)

Start with **direct 3.3V** from the S3-Zero (no buffers). With two panels and a
short ribbon this is often clean - if so, the SN74AHCT125N chips are not needed.

If you see **ghosting / flicker / dim or unstable pixels**, buffer the 12
priority lines with the 3x SN74AHCT125N (each chip = 4 buffers):

- **Buffer (12):** CLK, R1, G1, B1, R2, G2, B2, LAT, OE, A, B, C
- **Leave direct on 3.3V (2):** D, E (address lines change once per row - large
  timing margin, safe unbuffered).
- Per chip: VCC(pin 14) = 5V, GND(pin 7) = GND, tie all four `~OE` enable inputs
  LOW (to GND) so outputs are always enabled. Feed the 3.3V signal into each
  buffer's A input, take the 5V-level signal from its Y output to the panel.

(Full 14-line buffering later would need a 74AHCT245 / 74HCT541 - NOT a shift
register; 595/164 are the wrong device class.)

---

## 7. Flashing the S3-Zero

The S3-Zero has **no USB-UART chip** - flash over native USB:

1. **Hold BOOT (GPIO0)** while plugging in the USB cable -> enters download mode.
2. Release BOOT.
3. `platformio run -e matrix-s3 --target upload`
4. Serial logs come back over native USB-CDC: `platformio device monitor -e matrix-s3`
   (115200 baud). The sketch waits up to 2s for the USB-CDC host to attach so the
   first diagnostic lines are not lost.

---

## 8. FM6126A driver verification (KNOWN UNKNOWN)

The bring-up sketch defaults `USE_FM6126A 1` **only because Waveshare 64x64
units commonly need it** - it is a guess until you check.

- Look at the **IC markings on the back** of the panel.
- Empirically: if the screen stays **blank with init ON**, set `USE_FM6126A 0`
  and reflash. If blank with init OFF, set it back to `1`.
- **Record the actual chip** once known (and update project memory).

---

## 9. First-power checklist + troubleshooting

**Checklist:** grounds bonded first -> panel 5V on -> USB last -> brightness
stays low for first light -> flash -> watch the pattern cycle.

| Symptom | Likely cause / fix |
|---------|--------------------|
| Dead-black screen | FM6126A init wrong (toggle `USE_FM6126A`); OR no/!bad panel power; OR (with 2 panels) set `PANELS 1` to isolate which panel/segment is dead |
| Wrong colors (e.g. red shows blue) | R/G/B line swap - recheck R1/G1/B1, R2/G2/B2 |
| Only top OR bottom half lit/dim | R2/G2/B2 (lower-half) wiring issue |
| Image halved / doubled / mirrored vertically | Wrong scan rate - panel not 1/32, or E line not wired / wrong pin. Re-confirm scan + E pin (GPIO12 -> HUB75 pin 8) |
| Image shifted or wrapped horizontally | Address line (A-E) wiring error |
| Flicker / ghosting | Apply the AHCT125 buffer (Section 6); or toggle `mxconfig.clkphase` |
| Missing / smeared last column | Toggle `mxconfig.clkphase` |
| Right half wrong / seam colors swapped | Chain order reversed - check panel1 JOUT -> panel2 JIN |

---

*Phase 1 is done when all six bring-up patterns render correctly on the full
128x64 chain and the real driver chip is verified and recorded.*
