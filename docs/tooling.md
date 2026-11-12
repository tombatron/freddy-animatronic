# Tooling

## Arduino IDE (Arch Linux)

```bash
sudo pacman -S arduino-ide          # or: yay -S arduino-ide-bin
sudo usermod -aG uucp $USER         # serial port access; log out and back in
```

- Boards Manager URL: `https://espressif.github.io/arduino-esp32/package_esp32_index.json`, then install **esp32 by Espressif**.
- Board: **ESP32S3 Dev Module**. Set **USB CDC On Boot: Enabled** when using the board's native USB port.
- Every sketch needs its **own folder named after the file**, or the IDE compiles two `setup()` functions together.

A udev rule saves re-running `chmod` after every reset:

```bash
echo 'SUBSYSTEM=="tty", ATTRS{idVendor}=="303a", MODE="0666"' | sudo tee /etc/udev/rules.d/99-esp32.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
```

## Serial console and logging

```bash
sudo pacman -S picocom
picocom -b 115200 --logfile run.csv /dev/ttyACM0     # quit: Ctrl+A, Ctrl+X
```

Older sketches end lines with `\n` only; add `--imap lfcrlf` for tidy output. The Arduino Serial Monitor can't send arrow keys, so use picocom for the controllers.

## Logic analyzer (FX2 clone, "Saleae Logic" 8ch)

**Arch Linux:**

```bash
sudo pacman -S pulseview sigrok-cli sigrok-firmware-fx2lafw
printf '%s\n' \
'SUBSYSTEM=="usb", ATTRS{idVendor}=="0925", ATTRS{idProduct}=="3881", MODE="0666"' \
'SUBSYSTEM=="usb", ATTRS{idVendor}=="1d50", ATTRS{idProduct}=="608c", MODE="0666"' \
| sudo tee /etc/udev/rules.d/99-logic-analyzer.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
```

Decode serial to text:

```bash
sigrok-cli -d fx2lafw --config samplerate=1m --time 60s \
  -C D2 -P uart:rx=D2:baudrate=9600 -A uart=rx-data
```

In practice this clone dropped out on long captures over USB, so the ESP32 sniffer ([firmware/freddy_sniffer](../firmware/freddy_sniffer/)) turned out to be the more reliable way to read the elbow serial line.

**Windows:** install PulseView, then run Zadig (Options → List All Devices) and install **WinUSB** on `0925:3881`. After PulseView loads firmware, the device re-enumerates as `1d50:608C`, which may need WinUSB too. Pick the **fx2lafw** driver in PulseView.

## Analysis

Logs are CSV; pandas and matplotlib were enough. `tools/elbow_frame.py` builds, decodes and verifies elbow frames.
