// Freddy Phase 1 logger
// Reads tap board rows 1-9 (blue junction pins only, never the red hot pins)
// on ESP32-S3 GPIO1-GPIO9 and prints the real line voltages.
// GPIO10 is an event marker: wire a pushbutton between GPIO10 and GND and
// hold it while something happens (e.g. the elbow moving). "mark" logs 1 while pressed.
//
// Wiring: tap board GND pin -> ESP32 GND first, then junction pin of row N -> GPIO N.

const int PINS[9] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
const int MARK_PIN = 10;

// Undo the 47k/22k divider so readings show the real voltage on Freddy's wire.
const float SCALE = (47.0f + 22.0f) / 22.0f;   // about 3.14

const uint32_t PERIOD_MS = 5;                  // 200 samples per second

// 1 = Arduino Serial Plotter (live graph), 0 = CSV text you can save and send
#define PLOTTER 0

void setup() {
  Serial.begin(921600);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);              // pin range about 0-3.1V
  pinMode(MARK_PIN, INPUT_PULLUP);
  delay(500);
#if !PLOTTER
  Serial.println("t_ms,r1,r2,r3,r4,r5,r6,r7,r8,r9,mark");
#endif
}

void loop() {
  static uint32_t next = millis();
  if ((int32_t)(millis() - next) < 0) return;
  next += PERIOD_MS;

  float v[9];
  for (int i = 0; i < 9; i++) {
    v[i] = analogReadMilliVolts(PINS[i]) * SCALE / 1000.0f;
  }
  int mark = digitalRead(MARK_PIN) == LOW ? 1 : 0;

#if PLOTTER
  for (int i = 0; i < 9; i++) Serial.printf("r%d:%.2f ", i + 1, v[i]);
  Serial.printf("mark:%d\n", mark * 5);
#else
  Serial.printf("%lu", (unsigned long)millis());
  for (int i = 0; i < 9; i++) Serial.printf(",%.2f", v[i]);
  Serial.printf(",%d\n", mark);
#endif
}
