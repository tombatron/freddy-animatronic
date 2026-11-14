// Freddy elbow controller (right elbow, device 01)
//
// Wiring (see Step 5 in the Phase 1 doc):
//   GPIO17 -> 1k -> elbow signal (cable 2 white)          [commands out]
//   GPIO1  <- motion-sense divider junction (47k/22k)     [7.6V rail, ~2.4V at pin]
//   ESP32 GND -> breadboard GND rail
//
// Frame (9 bytes): AB 01 09 41 [pos lo] [pos hi] [time lo] [time hi] [checksum]
//   pos  = target position, signed 16-bit little-endian (-700 = home, 350 = fully in, right arm)
//   time = move time in ms
//   checksum = (0xAF + 9*b0 + 8*b1 + ... + 2*b7) & 0xFF
//
// Keys (use picocom; the Arduino Serial Monitor can't send arrow keys):
//   Up arrow / Down arrow = jog the elbow in / out by one step
//   h          = home (-700)
//   i          = fully in (350)
//   m          = middle (-250)
//   j<num>     = set jog step, e.g. j25   (then Enter)
//   n<num>     = set device ID, e.g. n2 (1 = right elbow; left is unconfirmed)
//   p<num> t<num> then g = go to any position / move time
//   w          = toggle streaming the rail voltage during measured moves
//   c          = calibrate the motion detector
//   d<mV>      = set the detector threshold by hand
//   b / a      = checksum hunts (one target / all 16), kept for the left elbow
//   any key    = abort a running hunt
//
// Every move goes through moveTo(), which refuses anything outside the safe range.
//
// Power-up order: ESP32 on USB first, THEN plug in the 7.2V adapter.

const int TX_PIN = 17, RX_PIN = 18, SENSE_PIN = 1;
const float SCALE = (47.0f + 22.0f) / 22.0f;   // undo the divider -> real rail volts
const uint32_t SETTLE_MS = 2500;               // time for the elbow to get home

// Safe range: only what we've seen the stock board use
const int POS_HOME = -700, POS_MIN = -700, POS_MAX = 350;   // measured right-arm range
const int TIME_MIN = 200, TIME_MAX = 3000;     // move time limits, ms
const int JOG_TIME = 400;                      // move time for arrow-key jogs, ms

// One-bit variations used for the checksum hunt
struct Target { int pos; int time; };
const Target TARGETS[] = {
  {-249, 1500}, {-252, 1500}, {-254, 1500}, {-242, 1500},
  {-234, 1500}, {-218, 1500}, {-186, 1500}, {-122, 1500},
  {-250, 1501}, {-250, 1502}, {-250, 1496}, {-250, 1492},
  {-250, 1484}, {-250, 1532}, {-250, 1436}, {-250, 1372},
};
const int N_TARGETS = sizeof(TARGETS) / sizeof(TARGETS[0]);

float dipThresh = 0.10f;   // volts on the rail; set by 'c' or 'd'
bool stream = false;
int huntPos = -250, huntTime = 1500;
int elbowPos = POS_HOME;   // last position we commanded (assumed home at boot)
int jogStep = 50;
uint8_t deviceId = 0x01;   // 01 = right elbow; set with n<num> (left elbow might be 02)

uint8_t elbowChecksum(const uint8_t *f) {
  uint32_t s = 0xAF;
  for (int i = 0; i < 8; i++) s += (uint32_t)(9 - i) * f[i];
  return s & 0xFF;
}

void makeFrame(uint8_t *f, int pos, int time, uint8_t chk) {
  uint16_t p = (uint16_t)(int16_t)pos, t = (uint16_t)time;
  const uint8_t head[4] = {0xAB, deviceId, 0x09, 0x41};
  memcpy(f, head, 4);
  f[4] = p & 0xFF; f[5] = p >> 8; f[6] = t & 0xFF; f[7] = t >> 8; f[8] = chk;
}

void makeGoodFrame(uint8_t *f, int pos, int time) {
  makeFrame(f, pos, time, 0);
  f[8] = elbowChecksum(f);
}

void sendFrame(const uint8_t *f, const char *name, bool quiet = false) {
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

// Send a frame, then watch the rail for windowMs. Returns the dip in volts.
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

// The one gate every normal move goes through. Returns false if refused.
bool moveTo(int pos, int time, const char *name, bool measure) {
  if (pos < POS_MIN || pos > POS_MAX) {
    Serial.printf("REFUSED: position %d is outside %d..%d\r\n", pos, POS_MIN, POS_MAX);
    return false;
  }
  if (time < TIME_MIN || time > TIME_MAX) {
    Serial.printf("REFUSED: move time %d ms is outside %d..%d\r\n", time, TIME_MIN, TIME_MAX);
    return false;
  }
  uint8_t f[9];
  makeGoodFrame(f, pos, time);
  if (measure) sendAndMeasure(f, name, time + 500);
  else sendFrame(f, name);
  elbowPos = pos;
  Serial.printf("           elbow -> %d\r\n", elbowPos);
  return true;
}

void jog(int dir) {
  int target = elbowPos + dir * jogStep;
  if (target > POS_MAX) target = POS_MAX;
  if (target < POS_MIN) target = POS_MIN;
  if (target == elbowPos) {
    Serial.printf("at limit (%d)\r\n", elbowPos);
    return;
  }
  moveTo(target, JOG_TIME, dir > 0 ? "UP" : "DOWN", false);
}

void goHome() {
  uint8_t f[9];
  makeGoodFrame(f, POS_HOME, 1500);
  sendFrame(f, "HOME", true);
  elbowPos = POS_HOME;
  delay(SETTLE_MS);
}

bool aborted() {
  if (!Serial.available()) return false;
  while (Serial.available()) Serial.read();
  Serial.print("-- aborted --\r\n");
  return true;
}

void calibrate() {
  Serial.print("-- calibrating from home: bad frame (no move), middle, in --\r\n");
  uint8_t bad[9], mid[9], in[9];
  makeFrame(bad, -250, 1500, 0x00);   // wrong checksum: module ignores it
  makeGoodFrame(mid, -250, 1500);
  makeGoodFrame(in, 200, 1000);
  goHome();
  float dBad = sendAndMeasure(bad, "BAD", 1500);
  goHome();
  float dMid = sendAndMeasure(mid, "MID", 2000);
  goHome();
  float dIn  = sendAndMeasure(in,  "IN",  2000);
  goHome();
  float minMove = min(dMid, dIn);
  Serial.printf("   moves: mid %.3f  in %.3f   no-move (bad frame): %.3f\r\n", dMid, dIn, dBad);
  if (minMove > 2 * dBad + 0.05f) {
    dipThresh = (minMove + dBad) / 2;
    Serial.printf("-- detector OK. threshold set to %.3f V --\r\n", dipThresh);
  } else {
    Serial.print("-- moves and no-move look too similar. Detector not reliable (try the INA219). --\r\n");
  }
}

// Returns checksum found, -1 if none, -2 if aborted. Starts with the elbow at home.
int huntOne(int pos, int time, const char *tag = "1/1") {
  if (pos < POS_MIN || pos > POS_MAX || time < TIME_MIN || time > TIME_MAX) {
    Serial.printf("REFUSED: hunt target %d / %d ms is out of range\r\n", pos, time);
    return -1;
  }
  uint32_t t0 = millis();
  Serial.printf("-- [%s] hunting pos %d, time %d (threshold %.3f V) --\r\n", tag, pos, time, dipThresh);
  uint8_t f[9];
  goHome();
  for (int c = 0; c <= 0xFF; c++) {
    if (aborted()) return -2;
    makeFrame(f, pos, time, (uint8_t)c);
    float d = sendAndMeasure(f, "HUNT", 1200, true);
    Serial.printf("   [%s] chk %02X  dip %.3f V  %s  (%lus)\r\n", tag, c, d,
                  d > dipThresh ? "MOVED?" : "-", (unsigned long)((millis() - t0) / 1000));
    if (d > dipThresh) {
      goHome();
      float d2 = sendAndMeasure(f, "CONF", 1200, true);
      goHome();
      Serial.printf("   [%s] confirm dip %.3f V\r\n", tag, d2);
      if (d2 > dipThresh) {
        Serial.printf("HIT [%s] pos=%d time=%d chk=%02X after %lus  frame:", tag, pos, time, c,
                      (unsigned long)((millis() - t0) / 1000));
        for (int i = 0; i < 9; i++) Serial.printf(" %02X", f[i]);
        Serial.print("\r\n");
        return c;
      }
      Serial.print("   not confirmed, continuing\r\n");
    }
  }
  Serial.printf("NO HIT [%s] pos=%d time=%d\r\n", tag, pos, time);
  return -1;
}

void huntAll() {
  Serial.print("-- automatic hunt: 16 frames. Any key aborts. --\r\n");
  int found[N_TARGETS];
  uint32_t tAll = millis();
  for (int i = 0; i < N_TARGETS; i++) {
    char tag[8];
    snprintf(tag, sizeof(tag), "%d/%d", i + 1, N_TARGETS);
    found[i] = huntOne(TARGETS[i].pos, TARGETS[i].time, tag);
    Serial.printf("-- %d of %d done, %lu min elapsed --\r\n", i + 1, N_TARGETS,
                  (unsigned long)((millis() - tAll) / 60000));
    if (found[i] == -2) return;
  }
  Serial.print("-- SUMMARY (pos,time,checksum) --\r\n");
  for (int i = 0; i < N_TARGETS; i++) {
    if (found[i] >= 0) Serial.printf("%d,%d,%02X\r\n", TARGETS[i].pos, TARGETS[i].time, found[i]);
    else Serial.printf("%d,%d,none\r\n", TARGETS[i].pos, TARGETS[i].time);
  }
}

long readNumber() {
  String s = Serial.readStringUntil('\n');
  s.trim();
  return s.toInt();
}

// Arrow keys arrive as ESC [ A (up) or ESC [ B (down)
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
  Serial.print("\r\nFreddy elbow:  Up/Down arrows = jog   h = home   i = in   m = middle\r\n");
  Serial.print("  j<num> = jog step   p<num>/t<num> + g = go anywhere   w = stream\r\n");
  Serial.print("  c = calibrate   d<mV> = threshold   b / a = hunts\r\n");
  Serial.printf("  n<num> = device ID (now %02X)\r\n", deviceId);
  Serial.printf("  safe range %d..%d, jog step %d, elbow at %d, rail now %.2f V\r\n\r\n",
                POS_MIN, POS_MAX, jogStep, elbowPos, baseline());
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
    case 0x1B: {
      int dir = readArrow();
      if (dir != 0) jog(dir);
      break;
    }
    case 'h': moveTo(POS_HOME, 1500, "HOME", true); break;
    case 'i': moveTo(POS_MAX, 1000, "IN", true); break;
    case 'm': moveTo(-250, 1500, "MID", true); break;
    case 'j': {
      int s = readNumber();
      if (s >= 5 && s <= 300) { jogStep = s; Serial.printf("jog step = %d\r\n", jogStep); }
      else Serial.print("REFUSED: jog step must be 5..300\r\n");
      break;
    }
    case 'n': {
      int id = readNumber();
      if (id >= 1 && id <= 15) { deviceId = id; Serial.printf("device ID = %02X\r\n", deviceId); }
      else Serial.print("REFUSED: device ID must be 1..15\r\n");
      break;
    }
    case 'p': huntPos = readNumber();  Serial.printf("target position = %d\r\n", huntPos); break;
    case 't': huntTime = readNumber(); Serial.printf("target time = %d ms\r\n", huntTime); break;
    case 'g': moveTo(huntPos, huntTime, "GO", true); break;
    case 'w': stream = !stream; Serial.printf("stream %s\r\n", stream ? "on" : "off"); break;
    case 'c': calibrate(); break;
    case 'd': dipThresh = readNumber() / 1000.0f; Serial.printf("threshold = %.3f V\r\n", dipThresh); break;
    case 'b': huntOne(huntPos, huntTime); break;
    case 'a': huntAll(); break;
    case '\r': case '\n': break;
    default: printHelp();
  }
}
