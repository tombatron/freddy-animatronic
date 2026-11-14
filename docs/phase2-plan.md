# Phase 2: custom controller (plan)

> The first board draft is in [phase2-pcb.md](phase2-pcb.md). It changes some choices below: DRV8876 instead of DRV8871 (runs below 6.5V, adds current sensing), a TB6612 for the eye motor and LEDs instead of MOSFETs, and an MP1584 buck for audio.

Replace the stock board with one ESP32-S3 that plugs into the existing harness connectors. Heavy work (speech, vision, AI) runs on a PC; the ESP32 stays real-time: motors, sensors, audio playback.

## What the controller has to drive and read

| Function | Count | Interface | Notes |
| --- | --- | --- | --- |
| Shoulder motors | 2 | H-bridge channel each (DRV8871 or similar) | ~4 Ω, stall ~1.8A at 7.2V. PWM sets lift speed |
| Shoulder switches | 2 | GPIO input | Already 3.3V logic. Provide 3.3V on cable 3 red |
| Elbow servos | 2 | UART TX, one pin per elbow, 1kΩ series | 9600 8N1, frames per [elbow-protocol.md](elbow-protocol.md). Power from 7V |
| Eye motor | 1 | Low-side MOSFET | One direction |
| Eye home switch | 1 | GPIO with pull-up | Stop on falling edge |
| Eye LEDs | 1 | TB6612 channel (PWM) | One direction, ~2.8V at ~20mA |
| Trigger jack | 1 | GPIO with pull-up | Contact closure |
| PIR | 1 | GPIO | Verify voltage first |
| Supply monitor | 1 | ADC via 47k/22k | Brownout and stall hints |
| Audio (later) | 1 | I2S: MAX98357A + speaker | 3 GPIOs, separate 5V supply |

## Power

- The 7.2V 4A adapter sags to ~4.9V under a shoulder lift, so a plain buck converter would brown out the ESP32.
- Logic: **Pololu S7V7F5** (5V step-up/step-down, ~1A) with a 470–1000µF input capacitor.
- Audio: a separate regulator. Ordered: an MP1584 buck (the amp tolerates a sag, so buck-boost isn't needed).
- Consider a **7.5V 6A** adapter for headroom.
- Keep the 5A fuse. Use a 22AWG or heavier main feed (the Phase 1 lead is thinner).

## Optional sensors

- **INA219** current sensor: stall detection and motion confirmation.
- **MPU6050** on an upper arm: real shoulder angle for "go to 45°" control.
- Leave an I2C header for both.

## Board

Design in KiCad: ESP32-S3 dev board on female headers, H-bridge and MOSFET drivers, regulator modules on headers, SM connector footprints matching the harness, an I2C header, an I2S amp footprint, and a speaker connector. Five boards from JLCPCB cost about $20.

## Firmware outline

- Motion primitives: `shoulderLift(speed, ms)`, `shoulderHome()`, `elbowMove(side, pos, ms)` (clamped), `eyesSweep(on)`, `eyesHome()`, `eyeLeds(level)`.
- Choreography as timed keyframes, so the stock show can be reproduced first, then customized.
- WiFi link to a PC service for reactions and, later, audio.
