// Freddy LEFT elbow test suite
//
// Same wiring as the right-arm replay rig (Step 5 in the Phase 1 doc):
//   GPIO17 -> 1k -> replay plug signal -> LEFT cable 2 (row 5)
//   GPIO1  <- motion-sense divider junction (47k/22k on the 7.6V rail)
//   ESP32 GND -> breadboard GND rail
//   Stock board DISCONNECTED. Power-up order: ESP32 on USB first, THEN the adapter.
//
// The left elbow's ID and range are unknown, so this sketch starts in a
// conservative window (-200..200) that is safe even if the left arm is a
// mirror image of the right (-700..200 on the right).
//
// Keys (picocom -b 115200 /dev/ttyACM0):
//   f          = FIND ID: tries IDs 1..15 with a small safe move, reports which respond
//   n<num>     = set ID by hand, e.g. n2
//   z          = go to 0 (the safe center)
//   Up / Down  = jog +step / -step, never past the soft limits
//   j<num>     = jog step (5..200), e.g. j25
//   [  /  ]    = widen the LOWER / UPPER soft limit by 50 (deliberate exploration)
//   {  /  }    = narrow the LOWER / UPPER soft limit back to the current position
//   p<num> + g = go to a position (inside the soft limits)
//   c          = calibrate the motion detector (needs the ID set first)
//   w          = toggle streaming the rail voltage during measured moves
//   r          = report: ID, limits, position, threshold
//   x          = EXPLORE: auto-find the max up and max down by stepping outward
//                and checking the supply dip after each step (stops when the
//                servo stops responding). Sets the soft limits to what it finds.
//   s<num>     = explore step size (10..200), default 50
//
// If the elbow buzzes, strains or pushes against a stop: UNPLUG THE ADAPTER.

const int TX_PIN = 17, RX_PIN = 18, SENSE_PIN = 1;
const float SCALE = (47.0f + 22.0f) / 22.0f;
const int HARD_MIN = -900, HARD_MAX = 900;   // absolute cap, even when widening
const int MOVE_TIME = 1000, JOG_TIME = 400;

uint8_t deviceId = 0x01;     // confirmed: the left elbow also answers to 01
int softMin = -200, softMax = 200;
int elbowPos = 0;            // unknown at boot; 'z' makes it true
int jogStep = 25;
float dipThresh = 0.20f;     // right arm: moves dipped 0.46-0.80 V, no-move ~0
bool stream = false;
int target = 0;
int exploreStep = 50;
const int EXPLORE_CAP = 2000;       // never command beyond +/-2000 while exploring
const int STEP_TIME = 600;          // move time per explore step, ms

uint8_t checksum(const uint8_t *f) {
  uint32_t s = 0xAF;
  for (int i = 0; i < 8; i++) s += (uint32_t)(9 - i) * f[i];
  return s & 0xFF;
}

void makeFrame(uint8_t *f, uint8_t id, int pos, int time, bool good = true) {
  uint16_t p = (uint16_t)(int16_t)pos, t = (uint16_t)time;
  f[0] = 0xAB; f[1] = id; f[2] = 0x09; f[3] = 0x41;
  f[4] = p & 0xFF; f[5] = p >> 8; f[6] = t & 0xFF; f[7] = t >> 8;
  f[8] = good ? checksum(f) : 0x00;
}

void sendFrame(const uint8_t *f, const char *name, bool quiet) {
  Serial1.write(f, 9);
  Serial1.flush();
  if (quiet) return;
  Serial.printf("%8lu ms  %-5s:", (unsigned long)millis(), name);
  for (int i = 0; i < 9; i++) Serial.printf(" %02X", f[i]);
  Serial.print("\r\n");
}

float readRail() { return analogReadMilliVolts(SENSE_PIN) * SCALE / 1000.0f; }

float baseline() {
  float s = 0;
  for (int i = 0; i < 40; i++) { s += readRail(); delay(2); }
  return s / 40;
}

float sendAndMeasure(const uint8_t *f, const char *name, uint32_t windowMs, bool quiet = false) {
  float base = baseline();
  sendFrame(f, name, quiet);
  float buf[5] = {base, base, base, base, base};
  int bi = 0;
  float minAvg = base;
  uint32_t start = millis();
  while (millis() - start < windowMs) {
    buf[bi] = readRail(); bi = (bi + 1) % 5;
    float avg = (buf[0] + buf[1] + buf[2] + buf[3] + buf[4]) / 5;
    if (avg < minAvg) minAvg = avg;
    if (stream) Serial.printf("  %5lu ms  %.3f V\r\n", (unsigned long)(millis() - start), avg);
    delay(5);
  }
  float dip = base - minAvg;
  if (!quiet) Serial.printf("           dip %.3f V (rail %.2f V)%s\r\n", dip, base, dip > dipThresh ? "  <- MOVED" : "");
  return dip;
}

// Every normal move goes through here. Refuses anything outside the soft limits.
bool moveTo(int pos, int time, const char *name, bool measure) {
  if (pos < softMin || pos > softMax) {
    Serial.printf("REFUSED: %d is outside the soft limits %d..%d\r\n", pos, softMin, softMax);
    return false;
  }
  uint8_t f[9];
  makeFrame(f, deviceId, pos, time);
  if (measure) sendAndMeasure(f, name, time + 600);
  else sendFrame(f, name, false);
  elbowPos = pos;
  Serial.printf("           left elbow -> %d   (limits %d..%d, ID %02X)\r\n", elbowPos, softMin, softMax, deviceId);
  return true;
}

void jog(int dir) {
  int t = elbowPos + dir * jogStep;
  if (t > softMax) t = softMax;
  if (t < softMin) t = softMin;
  if (t == elbowPos) {
    Serial.printf("at soft limit (%d). Use [ or ] to widen it deliberately.\r\n", elbowPos);
    return;
  }
  moveTo(t, JOG_TIME, dir > 0 ? "UP" : "DOWN", false);
}

// Try each ID with a small safe move (0 -> 80 -> 0). Watch the elbow too.
void findId() {
  Serial.print("-- FIND ID: trying IDs 1..15 with a small move (0 -> 80 -> 0). Watch the elbow. --\r\n");
  int hits = 0;
  for (int id = 1; id <= 15; id++) {
    if (Serial.available()) { while (Serial.available()) Serial.read(); Serial.print("-- aborted --\r\n"); return; }
    uint8_t a[9], b[9], c[9];
    makeFrame(a, id, 0, MOVE_TIME);
    makeFrame(b, id, 80, MOVE_TIME);
    makeFrame(c, id, 0, MOVE_TIME);
    Serial.printf("   ID %02X ...", id);
    float d1 = sendAndMeasure(a, "", 1500, true);
    float d2 = sendAndMeasure(b, "", 1500, true);
    float d3 = sendAndMeasure(c, "", 1500, true);
    float best = max(d1, max(d2, d3));
    bool moved = best > dipThresh;
    if (moved) hits++;
    Serial.printf(" dips %.3f / %.3f / %.3f  %s\r\n", d1, d2, d3, moved ? "<- RESPONDS" : "-");
    if (moved && hits == 1) { deviceId = id; elbowPos = 0; }
  }
  if (hits == 0) Serial.print("-- no ID responded. Check the replay plug and power, or the detector threshold. --\r\n");
  else Serial.printf("-- done. Using ID %02X (first responder). %d ID(s) responded. --\r\n", deviceId, hits);
}

void calibrate() {
  Serial.printf("-- calibrating with ID %02X: bad frame (no move), then 0 -> 80 -> 0 --\r\n", deviceId);
  uint8_t bad[9], a[9], b[9];
  makeFrame(bad, deviceId, 80, MOVE_TIME, false);
  makeFrame(a, deviceId, 80, MOVE_TIME);
  makeFrame(b, deviceId, 0, MOVE_TIME);
  moveTo(0, MOVE_TIME, "ZERO", false);
  delay(1500);
  float dBad = sendAndMeasure(bad, "BAD", 1500);
  float dA = sendAndMeasure(a, "TO80", 1600);
  float dB = sendAndMeasure(b, "TO0", 1600);
  elbowPos = 0;
  float minMove = min(dA, dB);
  Serial.printf("   moves %.3f / %.3f   no-move %.3f\r\n", dA, dB, dBad);
  if (minMove > 2 * dBad + 0.05f) {
    dipThresh = (minMove + dBad) / 2;
    Serial.printf("-- detector OK. threshold %.3f V --\r\n", dipThresh);
  } else {
    Serial.print("-- moves too small to tell apart here. Keep threshold; trust your eyes. --\r\n");
  }
}

// ---------- automatic range explorer ----------

bool abortRequested() {
  if (!Serial.available()) return false;
  while (Serial.available()) Serial.read();
  Serial.print("-- aborted --\r\n");
  return true;
}

// Send a raw position (bypasses soft limits, capped) and return the dip.
float stepTo(int pos, const char *name) {
  uint8_t f[9];
  makeFrame(f, deviceId, pos, STEP_TIME);
  return sendAndMeasure(f, name, STEP_TIME + 400, true);
}

// Measure what a small step looks like versus no move, and set the threshold.
bool calibrateSmallSteps() {
  Serial.printf("-- calibrating small steps of %d with ID %02X --\r\n", exploreStep, deviceId);
  stepTo(0, "ZERO");
  delay(1500);
  uint8_t bad[9];
  makeFrame(bad, deviceId, exploreStep, STEP_TIME, false);
  float dBad = sendAndMeasure(bad, "BAD", STEP_TIME + 400, true);
  float d1 = stepTo(exploreStep, "S+");
  float d2 = stepTo(0, "S0");
  float d3 = stepTo(-exploreStep, "S-");
  float d4 = stepTo(0, "S0");
  elbowPos = 0;
  float minMove = min(min(d1, d2), min(d3, d4));
  Serial.printf("   step dips %.3f %.3f %.3f %.3f   no-move %.3f\r\n", d1, d2, d3, d4, dBad);
  if (minMove > 2 * dBad + 0.02f) {
    dipThresh = (minMove + dBad) / 2;
    Serial.printf("-- small steps are detectable. threshold %.3f V --\r\n", dipThresh);
    return true;
  }
  Serial.print("-- small steps are NOT reliably detectable at this size. Try a bigger step (s100) or an INA219. --\r\n");
  return false;
}

// Step outward from 0 in one direction until two steps in a row cause no dip.
// Returns the last position that produced a response. Sets *ok=false if aborted.
int findLimit(int dir, bool *ok) {
  Serial.printf("-- exploring %s from 0 in steps of %d --\r\n", dir > 0 ? "UP (+)" : "DOWN (-)", exploreStep);
  int pos = 0, lastMoved = 0, misses = 0;
  while (true) {
    if (abortRequested()) { *ok = false; return lastMoved; }
    int next = pos + dir * exploreStep;
    if (abs(next) > EXPLORE_CAP) { Serial.print("   reached the explore cap\r\n"); break; }
    float d = stepTo(next, "EXP");
    bool moved = d > dipThresh;
    Serial.printf("   pos %6d  dip %.3f V  %s\r\n", next, d, moved ? "moved" : "no response");
    if (moved) { lastMoved = next; misses = 0; }
    else if (++misses >= 2) break;
    pos = next;
  }
  Serial.printf("-- %s limit: about %d (between %d and %d) --\r\n", dir > 0 ? "UP" : "DOWN",
                lastMoved, lastMoved, lastMoved + dir * exploreStep);
  return lastMoved;
}

void explore() {
  Serial.print("-- EXPLORE: any key aborts. If it strains or buzzes, unplug the adapter. --\r\n");
  if (!calibrateSmallSteps()) return;
  bool ok = true;
  int up = findLimit(+1, &ok);
  stepTo(0, "BACK"); delay(1500); elbowPos = 0;
  if (!ok) return;
  int down = findLimit(-1, &ok);
  stepTo(0, "BACK"); delay(1500); elbowPos = 0;
  if (!ok) return;
  softMax = up; softMin = down;
  Serial.printf("\r\n== LEFT elbow range: %d .. %d  (step %d, ID %02X). Soft limits set to this. ==\r\n\r\n",
                down, up, exploreStep, deviceId);
}

void report() {
  Serial.printf("\r\nLEFT elbow: ID %02X, position %d, soft limits %d..%d, jog %d, threshold %.3f V, rail %.2f V\r\n\r\n",
                deviceId, elbowPos, softMin, softMax, jogStep, dipThresh, baseline());
}

long readNumber() {
  String s = Serial.readStringUntil('\n');
  s.trim();
  return s.toInt();
}

int readArrow() {
  uint32_t start = millis();
  char seq[2];
  int n = 0;
  while (n < 2 && millis() - start < 30) {
    if (Serial.available()) seq[n++] = Serial.read();
  }
  if (n == 2 && seq[0] == '[') {
    if (seq[1] == 'A') return 1;
    if (seq[1] == 'B') return -1;
  }
  return 0;
}

void printHelp() {
  Serial.print("\r\nFreddy LEFT elbow test suite\r\n");
  Serial.print("  f = find ID   n<num> = set ID   z = go to 0   Up/Down = jog   j<num> = step\r\n");
  Serial.print("  [ / ] = widen lower/upper limit by 50   { / } = pull limit in to here\r\n");
  Serial.print("  p<num> + g = go to position   c = calibrate   w = stream   r = report\r\n");
  Serial.print("  x = auto-explore range   s<num> = explore step\r\n");
  Serial.print("  Start with: f (find ID), then x (auto-explore the range)\r\n");
  report();
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  analogReadResolution(12);
  analogSetPinAttenuation(SENSE_PIN, ADC_11db);
  delay(1500);
  printHelp();
}

void loop() {
  if (!Serial.available()) return;
  char c = Serial.read();
  switch (c) {
    case 0x1B: { int d = readArrow(); if (d) jog(d); break; }
    case 'f': findId(); break;
    case 'n': {
      int id = readNumber();
      if (id >= 1 && id <= 15) { deviceId = id; Serial.printf("ID = %02X\r\n", deviceId); }
      else Serial.print("REFUSED: ID must be 1..15\r\n");
      break;
    }
    case 'z': moveTo(0, MOVE_TIME, "ZERO", true); break;
    case 'j': {
      int s = readNumber();
      if (s >= 5 && s <= 200) { jogStep = s; Serial.printf("jog step = %d\r\n", jogStep); }
      else Serial.print("REFUSED: step must be 5..200\r\n");
      break;
    }
    case '[': softMin = max(HARD_MIN, softMin - 50); Serial.printf("lower limit now %d (explore carefully)\r\n", softMin); break;
    case ']': softMax = min(HARD_MAX, softMax + 50); Serial.printf("upper limit now %d (explore carefully)\r\n", softMax); break;
    case '{': softMin = elbowPos; Serial.printf("lower limit pulled in to %d\r\n", softMin); break;
    case '}': softMax = elbowPos; Serial.printf("upper limit pulled in to %d\r\n", softMax); break;
    case 'p': target = readNumber(); Serial.printf("target = %d\r\n", target); break;
    case 'g': moveTo(target, MOVE_TIME, "GO", true); break;
    case 'c': calibrate(); break;
    case 'w': stream = !stream; Serial.printf("stream %s\r\n", stream ? "on" : "off"); break;
    case 'r': report(); break;
    case 'x': explore(); break;
    case 's': {
      int v = readNumber();
      if (v >= 10 && v <= 200) { exploreStep = v; Serial.printf("explore step = %d\r\n", exploreStep); }
      else Serial.print("REFUSED: explore step must be 10..200\r\n");
      break;
    }
    case '\r': case '\n': break;
    default: printHelp();
  }
}
