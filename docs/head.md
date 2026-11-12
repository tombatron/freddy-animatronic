# Head

The head is a one-way eye motor, a home switch and a set of LEDs. There's no serial device and no position control beyond "run until home".

![Head wiring, inferred](../schematics/head_wiring_inferred.svg)

| Cable | Role | Behavior during a show |
| --- | --- | --- |
| 3 (yellow/orange) | Eye sweep motor | Switched on for the whole show, always one direction; a cam turns rotation into the side-to-side sweep. About 4.4 s per sweep, six sweeps |
| 1 (yellow/white) | Eye home switch | Yellow pulled up to 4.8V. Drops to 0V for ~250 ms once per sweep; the last drop (~460 ms) is when the board stops the motor |
| 2 (yellow/white) | Eye LEDs | Off at idle. Rapid on/off flicker near the end of the show; at moments the polarity reverses (possibly a back-to-back LED pair) |

**Stock logic:** start the eye motor with the show, keep it running, and at the end stop it on the next home-switch closure so the eyes face straight ahead.

**Confirmed by unplug tests:**
- Cable 2 unplugged: the eyes don't light.
- Cable 1 unplugged: the eyes still sweep, but stop off-center.

**A misleading reading:** at idle, cable 2 shows about 2.55V across the pair. That's leakage through the LEDs into the logging divider, not the LEDs being driven.

![Head show log](images/head_show.png)

Log: [captures/head_show.csv](../captures/head_show.csv) (from `freddy_head_monitor`).

## For Phase 2

- Eye motor: one low-side MOSFET (one direction is enough).
- Home switch: a GPIO with a pull-up; stop the motor on its falling edge.
- LEDs: MOSFET drive with PWM for dimming and flicker. If they turn out to be a back-to-back pair, use an H-bridge channel instead.
