# Troubleshooting and lessons learned

| Symptom | Cause | Fix |
| --- | --- | --- |
| Every channel swings wildly, including a steady supply, maxing out at 9.95V | **No common ground** between the ESP32 and Freddy | ESP32 GND → breadboard − rail → tap board GND / power breakout −. Check with continuity |
| Meter shows ~7V on every wire of a cable | Meter's black probe on an unconnected ground | Probe against the power breakout's − screw. Sanity check: + should read ~7.6V |
| Logic analyzer misses motor drive | Drive lines only reach ~4.5V; through the divider that's ~1.4V, below the analyzer's ~2V threshold | Use the ESP32 ADC logger for motor lines; keep the analyzer for logic-level signals |
| Analyzer capture stops after under a second ("Device only sent N samples") | USB streaming dropouts on the FX2 clone | Lower the sample rate, change the cable or port, or use the ESP32 sniffer |
| `Permission denied: /dev/ttyACM0` | User not in `uucp`, or the session predates the group change | Log out and back in, or add the udev rule in [tooling.md](tooling.md) |
| `Device or resource busy` | Serial Monitor, Plotter or another `cat` holds the port | Close it; `fuser -v /dev/ttyACM0` |
| Upload: "No serial data received" | Board not in download mode, or the wrong USB port | Hold BOOT, tap RST, release BOOT; try the other USB-C port |
| `redefinition of 'void setup()'` | Two `.ino` files in one sketch folder | One folder per sketch, named after the file |
| Output prints as a staircase | `\n` without `\r` | `picocom --imap lfcrlf`, or use `\r\n` in sketches |
| Elbow movement looks weak in the replay rig | Module powered through breadboard contacts and thin jumpers | Keep the supply path short and thick; tie cable 3's black to ground too (it may share ground with the elbow module) |
| Calibration says "too similar" | Measuring moves that don't start from home (the module draws holding current anywhere else) | Start every calibration move from home |

## Safety habits that worked

- Measure every line with the meter before connecting anything else to it.
- Divider (or a 1kΩ resistor) on every ESP32 input; never connect anything straight to a GPIO.
- Two pin assignments on a cable can look identical; verify by measuring, not by color.
- Stock board out before the ESP32 transmits on any line.
- Check resistor values on the Ω setting before using them. 330Ω (orange-orange-brown) and 330kΩ (orange-orange-yellow) look nearly identical, and a 330k in an LED test reads as "the LEDs are dead".
