// Freddy serial sniffer
// Listens to an elbow signal line (stock board still in control) and prints
// every message as hex, decoded with the solved frame format.
//
// Wiring:
//   signal row HOT pin (must measure under 3.6V!) -> 1k resistor -> GPIO17
//   ESP32 GND -> tap board GND
// GPIO17 only listens (input); nothing is ever sent on Freddy's line.
//
// Open with: picocom -b 115200 /dev/ttyACM0

const int RX_PIN = 17;
const int TX_PIN = 16;            // unused, leave unconnected
const uint32_t GAP_MS = 4;        // silence that ends a message

uint8_t buf[64];
int n = 0;
uint32_t lastByte = 0;
uint32_t frames = 0;

uint8_t checksum(const uint8_t *f) {
  uint32_t s = 0xAF;
  for (int i = 0; i < 8; i++) s += (uint32_t)(9 - i) * f[i];
  return s & 0xFF;
}

void flushMessage() {
  if (n == 0) return;
  Serial.printf("%9lu ms  %2d bytes:", (unsigned long)lastByte, n);
  for (int i = 0; i < n; i++) Serial.printf(" %02X", buf[i]);
  if (n == 9 && buf[0] == 0xAB) {
    int16_t pos = (int16_t)(buf[4] | (buf[5] << 8));
    uint16_t tm = buf[6] | (buf[7] << 8);
    bool ok = checksum(buf) == buf[8];
    frames++;
    Serial.printf("   id %02X cmd %02X pos %6d time %5u  chk %s", buf[1], buf[3], pos, tm, ok ? "OK" : "BAD");
  }
  Serial.print("\r\n");
  n = 0;
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  delay(1500);
  Serial.print("\r\nFreddy sniffer: listening on GPIO17 at 9600 8N1. Trigger Freddy.\r\n\r\n");
}

void loop() {
  while (Serial1.available()) {
    if (n >= (int)sizeof(buf)) flushMessage();
    buf[n++] = Serial1.read();
    lastByte = millis();
  }
  if (n > 0 && millis() - lastByte > GAP_MS) flushMessage();
}
