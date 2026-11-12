# Firmware (ESP32-S3, Arduino)

Each sketch lives in its own folder, as the Arduino IDE requires. Board: **ESP32S3 Dev Module**; USB CDC On Boot: Enabled.

| Sketch | Purpose | Talks to Freddy? |
| --- | --- | --- |
| [freddy_logger](freddy_logger/) | Logs tap board rows 1–9 as real voltages (CSV, 200 Hz), with an event-marker button on GPIO10 | Listen only |
| [freddy_head_monitor](freddy_head_monitor/) | Logs the six head wires plus the supply, pair differences and PWM ripple (CSV, 100 Hz) | Listen only |
| [freddy_sniffer](freddy_sniffer/) | Reads an elbow's serial line on GPIO17 and decodes every frame | Listen only |
| [freddy_elbow_controller](freddy_elbow_controller/) | Drives an elbow: arrow-key jog, home/in/middle, go-to, range check, motion-sense calibration, checksum hunts | **Transmits**: stock board must be disconnected |
| [freddy_left_test](freddy_left_test/) | Elbow test suite: find device ID, automatic range explorer | **Transmits**: stock board must be disconnected |

Wiring for each sketch is described in the comment block at the top of its `.ino` file and in [docs/phase1-harness.md](../docs/phase1-harness.md).

The controller sketches clamp positions to the measured elbow ranges. Press `h` (home) after a reset, since the sketch assumes the elbow is at home when it boots.
