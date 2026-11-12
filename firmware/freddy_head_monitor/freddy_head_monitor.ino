// Freddy head monitor
// Logs the six head wires (through the Step 6 dividers) as real voltages,
// plus the voltage ACROSS each cable's pair, so we can tell LEDs from the eye motor.
//
// Wiring (Step 6 in the Phase 1 doc), every channel through a 47k/22k divider:
//   GPIO1 = head 1 yellow    GPIO2 = head 1 white
//   GPIO3 = head 2 yellow    GPIO4 = head 2 white
//   GPIO5 = head 3 yellow    GPIO6 = head 3 orange
//   GPIO7 = 7.6V supply (7th divider)
//   GPIO10 = event button to GND (hold it while the eyes move, optional)
//   ESP32 GND -> breadboard - rail (Freddy's ground)
//
// Output: CSV at 100 lines/s. Each value is the average of 16 quick reads,
// so PWM-driven LEDs/motors show their average. The *_rip columns show the
// spread within those 16 reads: a big ripple means the line is being PWM'd.
//
// Record with:  picocom -b 115200 --logfile head.csv /dev/ttyACM0
// Then trigger Freddy, let a full cycle run, and quit (Ctrl+A, Ctrl+X).

const int N = 7;
const int PINS[N] = {1, 2, 3, 4, 5, 6, 7};
const char *NAMES[N] = {"h1y", "h1w", "h2y", "h2w", "h3y", "h3o", "vin"};
const int MARK_PIN = 10;
const float SCALE = (47.0f + 22.0f) / 22.0f;   // undo the divider
const int OVERSAMPLE = 16;
const uint32_t PERIOD_MS = 10;

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  for (int i = 0; i < N; i++) analogSetPinAttenuation(PINS[i], ADC_11db);
  pinMode(MARK_PIN, INPUT_PULLUP);
  delay(1500);
  Serial.print("t_ms");
  for (int i = 0; i < N; i++) Serial.printf(",%s", NAMES[i]);
  Serial.print(",d1,d2,d3");                       // pair differences per head cable
  for (int i = 0; i < 6; i++) Serial.printf(",%s_rip", NAMES[i]);
  Serial.print(",mark\r\n");
}

void loop() {
  static uint32_t next = millis();
  if ((int32_t)(millis() - next) < 0) return;
  next += PERIOD_MS;

  float mean[N], rip[N];
  for (int i = 0; i < N; i++) {
    float sum = 0, lo = 99, hi = -99;
    for (int k = 0; k < OVERSAMPLE; k++) {
      float v = analogReadMilliVolts(PINS[i]) * SCALE / 1000.0f;
      sum += v;
      if (v < lo) lo = v;
      if (v > hi) hi = v;
    }
    mean[i] = sum / OVERSAMPLE;
    rip[i] = hi - lo;
  }
  int mark = digitalRead(MARK_PIN) == LOW ? 1 : 0;

  Serial.printf("%lu", (unsigned long)millis());
  for (int i = 0; i < N; i++) Serial.printf(",%.2f", mean[i]);
  Serial.printf(",%.2f,%.2f,%.2f", mean[0] - mean[1], mean[2] - mean[3], mean[4] - mean[5]);
  for (int i = 0; i < 6; i++) Serial.printf(",%.2f", rip[i]);
  Serial.printf(",%d\r\n", mark);
}
