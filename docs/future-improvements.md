# Future improvements

Ideas worth doing later, with enough detail to pick them up cold.

## Eye angle sensor (needs a 3D printer)

**Why:** the eye home switch broke during the head bench test (its leaf contacts came out and couldn't be put back; the switch body itself still works). Since `freddy_head_web` v1.2 the eyes are positioned by time alone, with center set by eye, which drifts a little with every start and stop. An angle sensor reads where the eyes actually point, all the time: no drift, no turn-length learning, and calibration becomes "look left, look right, done".

**Where:** the white keyed peg on top of the eyeball is the eye's top pivot. It only twists (it doesn't move sideways) and there's plenty of room above it. Head apart, Nov 2026.

**Parts:**
- AS5600 magnetic angle sensor module (the common 23×23 mm board, 3-pack with diametric magnets, ~$8). The sensing element is the chip itself.
- A Qwiic-to-jumper cable (SparkFun PRT-14988 or similar): black GND, red VCC, blue SDA, yellow SCL. OUT, GPO unused.
- M2 screws and nylon standoffs for the board.

**Mounting:**
- Magnet glued flat and centered on the end of the peg (CA or epoxy).
- Board chip-down, centered over the peg, chip 0.5–3 mm (aim for ~1.5 mm) above the magnet.
- Printed bracket anchored to the two existing screw bosses just above the eye ring (no new holes), with one slotted hole for fine adjustment.
- Optional printed alignment cap: fits over the peg's flats with a nub that locates the board's center; remove it before gluing the magnet.
- **No-printer version:** a perfboard strip or aluminium mending plate across the same two bosses (one hole filed into a slot for adjustment), M2 nylon standoffs down from it, board chip-down on the standoffs; washers set the height. Lock with a dab of glue once aligned. Nylon (or brass) only near the chip: steel bends the field. If the board's holes are 3 mm, M2 still fits with washers.
- Tolerance: a few degrees of tilt or a fraction of a mm off-center only makes the reading slightly non-linear, and it's repeatable, so calibration takes it out. What matters is the gap and that nothing flexes.

**Board quirks to sort first:**
- Tie DIR to GND (often left floating on these boards, which makes readings jump).
- At 3.3 V the chip wants its VDD5V and VDD3V3 pins bridged; try as-is first, bridge them if readings are noisy or it drops off I2C.

**Controller:** no PCB change. J14 (Qwiic, head-end edge) has 3V3, GND, SDA (GPIO41), SCL (GPIO42) and its own 4.7k pull-ups; AS5600 is address 0x36. The enclosure lid already has a J14 cable notch. (On the head bench breadboard GPIO41/42 drive the LEDs, so use two free GPIOs there.)

**Firmware:**
- Alignment mode: live magnet status (too weak / OK / too strong), field magnitude, and angle wobble across a sweep, to center the bracket.
- Closed-loop eye positioning on the measured angle instead of timing; the calibration wizard shrinks to marking full left and full right.

**Stock board:** the AS5600 does nothing for it. If stock compatibility matters, also fit a roller microswitch on cable 1 (below).

## Eye home switch replacement (stock-compatible)

If the stock board needs its home signal back: an Omron D2F-01L3 (subminiature, 12.8×5.8×6.5 mm, hinge roller lever, gold contacts for low current) fits where the original switch sat (13.4 mm). Wire C and NO to head cable 1 (yellow, white); leave NC open. The roller makes reverse running safe, which is probably what pulled the original leaves out. Set it so the cam disc's pointed tab rolls the lever in by 1–2 mm at center.
