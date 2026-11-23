/*
  freddy_head_web: head bench test with a web dashboard

  v3: the eyes are positioned from their MEASURED angle. An AS5600 magnetic angle
  sensor sits over a diametric magnet on the eyeball's top pivot, so the firmware
  always knows where the eyes point: no homing, no timing, no drift.

  Calibration is manual (on the page): move the eyes with raw cam disc pulses or precise
  eye nudges, and press "Set here" for full left, center and full right. Each point is
  saved to flash as soon as it's set; redo any one of them any time. A "Show range" sweep
  can draw the extremes the cam disc actually reaches, as a hint.

  Go to: eye position in % from -100 (full left) through 0 (center) to +100 (full
  right). A control loop drives the cam disc either way, slows near the target and stops
  within the tolerance (default 1 degree).

  The cam disc turns continuously and swings the eyes center -> right -> center -> left, so
  which motor direction moves the eyes which way depends on where the cam disc is. The
  control loop doesn't need to know: it watches the angle and reverses when the eyes
  move away from the target.

  Also: manual pulses and free run (for debugging), LED modes, LED steps, and the
  sensor details panel (alignment: magnet status, field swing, noise).

    http://freddy.local   (redirects to Freddy's IP, so later requests skip the
                           sometimes-slow .local lookup; or use the IP from the serial monitor)

  The page uses Hotwired Turbo from a CDN, with a few lines of its own JS as a fallback
  when there's no internet (Freddy's access point). Live values go out over a
  WebSocket only when they change, at most 10 times a second per value.

  Board: ESP32S3 Dev Module, USB CDC On Boot: Enabled. Arduino-ESP32 core 3.x.
  Libraries (Library Manager): "ESP Async WebServer" and "Async TCP", both by ESP32Async.
  WiFi: copy secrets.h.example to secrets.h. Without it, Freddy starts an access point
  "Freddy" (password "fazbear1987") at http://192.168.4.1

  STOCK BOARD DISCONNECTED FROM THE HEAD. Wiring: docs/head-bench-test.md
  (breadboard pins below; set USE_PCB_PINS to 1 on the controller board).
  AS5600: VCC 3V3, GND, DIR to GND, SDA/SCL below. Breadboard: SDA 4, SCL 5 with 4.7k
  pull-ups. PCB: the Qwiic port J14 (SDA 41, SCL 42, pull-ups on the board).

  Serial: x or space = stop, ? = status.
*/

#include <WiFi.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <Wire.h>
#include "esp_task_wdt.h"
#include "page.h"

#if __has_include("secrets.h")
#include "secrets.h"
#define HAVE_WIFI_SECRETS 1
#endif
#ifndef AP_PASS
#define AP_PASS "fazbear1987"
#endif

// Bump FW_VERSION with each change; the build time tells two builds of one version apart.
#define FW_VERSION "4.2.0"
const char *FW_BUILD = __DATE__ " " __TIME__;
const char *HOSTNAME = "freddy";
const char *AP_SSID = "Freddy";

// ---------------- pins
#define USE_PCB_PINS 0                     // 0 = head bench breadboard, 1 = controller PCB
#if USE_PCB_PINS
const int PIN_M1 = 11, PIN_M2 = 12;        // TB6612 AIN1/AIN2: eye motor
const int PIN_L1 = 15, PIN_L2 = 16;        // TB6612 BIN1/BIN2: eye LEDs
const int PIN_SDA = 41, PIN_SCL = 42;      // Qwiic port J14 (AS5600)
#else
const int PIN_M1 = 38, PIN_M2 = 39;
const int PIN_L1 = 41, PIN_L2 = 42;
const int PIN_SDA = 4, PIN_SCL = 5;        // AS5600 on the breadboard
#endif
#ifdef RGB_BUILTIN
const int PIN_RGB = RGB_BUILTIN;
#else
const int PIN_RGB = 48;
#endif

const int LED_BITS = 10, LED_MAX = (1 << LED_BITS) - 1;   // 10 bits for smooth dimming
const uint32_t RUN_LIMIT_MS = 30000;       // free running stops by itself after this
const int TURN_MS = 4224;                  // one cam disc turn at full speed (measured on the bench)
const float EDGE_MARGIN = 2.5f;            // degrees: targets stay this far inside the extremes (the eyes barely move there)

// ---------------- settings (saved in flash)
int speedPct = 100;                        // free-run speed
bool haveL = false, haveR = false, haveC = false;   // which points have been set
float rightDeg = 0, leftDeg = 0, centerDeg = 0;     // sensor angles (0..360) at full right, full left, center
bool calibrated = false;                   // all three set, and center lies between the ends
bool hintSwept = false;                    // a range sweep has run: its extremes are drawn as hints
float hintA = 0, hintB = 0;
float tolDeg = 1.0f;                       // a go-to is done within this many degrees
int minSpeed = 50;                         // slowest the control loop drives (near the target)
bool symmetric = false;                    // limit both sides to the shorter side's travel
// Path: how a go-to picks the cam disc direction.
//   0 shortest   reverse within the segment, whichever way gets there first
//   1 forward    the disc only turns forward (like the stock board), over an extreme if need be
//   2 preferred  a chosen disc direction for moving the eyes right, and one for moving left
// Fixed directions (1, 2) still allow a small reverse correction within FINE_ZONE of the target.
// Defaults from the bench head (Nov 2026): preferred, right = disc reverse, left = disc forward.
int pathMode = 2;
int prefRight = -1, prefLeft = 1;          // disc direction for each eye direction in mode 2 (+1 forward, -1 reverse)
const float FINE_ZONE = 3.0f;
const float END_ZONE = 5.0f;               // "at an end of travel": within this many degrees of it
const char *PATH_NAMES[] = {"shortest", "forward only", "preferred per direction"};
const float SLOW_ZONE = 15.0f;             // full speed until this close, then ramp down to minSpeed
int ledPct = 60, ledPeriod = 3000;
enum LedMode { L_OFF, L_ON, L_BREATHE, L_FLICKER, L_BLINK };
const char *LED_MODES[] = {"off", "on", "breathe", "flicker", "blink"};
LedMode ledMode = L_OFF;

// ---------------- moves
enum MoveType { MV_NONE, MV_RUN, MV_TIMED, MV_PAUSE, MV_SEEK };
struct Move {
  MoveType type = MV_NONE;
  int dir = 1, pct = 100;
  uint32_t start = 0, ms = 0;              // TIMED/PAUSE length, RUN limit
} mv;
bool motorOn() { return mv.type == MV_RUN || mv.type == MV_TIMED || mv.type == MV_SEEK; }
// Time since t, never negative (a move started later in this pass can be "in the future").
uint32_t since(uint32_t t, uint32_t now) { return (int32_t)(now - t) > 0 ? now - t : 0; }

// closed-loop seek state
struct {
  float target = 0;                        // sensor angle, 0..360
  float tol = 1.0f;                        // this move's tolerance, degrees
  int dir = 1, pct = 100;
  uint32_t checkAt = 0, settleAt = 0, kickUntil = 0;
  float angAtCheck = 0;
  int flips = 0, settles = 0;
  const char *name = "";
  bool gentle = false;                     // a nudge: stop quietly when close instead of failing
  int mode = 0;                            // path mode for this move (nudges always use shortest)
  bool approaching = true;                 // was the angle last seen moving toward the target?
  // Which way the eyes moved (on the sensor) the last time the cam disc ran forward: +1, -1,
  // or 0 = unknown. It only changes when the eyes pass an extreme, so it's a good guess
  // for the next move's starting direction.
  int fwdSign = 0;
} sk;

// ---------------- jobs
enum Job { J_NONE, J_SWEEP, J_TOUR, J_DART };
const char *JOB_NAMES[] = {"none", "showing range", "tour", "dart"};
Job job = J_NONE;
int jStep = 0;
// LED steps (runs alongside anything else)
const int LED_STEPS[] = {100, 50, 25, 12, 6, 3, 2, 1, 0};
int ledStep = -1; uint32_t ledStepAt = 0;

float sweepStartDeg = 0;

Preferences prefs;
int resetReason = 0;

// ---------------- web
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
struct Cmd { char a; int v; int t; };
QueueHandle_t cmdQueue;                    // web callbacks run in another task: hand commands to loop()

void jobStep(bool ok);
void startJob(Job j);
void report(const char *fmt, ...);

// ---------------------------------------------------------------- angles
float norm360(float a) { a = fmodf(a, 360.0f); return a < 0 ? a + 360.0f : a; }
float wrap180(float a) { a = fmodf(a + 180.0f, 360.0f); if (a < 0) a += 360.0f; return a - 180.0f; }

// ---------------------------------------------------------------- AS5600 eye angle sensor
const uint8_t AS5600_ADDR = 0x36;
const int ENC_WIN = 200;                   // 1 s of readings at 200 Hz, for the noise figure
struct {
  bool found = false;                      // answered on I2C
  uint8_t status = 0;                      // bit 5 MD magnet detected, 4 ML too weak, 3 MH too strong
  uint16_t raw = 0;                        // 0..4095 = one full turn
  uint8_t agc = 0;                         // gain the chip is using: mid-range is best
  uint16_t mag = 0;                        // field strength (CORDIC magnitude)
  uint16_t magMin = 0xFFFF, magMax = 0;    // statistics since the last reset
  float unwrapped = 0, turnMin = 0, turnMax = 0;
  uint16_t lastRaw = 0; bool haveLast = false;
  float win[ENC_WIN]; int winN = 0, winI = 0;
  uint32_t lastPoll = 0, lastProbe = 0;
} enc;

bool magnetOk() { return enc.found && (enc.status & 0x20); }
float angNow() { return enc.raw * 360.0f / 4096; }

bool asRead(uint8_t reg, uint8_t *buf, int n) {
  Wire.beginTransmission(AS5600_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)AS5600_ADDR, n) != n) return false;
  for (int i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

void encResetStats() {
  enc.magMin = 0xFFFF; enc.magMax = 0;
  enc.unwrapped = enc.turnMin = enc.turnMax = 0;
  enc.haveLast = false; enc.winN = enc.winI = 0;
}

// Poll at 200 Hz while it answers; look for it once a second while it doesn't.
void encPoll(uint32_t now) {
  if (now - enc.lastPoll < 5) return;
  enc.lastPoll = now;
  if (!enc.found && now - enc.lastProbe < 1000) return;
  enc.lastProbe = now;
  uint8_t a[3], b[3];
  bool ok = asRead(0x0B, a, 3) && asRead(0x1A, b, 3);   // STATUS, RAW ANGLE / AGC, MAGNITUDE
  if (!ok) { if (enc.found) report("AS5600: lost on I2C"); enc.found = false; return; }
  if (!enc.found) { report("AS5600: found on I2C"); encResetStats(); }
  enc.found = true;
  enc.status = a[0];
  enc.raw = ((a[1] & 0x0F) << 8) | a[2];
  enc.agc = b[0];
  enc.mag = ((b[1] & 0x0F) << 8) | b[2];
  if (!(enc.status & 0x20)) return;
  enc.magMin = min(enc.magMin, enc.mag);
  enc.magMax = max(enc.magMax, enc.mag);
  if (enc.haveLast) {
    int d = (int)enc.raw - (int)enc.lastRaw;            // shortest way round
    if (d > 2048) d -= 4096;
    if (d < -2048) d += 4096;
    enc.unwrapped += d * 360.0f / 4096;
    enc.turnMin = min(enc.turnMin, enc.unwrapped);
    enc.turnMax = max(enc.turnMax, enc.unwrapped);
  }
  enc.lastRaw = enc.raw; enc.haveLast = true;
  enc.win[enc.winI] = enc.unwrapped;
  enc.winI = (enc.winI + 1) % ENC_WIN;
  if (enc.winN < ENC_WIN) enc.winN++;
}

float encNoise() {                         // peak-to-peak over the last second, degrees
  if (enc.winN < 20) return 0;
  float lo = 1e9f, hi = -1e9f;
  for (int i = 0; i < enc.winN; i++) { lo = min(lo, enc.win[i]); hi = max(hi, enc.win[i]); }
  return hi - lo;
}

// ---------------------------------------------------------------- eye coordinates
// rel = degrees from center, + toward the right. pct = -100 (full left) .. +100 (full right).
float rightSign() { return wrap180(rightDeg - leftDeg) >= 0 ? 1.0f : -1.0f; }
float relOf(float sensorDeg) { return wrap180(sensorDeg - centerDeg) * rightSign(); }
float rangeR() { return relOf(rightDeg); }           // > 0
float rangeL() { return -relOf(leftDeg); }           // > 0
// The % scale: in symmetric mode both sides use the shorter side's travel, so left 50% and
// right 50% are mirror images. "Far" targets always use the true extremes.
// (symmetric: the shorter side minus the edge margin, so both sides stop at exactly the same angle)
float symRange() { return max(1.0f, min(rangeR(), rangeL()) - EDGE_MARGIN); }
float effR() { return symmetric ? symRange() : rangeR(); }
float effL() { return symmetric ? symRange() : rangeL(); }
float pctOfRel(float rel) { return rel >= 0 ? 100.0f * rel / max(1.0f, effR()) : 100.0f * rel / max(1.0f, effL()); }
float sensorOfPct(float pct, bool far = false) {
  pct = constrain(pct, -100.0f, 100.0f);
  float rr = far ? rangeR() : effR(), rl = far ? rangeL() : effL();
  float rel = pct >= 0 ? pct / 100.0f * rr : pct / 100.0f * rl;
  rel = constrain(rel, -rangeL() + EDGE_MARGIN, rangeR() - EDGE_MARGIN);
  return norm360(centerDeg + rel * rightSign());
}

// ---------------------------------------------------------------- hardware
// Slow-decay PWM: hold one input high, pulse the other between low (drive) and high
// (brake), so partial speeds keep their torque. 255 = fully on at 8 bits.
void motorDrive(int d, int pct) {
  int brakeDuty = 255 - constrain(pct, 0, 100) * 255 / 100;
  if (d > 0) { ledcWrite(PIN_M1, 255); ledcWrite(PIN_M2, brakeDuty); }
  else       { ledcWrite(PIN_M2, 255); ledcWrite(PIN_M1, brakeDuty); }
}
void motorBrake() { ledcWrite(PIN_M1, 255); ledcWrite(PIN_M2, 255); }

// Brightness in % is perceptual (gamma 2.2), so 10% looks like a tenth, not a third.
void ledOut(float pct) {
  static int last = -1;
  int duty = pct <= 0 ? 0 : max(1, (int)lroundf(LED_MAX * powf(min(pct, 100.0f) / 100.0f, 2.2f)));
  if (duty == last) return;
  last = duty;
  ledcWrite(PIN_L2, 0);
  ledcWrite(PIN_L1, duty);
}

void rgb(uint8_t r, uint8_t g, uint8_t b) {
  static uint32_t last = 0xFFFFFFFF;
  uint32_t c = (r << 16) | (g << 8) | b;
  if (c != last) { last = c; rgbLedWrite(PIN_RGB, r, g, b); }
}

// ---------------------------------------------------------------- output to the page
bool canPush() { return ws.count() > 0 && ws.availableForWriteAll(); }

void stream(const char *action, const char *target, const char *html) {
  String s;
  s.reserve(strlen(html) + 96);
  s += "<turbo-stream action=\""; s += action; s += "\" target=\""; s += target;
  s += "\"><template>"; s += html; s += "</template></turbo-stream>";
  ws.textAll(s);
}

// The log and results live here too (newest last), so a page that connects or reloads
// gets the whole history, and a reboot starts clean.
const int LOG_KEEP = 40, RES_KEEP = 30;
char logRing[LOG_KEEP][240], resRing[RES_KEEP][360];
int logCount = 0, resCount = 0;

void sendRing(const char *target, int count, int stride, const char *base) {
  String s;
  s.reserve(count * 120 + 96);
  for (int i = count - 1; i >= 0; i--) s += base + i * stride;   // newest first
  stream("update", target, s.c_str());
}

void report(const char *fmt, ...) {
  char msg[200], b[240];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);
  Serial.printf("# %s\n", msg);
  uint32_t t = millis() / 100;                              // tenths of a second since boot
  snprintf(b, sizeof b, "<li><span>%lu:%02lu.%lu</span> %s</li>", t / 600, t / 10 % 60, t % 10, msg);
  if (logCount == LOG_KEEP) { memmove(logRing[0], logRing[1], (LOG_KEEP - 1) * sizeof logRing[0]); logCount--; }
  strlcpy(logRing[logCount++], b, sizeof logRing[0]);
  if (canPush()) stream("prepend", "log", b);
}

void resultRow(const char *what, const char *result, const char *note) {
  Serial.printf("# RESULT %s | %s | %s\n", what, result, note);
  char b[360];
  snprintf(b, sizeof b, "<tr><td>%s</td><td>%s</td><td>%s</td></tr>", what, result, note);
  if (resCount == RES_KEEP) { memmove(resRing[0], resRing[1], (RES_KEEP - 1) * sizeof resRing[0]); resCount--; }
  strlcpy(resRing[resCount++], b, sizeof resRing[0]);
  if (canPush()) stream("prepend", "results", b);
}

// What a freshly connected page needs: version, history, and the inputs set to the
// values Freddy is actually using.
void sendPageState() {
  char b[700];
  snprintf(b, sizeof b, "v%s &middot; %s", FW_VERSION, FW_BUILD);
  stream("update", "fw", b);
  sendRing("log", logCount, sizeof logRing[0], logRing[0]);
  sendRing("results", resCount, sizeof resRing[0], resRing[0]);
  snprintf(b, sizeof b, "<input type=\"range\" name=\"v\" min=\"5\" max=\"100\" step=\"5\" value=\"%d\" "
           "oninput=\"this.nextElementSibling.value=this.value+'%%'\" onchange=\"this.form.requestSubmit()\"><output>%d%%</output>", speedPct, speedPct);
  stream("update", "f_speed", b);
  snprintf(b, sizeof b, "<input type=\"range\" name=\"v\" min=\"0\" max=\"100\" value=\"%d\" "
           "oninput=\"this.nextElementSibling.value=this.value+'%%'\" onchange=\"this.form.requestSubmit()\"><output>%d%%</output>", ledPct, ledPct);
  stream("update", "f_bright", b);
  snprintf(b, sizeof b, "<input type=\"number\" name=\"v\" min=\"200\" max=\"10000\" step=\"100\" value=\"%d\">", ledPeriod);
  stream("update", "f_period", b);
  snprintf(b, sizeof b, "<input type=\"number\" name=\"v\" min=\"0.3\" max=\"5\" step=\"0.1\" value=\"%.1f\">", tolDeg);
  stream("update", "f_tol", b);
  snprintf(b, sizeof b, "<input type=\"number\" name=\"v\" min=\"20\" max=\"100\" step=\"5\" value=\"%d\">", minSpeed);
  stream("update", "f_minspd", b);
}

// Live values: each card is sent only when its HTML changes, and at most every
// 100 ms (sys every 2 s). Values are rounded first so noise doesn't count as change.
enum { C_EYES, C_POS, C_SENS, C_LED, C_SYS, C_WIZ, C_ENC, C_HEAD, C_PATH, C_GOTO, N_CARDS };
const char *CARD_IDS[N_CARDS] = {"c_eyes", "c_pos", "c_sens", "c_led", "c_sys", "cal", "enc", "head", "path", "gotos"};
uint32_t cardHash[N_CARDS], cardSent[N_CARDS];

uint32_t fnv(const char *s) { uint32_t h = 2166136261u; while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; } return h; }

void pushCard(int i, const char *html, uint32_t now) {
  uint32_t h = fnv(html);
  if (h == cardHash[i]) return;                                // unchanged: send nothing
  if (now - cardSent[i] < (i == C_SYS ? 2000u : 100u)) return; // changed, but too soon: next pass
  if (!canPush()) return;                                      // no client, or its queue is full: retry later
  cardHash[i] = h;
  cardSent[i] = now;
  stream("update", CARD_IDS[i], html);
}

const char *magnetVerdict(const char **cls) {
  bool md = enc.status & 0x20, ml = enc.status & 0x10, mh = enc.status & 0x08;
  *cls = "ok";
  if (!enc.found) { *cls = "bad"; return "Sensor not found"; }
  if (!md && ml) { *cls = "bad"; return "Magnet too far / not seen"; }
  if (!md) { *cls = "bad"; return "No magnet"; }
  if (mh) { *cls = "warn"; return "Magnet too close"; }
  if (ml) { *cls = "warn"; return "Magnet a bit far"; }
  return "Magnet OK";
}

// ---------------------------------------------------------------- calibration panel
// Sets with center between them? (going from left to right through center, not the long way round)
bool calValid() {
  if (!(haveL && haveR && haveC)) return false;
  float lr = wrap180(rightDeg - leftDeg), lc = wrap180(centerDeg - leftDeg);
  return fabsf(lr) > 5 && (lc > 0) == (lr > 0) && fabsf(lc) < fabsf(lr);
}

static void calRow(String &h, const char *name, bool have, float deg, int which, float rel) {
  h += "<tr><th>"; h += name; h += "</th><td>";
  if (have) {
    h += String(deg, 1) + "&deg;";
    if (calibrated && which != 3) { h += " <span class=\"mute\">("; h += String(fabsf(rel), 0); h += "&deg; from center)</span>"; }
  } else h += "<span class=\"mute\">not set</span>";
  h += "</td><td><form action=\"/cmd\" method=\"post\"><input type=\"hidden\" name=\"a\" value=\"mark\">"
       "<button name=\"v\" value=\""; h += which; h += "\" class=\""; h += have ? "" : "primary"; h += "\">Set here</button></form></td></tr>";
}

void pushCalPanel(uint32_t now) {
  String h;
  h.reserve(1800);
  h += "<h3>Calibration</h3><table class=\"caltab\"><tr><th>Sensor now</th><td colspan=\"2\"><b>";
  h += magnetOk() ? String(roundf(angNow() * 2) / 2, 1) + "&deg;" : String("no reading");
  h += "</b></td></tr>";
  calRow(h, "Full left", haveL, leftDeg, 1, calibrated ? relOf(leftDeg) : 0);
  calRow(h, "Center", haveC, centerDeg, 3, 0);
  calRow(h, "Full right", haveR, rightDeg, 2, calibrated ? relOf(rightDeg) : 0);
  h += "</table><p>";
  if (calibrated) {
    char t[120];
    snprintf(t, sizeof t, "<b class=\"ok\">Calibrated.</b> Travel L %.0f&deg; / R %.0f&deg; (%.0f&deg; total).", rangeL(), rangeR(), rangeL() + rangeR());
    h += t;
  } else if (haveL && haveR && haveC) h += "<b class=\"bad\">Center isn't between full left and full right.</b> Check the three points.";
  else h += "Move the eyes to each position and press Set here. Each one is saved straight away.";
  h += "</p>";
  pushCard(C_WIZ, h.c_str(), now);
}

// ---------------------------------------------------------------- sensor details panel
void pushEncoder(uint32_t now) {
  char b[1800];
  if (!enc.found) {
    snprintf(b, sizeof b, "<div class=\"verdict bad\">Sensor not found</div><p>No answer at I2C address 0x36 on SDA GPIO%d / SCL GPIO%d. "
             "Check VCC (3V3), GND, SDA and SCL, and that SDA and SCL aren't swapped.</p>", PIN_SDA, PIN_SCL);
    pushCard(C_ENC, b, now);
    return;
  }
  const char *cls; const char *verdict = magnetVerdict(&cls);
  float deg = angNow();
  int degQ = (int)lroundf(deg * 2);
  float turned = enc.turnMax - enc.turnMin;
  char swing[200];
  if (enc.magMax >= enc.magMin && enc.magMax > 0) {
    float pct = 100.0f * (enc.magMax - enc.magMin) / max(1, (int)enc.magMax);
    snprintf(swing, sizeof swing, "%d&ndash;%d (%.0f%% swing over %.0f&deg; turned)", enc.magMin, enc.magMax, pct, turned);
  } else snprintf(swing, sizeof swing, "&ndash;");
  float nx = 50 + 40 * sinf(deg * PI / 180), ny = 50 - 40 * cosf(deg * PI / 180);
  snprintf(b, sizeof b,
           "<div class=\"enc\"><svg viewBox=\"0 0 100 100\" class=\"dial\"><circle cx=\"50\" cy=\"50\" r=\"46\"/>"
           "<line x1=\"50\" y1=\"50\" x2=\"%.1f\" y2=\"%.1f\"/></svg><div>"
           "<div class=\"verdict %s\">%s</div>"
           "<table><tr><th>Angle</th><td>%.1f&deg; (raw %d)</td></tr>"
           "<tr><th>Field strength</th><td>%d &middot; gain %d</td></tr>"
           "<tr><th>Field swing</th><td>%s</td></tr>"
           "<tr><th>Noise</th><td>%.2f&deg; peak-to-peak over 1 s</td></tr></table></div></div>",
           nx, ny, cls, verdict, degQ / 2.0f, enc.raw / 8 * 8, enc.mag / 16 * 16, enc.agc, swing, encNoise());
  pushCard(C_ENC, b, now);
}

// ---------------------------------------------------------------- head diagram
// Freddy seen from above, facing up the page, so screen right is Freddy's own right.
// rel angle (degrees, + = right) -> direction (sin, -cos).
static void ray(String &h, float x0, float y0, float deg, float len, const char *cls) {
  float a = deg * PI / 180;
  h += "<line class=\""; h += cls; h += "\" x1=\""; h += String(x0, 1); h += "\" y1=\""; h += String(y0, 1);
  h += "\" x2=\""; h += String(x0 + len * sinf(a), 1); h += "\" y2=\""; h += String(y0 - len * cosf(a), 1); h += "\"/>";
}
static void wedge(String &h, float from, float to, float rad, const char *cls) {   // pie slice from the eyes' midpoint
  const float cx = 0, cy = -22;
  float a0 = from * PI / 180, a1 = to * PI / 180;
  h += "<path class=\""; h += cls; h += "\" d=\"M"; h += String(cx, 1); h += ","; h += String(cy, 1);
  h += " L"; h += String(cx + rad * sinf(a0), 1); h += ","; h += String(cy - rad * cosf(a0), 1);
  h += " A"; h += String(rad, 0); h += ","; h += String(rad, 0); h += " 0 "; h += (to - from > 180) ? "1" : "0"; h += " 1 ";
  h += String(cx + rad * sinf(a1), 1); h += ","; h += String(cy - rad * cosf(a1), 1); h += " Z\"/>";
}

void pushHead(uint32_t now) {
  String h;
  h.reserve(2400);
  // What do we know? Draw relative to center if it's set; otherwise relative to the middle
  // of whatever ends are set (or the sweep hint), so the eyes are visibly moving either way.
  bool haveSides = haveL && haveR;
  float ref = haveC ? centerDeg
            : haveSides ? norm360(leftDeg + wrap180(rightDeg - leftDeg) / 2)
            : hintSwept ? norm360(hintB + wrap180(hintA - hintB) / 2)
            : 0;
  bool haveMid = haveC || haveSides || hintSwept;
  float sign = haveSides ? rightSign() : 1;
  bool reading = magnetOk();
  float rel = reading ? wrap180(angNow() - ref) * sign : 0;
  rel = roundf(rel * 2) / 2;                                 // half-degree steps: smooth, but not chatty
  auto relTo = [&](float sensorDeg) { return wrap180(sensorDeg - ref) * sign; };

  h += "<svg viewBox=\"-118 -126 236 206\" class=\"headsvg\">";
  // range: full travel (when both ends are set), and the usable part in symmetric mode
  if (haveSides) {
    float l = relTo(leftDeg), rr = relTo(rightDeg);
    wedge(h, min(l, rr), max(l, rr), 92, "range");
    if (symmetric && calibrated) wedge(h, -effL(), effR(), 92, "usable");
  }
  if (haveL) ray(h, 0, -22, relTo(leftDeg), 96, "edge");
  if (haveR) ray(h, 0, -22, relTo(rightDeg), 96, "edge");
  if (hintSwept) { ray(h, 0, -22, relTo(hintA), 104, "hint"); ray(h, 0, -22, relTo(hintB), 104, "hint"); }
  if (haveC) ray(h, 0, -22, 0, 100, "center");
  // head: ears, face, snout, eyes
  h += "<circle class=\"ear\" cx=\"-40\" cy=\"-30\" r=\"15\"/><circle class=\"ear\" cx=\"40\" cy=\"-30\" r=\"15\"/>"
       "<circle class=\"skull\" cx=\"0\" cy=\"8\" r=\"50\"/><ellipse class=\"snout\" cx=\"0\" cy=\"-38\" rx=\"17\" ry=\"11\"/>"
       "<ellipse class=\"nose\" cx=\"0\" cy=\"-46\" rx=\"6\" ry=\"4\"/>"
       "<circle class=\"eye\" cx=\"-17\" cy=\"-22\" r=\"6\"/><circle class=\"eye\" cx=\"17\" cy=\"-22\" r=\"6\"/>";
  // target while a go-to runs
  if (mv.type == MV_SEEK && haveMid) ray(h, 0, -22, roundf(wrap180(sk.target - ref) * sign * 2) / 2, 100, "target");
  // gaze
  if (reading && haveMid) { ray(h, -17, -22, rel, 78, "gaze"); ray(h, 17, -22, rel, 78, "gaze"); }
  // labels
  h += "<text x=\"-104\" y=\"72\" class=\"lbl\">L</text><text x=\"98\" y=\"72\" class=\"lbl\">R</text>";
  h += "<text x=\"0\" y=\"74\" class=\"read\">";
  if (!reading) h += "no angle reading";
  else if (!haveMid) h += "set a point to see the eyes here";
  else {
    char t[64];
    if (haveC && fabsf(rel) < 0.5f) snprintf(t, sizeof t, "dead center");
    else snprintf(t, sizeof t, "%.1f&deg; %s%s", fabsf(rel), haveSides ? (rel > 0 ? "right" : "left") : (rel > 0 ? "+" : "&minus;"),
                  haveC ? "" : " from middle");
    h += t;
  }
  h += "</text></svg>";
  pushCard(C_HEAD, h.c_str(), now);
}

// ---------------------------------------------------------------- path panel
// Only the chosen mode's controls are shown.
void pushPath(uint32_t now) {
  static const char *DESC[] = {
    "The disc reverses within the segment, whichever way gets the eyes there first.",
    "The disc only turns forward, like the stock board: sometimes the long way round, over an extreme.",
    "At the ends of travel either disc direction works, via different stretches of the disc: pick which one to use for moving right and for moving left. Elsewhere it goes the direct way and never rides over an end."};
  String h;
  h.reserve(1400);
  h += "<form action=\"/cmd\" method=\"post\" class=\"seg\"><input type=\"hidden\" name=\"a\" value=\"path\"><label>Path</label>";
  for (int m = 0; m < 3; m++) {
    h += "<button name=\"v\" value=\""; h += m; h += "\" class=\""; h += m == pathMode ? "primary" : ""; h += "\">";
    h += m == 0 ? "Shortest" : m == 1 ? "Forward only" : "Preferred per direction"; h += "</button>";
  }
  h += "</form><p class=\"hint\">"; h += DESC[pathMode]; h += "</p>";
  if (pathMode == 2) {
    for (int side = 0; side < 2; side++) {                   // 0 = moving right, 1 = moving left
      int cur = side == 0 ? prefRight : prefLeft;
      h += "<form action=\"/cmd\" method=\"post\" class=\"seg\"><input type=\"hidden\" name=\"a\" value=\"pref\"><label>";
      h += side == 0 ? "Moving right" : "Moving left"; h += "</label>";
      for (int d = 0; d < 2; d++) {                          // 0 = forward, 1 = reverse
        int code = side * 2 + d + 1;                         // 1 right-fwd, 2 right-rev, 3 left-fwd, 4 left-rev
        bool on = (d == 0) == (cur > 0);
        h += "<button name=\"v\" value=\""; h += code; h += "\" class=\""; h += on ? "primary" : ""; h += "\">";
        h += d == 0 ? "Disc forward" : "Disc reverse"; h += "</button>";
      }
      h += "</form>";
    }
    if (!calibrated) h += "<p class=\"hint\"><b class=\"bad\">Needs full left, center and full right set</b> to know which way is right.</p>";
  }
  pushCard(C_PATH, h.c_str(), now);
}

void pushCards(uint32_t now) {
  char b[900];
  const char *mvName[] = {"stopped", "running", "pulse", "pausing", "moving"};

  if (mv.type == MV_SEEK) snprintf(b, sizeof b, "<div class=\"k\">Eyes</div><div class=\"v\">%s</div><div class=\"s\">%s &middot; %d%% %s</div>",
                                   job != J_NONE ? JOB_NAMES[job] : "moving", sk.name, sk.pct, sk.dir > 0 ? "&rarr;" : "&larr;");
  else snprintf(b, sizeof b, "<div class=\"k\">Eyes</div><div class=\"v\">%s%s</div><div class=\"s\">tolerance %.1f&deg; &middot; min speed %d%%</div>",
                job != J_NONE ? JOB_NAMES[job] : mvName[mv.type], motorOn() ? (mv.dir > 0 ? " &rarr;" : " &larr;") : "", tolDeg, minSpeed);
  pushCard(C_EYES, b, now);

  if (calibrated && magnetOk()) {
    float rel = relOf(angNow());
    int p = (int)lroundf(pctOfRel(rel));
    int bar = constrain(50 + p / 2, 0, 100);
    char val[24];
    if (abs(p) > 102) snprintf(val, sizeof val, "far %s", p > 0 ? "R" : "L");
    else snprintf(val, sizeof val, "%s%d%%", p > 0 ? "R " : p < 0 ? "L " : "", min(abs(p), 100));
    char mode[48];
    if (symmetric) snprintf(mode, sizeof mode, "symmetric &plusmn;%.0f&deg;", effR());
    else snprintf(mode, sizeof mode, "L %.0f&deg; / R %.0f&deg;", rangeL(), rangeR());
    snprintf(b, sizeof b, "<div class=\"k\">Eye position</div><div class=\"v\">%s</div><div class=\"s\">%+.1f&deg; from center &middot; %s</div>"
             "<div class=\"bar\"><b class=\"l\" style=\"left:0%%\"></b><b class=\"c\" style=\"left:50%%\"></b><b class=\"r\" style=\"left:100%%\"></b>"
             "<i style=\"left:%d%%\"></i></div>", val, rel, mode, bar);
  } else if (!magnetOk()) {
    snprintf(b, sizeof b, "<div class=\"k\">Eye position</div><div class=\"v\">?</div><div class=\"s\">no angle reading: see Sensor details</div>");
  } else {
    snprintf(b, sizeof b, "<div class=\"k\">Eye position</div><div class=\"v\">%.1f&deg;</div><div class=\"s\">sensor angle &middot; not calibrated</div>",
             (int)lroundf(angNow() * 2) / 2.0f);
  }
  pushCard(C_POS, b, now);

  const char *cls; const char *v = magnetVerdict(&cls);
  snprintf(b, sizeof b, "<div class=\"k\">Angle sensor</div><div class=\"v %s\">%s</div><div class=\"s\">%s</div>", cls,
           magnetOk() ? (String(angNow(), 1) + "&deg;").c_str() : "&ndash;", v);
  pushCard(C_SENS, b, now);

  if (ledStep >= 0) snprintf(b, sizeof b, "<div class=\"k\">LEDs</div><div class=\"v\">steps</div><div class=\"s\">now %d%%</div>", LED_STEPS[ledStep]);
  else snprintf(b, sizeof b, "<div class=\"k\">LEDs</div><div class=\"v\">%s</div><div class=\"s\">%d%%%s</div>", LED_MODES[ledMode], ledPct,
                (ledMode == L_BREATHE || ledMode == L_BLINK) ? (String(" &middot; ") + ledPeriod + " ms").c_str() : "");
  pushCard(C_LED, b, now);

  static int shownRssi = 0;                                  // RSSI wobbles: only move it 6 dB at a time
  int rssi = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
  if (abs(rssi - shownRssi) >= 6) shownRssi = rssi;
  snprintf(b, sizeof b, "<div class=\"k\">System</div><div class=\"v\">%s</div><div class=\"s\">v%s &middot; heap %luk &middot; reset %d</div>",
           shownRssi ? (String(shownRssi) + " dBm").c_str() : "AP mode", FW_VERSION, ESP.getFreeHeap() / 10240 * 10, resetReason);
  pushCard(C_SYS, b, now);

  pushCalPanel(now);
  pushEncoder(now);
  pushHead(now);
  pushPath(now);
  {                                                         // go-to buttons: Far only differs from Full in symmetric mode
    String h = "<form action=\"/cmd\" method=\"post\"><input type=\"hidden\" name=\"a\" value=\"goto\">"
               "<button name=\"v\" value=\"-100\">Full left</button><button name=\"v\" value=\"-50\">Left</button>"
               "<button name=\"v\" value=\"0\" class=\"primary\">Center</button>"
               "<button name=\"v\" value=\"50\">Right</button><button name=\"v\" value=\"100\">Full right</button></form>";
    if (symmetric)
      h += "<form action=\"/cmd\" method=\"post\"><input type=\"hidden\" name=\"a\" value=\"goto\"><input type=\"hidden\" name=\"t\" value=\"1\">"
           "<button name=\"v\" value=\"-100\">Far left</button><button name=\"v\" value=\"100\">Far right</button></form>";
    pushCard(C_GOTO, h.c_str(), now);
  }
}

// ---------------------------------------------------------------- settings in flash
void saveCal() {
  prefs.putBool("hl", haveL); prefs.putBool("hr", haveR); prefs.putBool("hc", haveC);
  prefs.putFloat("r", rightDeg);
  prefs.putFloat("l", leftDeg);
  prefs.putFloat("c", centerDeg);
}
void saveTuning() {
  prefs.putFloat("tol", tolDeg); prefs.putInt("minspd", minSpeed); prefs.putBool("sym", symmetric);
  prefs.putInt("path", pathMode); prefs.putInt("pr", prefRight); prefs.putInt("pl", prefLeft);
}

// ---------------------------------------------------------------- moves
void startMove(MoveType t, int dir, int pct, uint32_t ms) {
  mv.type = t; mv.dir = dir; mv.pct = pct; mv.ms = ms; mv.start = millis();
  if (t == MV_PAUSE) { motorBrake(); return; }
  motorDrive(dir, pct);
}

void endMove(bool ok, const char *why) {
  mv.type = MV_NONE;
  motorBrake();
  if (job != J_NONE) jobStep(ok);
  else if (why) report("%s", why);
}

void stopAll(const char *why) {
  bool busy = mv.type != MV_NONE || job != J_NONE;
  job = J_NONE;
  mv.type = MV_NONE;
  motorBrake();
  if (busy && why) report("STOP (%s)", why);
}

// ---------------------------------------------------------------- closed-loop seek
// Drive the cam disc until the sensor reads target (degrees, 0..360). Direction is found by
// watching the angle: if the eyes move away from the target, reverse.
void seekTo(float target, const char *name, float tol = -1, bool gentle = false) {
  if (!magnetOk()) { report("go to %s: no angle reading", name); if (job != J_NONE) { job = J_NONE; } return; }
  sk.target = norm360(target);
  sk.name = name;
  sk.tol = tol > 0 ? tol : tolDeg;
  sk.gentle = gentle;
  sk.mode = gentle ? 0 : pathMode;
  float err = wrap180(sk.target - angNow());
  if (fabsf(err) <= sk.tol) { report("go to %s: already there (%+.1f&deg;)", name, err); if (job != J_NONE) jobStep(true); return; }
  // start the way that moved the eyes toward the target last time (forward if unknown)
  if (sk.mode == 1) sk.dir = 1;
  else if (sk.mode == 2 && calibrated) {
    // At an end of travel both disc directions bring the eyes back, via different stretches of
    // the disc: that's where the preference applies. Anywhere else, go the direct way (it never
    // rides over an end), like shortest.
    float relNow = relOf(angNow());
    bool atEnd = relNow > rangeR() - END_ZONE || relNow < -rangeL() + END_ZONE;
    if (atEnd || sk.fwdSign == 0) sk.dir = (relOf(sk.target) > relNow) ? prefRight : prefLeft;
    else sk.dir = (err > 0) == (sk.fwdSign > 0) ? 1 : -1;
  }
  else sk.dir = sk.fwdSign == 0 ? 1 : ((err > 0) == (sk.fwdSign > 0) ? 1 : -1);
  sk.approaching = true;
  sk.flips = 0; sk.settles = 0; sk.settleAt = 0; sk.kickUntil = 0;
  sk.checkAt = millis(); sk.angAtCheck = angNow();
  sk.pct = 100;
  startMove(MV_SEEK, sk.dir, 100, sk.mode == 0 ? 4000 : 7000);   // fixed directions may go most of a turn
}

void seekStep(uint32_t now) {
  if (!magnetOk()) { endMove(false, "go to: lost the angle reading"); return; }
  float ang = angNow();
  float err = wrap180(sk.target - ang);
  if (sk.settleAt) {                                         // braked: wait for the coast to finish, then check
    if (since(sk.settleAt, now) < 150) return;
    sk.settleAt = 0;
    if (fabsf(err) <= sk.tol) {
      mv.type = MV_NONE;
      uint32_t took = since(mv.start, now);
      if (job == J_NONE) report("go to %s: done, %+.1f&deg; off, %lu ms, %d reversal%s", sk.name, err, took, sk.flips, sk.flips == 1 ? "" : "s");
      else { report("%s: %+.1f&deg; off, %lu ms, %d reversal%s", sk.name, err, took, sk.flips, sk.flips == 1 ? "" : "s"); jobStep(true); }
      return;
    }
    if (++sk.settles > 4) {
      if (sk.gentle) { mv.type = MV_NONE; report("%s: close enough (%+.1f&deg;)", sk.name, err); return; }
      endMove(false, "go to: overshoots and won't settle. Raise the tolerance or lower min speed."); return;
    }
    sk.checkAt = now; sk.angAtCheck = ang;                   // coasted past: correct
  }
  if (fabsf(err) <= sk.tol) { motorBrake(); sk.settleAt = now; return; }
  if (since(mv.start, now) > mv.ms) { endMove(false, "go to: timed out. Is the motor powered?"); return; }

  // every 40 ms: is the angle moving toward the target? If not, reverse (shortest path, or
  // a fixed-direction move that's within FINE_ZONE: a small correction). Barely moving: kick.
  if (since(sk.checkAt, now) >= 40) {
    float moved = wrap180(ang - sk.angAtCheck);
    if (fabsf(moved) > 0.15f) {
      sk.fwdSign = ((moved > 0) == (sk.dir > 0)) ? 1 : -1;   // learn the cam disc's current direction
      sk.approaching = (moved > 0) == (err > 0);
      bool mayReverse = sk.mode != 1 || fabsf(err) < FINE_ZONE;   // only forward-only commits to a direction
      if (!sk.approaching && mayReverse) { sk.dir = -sk.dir; sk.flips++; }
    } else if (now > sk.kickUntil) sk.kickUntil = now + 150;  // stalled at low speed: full power briefly
    sk.checkAt = now; sk.angAtCheck = ang;
    if (sk.flips > (sk.gentle ? 4 : 16)) {
      if (sk.gentle) { motorBrake(); mv.type = MV_NONE; report("%s: close enough (%+.1f&deg;)", sk.name, wrap180(sk.target - ang)); return; }
      endMove(false, "go to: keeps reversing. Check the calibration."); return;
    }
  }
  float a = fabsf(err);
  int pct = a >= SLOW_ZONE ? 100 : minSpeed + (int)((100 - minSpeed) * max(0.0f, a - sk.tol) / (SLOW_ZONE - sk.tol));
  if (now < sk.kickUntil) pct = 100;
  if (sk.mode == 1 && !sk.approaching) pct = 100;            // forward only, heading over an end: no need to creep
  sk.pct = constrain(pct, minSpeed, 100);
  motorDrive(sk.dir, sk.pct);
}

void gotoPct(float pct, const char *name, bool far = false) {
  if (!calibrated) { report("go to: calibrate first"); job = J_NONE; return; }
  seekTo(sensorOfPct(pct, far), name);
}

// ---------------------------------------------------------------- jobs
void startJob(Job j) {
  stopAll(nullptr);
  job = j; jStep = 0;
  jobStep(true);
}

void jobStep(bool ok) {
  if (!ok && job != J_SWEEP) { job = J_NONE; return; }
  switch (job) {
    case J_SWEEP:                                            // run a bit over one turn, watching the extremes
      if (jStep == 0) {
        if (!magnetOk()) { job = J_NONE; report("sweep: no angle reading"); break; }
        encResetStats();
        sweepStartDeg = angNow();
        jStep = 1;
        report("sweep: running %d ms", TURN_MS * 13 / 10);
        startMove(MV_TIMED, 1, 100, TURN_MS * 13 / 10);
      } else {
        job = J_NONE;
        float range = enc.turnMax - enc.turnMin;
        if (range < 20 || range > 200) {
          report("sweep: only %.0f&deg; of travel seen. Did the eyes move? Is the magnet on the pivot?", range);
          break;
        }
        hintA = norm360(sweepStartDeg + enc.turnMax);
        hintB = norm360(sweepStartDeg + enc.turnMin);
        hintSwept = true;
        char b1[64], b2[96];
        snprintf(b1, sizeof b1, "%.0f&deg; of eye travel", range);
        snprintf(b2, sizeof b2, "extremes at %.1f&deg; and %.1f&deg; on the sensor (dotted on the diagram)", hintA, hintB);
        resultRow("range sweep", b1, b2);
      }
      break;
    case J_TOUR: {                                           // left, center, right, center with pauses
      static const float PCTS[] = {-100, 0, 100, 0};
      static const char *NAMES[] = {"full left", "center", "full right", "center"};
      if (jStep >= 8) { job = J_NONE; report("tour done"); break; }
      int i = jStep / 2;
      jStep++;
      if (jStep % 2 == 1) gotoPct(PCTS[i], NAMES[i]);
      else startMove(MV_PAUSE, 1, 0, 1000);
      break;
    }
    case J_DART: {                                           // glance left and right, back to center
      static const float PCTS[] = {-60, 60, 0};
      static const char *NAMES[] = {"glance left", "glance right", "center"};
      if (jStep >= 6) { job = J_NONE; break; }
      int i = jStep / 2;
      jStep++;
      if (jStep % 2 == 1) gotoPct(PCTS[i], NAMES[i]);
      else startMove(MV_PAUSE, 1, 0, 400);
      break;
    }
    default: break;
  }
}

// ---------------------------------------------------------------- manual calibration
void markPoint(int which) {                                // 1 full left, 2 full right, 3 center
  if (!magnetOk()) { report("set: no angle reading"); return; }
  stopAll(nullptr);
  float a = angNow();
  const char *name = which == 1 ? "full left" : which == 2 ? "full right" : "center";
  if (which == 1) { leftDeg = a; haveL = true; }
  if (which == 2) { rightDeg = a; haveR = true; }
  if (which == 3) { centerDeg = a; haveC = true; }
  calibrated = calValid();
  saveCal();
  char b1[48]; snprintf(b1, sizeof b1, "%.1f&deg; on the sensor", a);
  char b2[96];
  if (calibrated) snprintf(b2, sizeof b2, "travel L %.0f&deg; / R %.0f&deg;", rangeL(), rangeR());
  else snprintf(b2, sizeof b2, "%s", haveL && haveR && haveC ? "center isn't between the ends" : "set the other points too");
  char t[40]; snprintf(t, sizeof t, "set %s", name);
  resultRow(t, b1, b2);
}

// Nudge the eyes by tenths of a degree: + is right once both ends are set, sensor + before that.
void nudgeEyes(int tenths) {
  if (!magnetOk()) { report("nudge: no angle reading"); return; }
  stopAll(nullptr);
  float s = (haveL && haveR) ? rightSign() : 1;
  static char name[24];
  snprintf(name, sizeof name, "nudge %+.1f&deg;", tenths / 10.0f);
  seekTo(norm360(angNow() + tenths / 10.0f * s), name, min(tolDeg, 0.5f), true);
}

// ---------------------------------------------------------------- commands
void status() {
  report("sensor %s %.1f&deg; | %s | L %.1f C %.1f R %.1f | %s | tol %.1f | min speed %d | led %s %d%%",
         magnetOk() ? "ok" : "no reading", angNow(), calibrated ? "calibrated" : "not calibrated",
         leftDeg, centerDeg, rightDeg, symmetric ? "symmetric" : "full range", tolDeg, minSpeed, LED_MODES[ledMode], ledPct);
}

void runCmd(char a, int v, int t) {
  switch (a) {
    case 'x': stopAll("stop pressed"); break;
    case 'f': case 'r':
      stopAll(nullptr);
      startMove(MV_RUN, a == 'f' ? 1 : -1, speedPct, RUN_LIMIT_MS);
      report("%s at %d%% (stops by itself after 30 s)", a == 'f' ? "forward" : "reverse", speedPct);
      break;
    case 's':
      speedPct = constrain((v + 2) / 5 * 5, 5, 100);
      if (mv.type == MV_RUN && job == J_NONE) startMove(MV_RUN, mv.dir, speedPct, RUN_LIMIT_MS);
      break;
    case 'j': stopAll(nullptr); startMove(MV_TIMED, t < 0 ? -1 : 1, 100, constrain(abs(v), 10, 2000)); break;
    case 'G': {                                              // go to: v = percent, -100..100
      stopAll(nullptr);
      bool far = t == 1;                                     // far: the true extremes, ignoring symmetric mode
      char name[24];
      if (far) snprintf(name, sizeof name, "far %s", v > 0 ? "right" : "left");
      else snprintf(name, sizeof name, v == 0 ? "center" : "%s %d%%", v > 0 ? "right" : "left", abs(v));
      static char nameKeep[24]; strlcpy(nameKeep, name, sizeof nameKeep);
      gotoPct(v, nameKeep, far);
      break;
    }
    case 'd': if (calibrated) startJob(J_DART); else report("calibrate first"); break;
    case 'T': if (calibrated) startJob(J_TOUR); else report("calibrate first"); break;
    case 'O': tolDeg = constrain(v / 10.0f, 0.3f, 5.0f); saveTuning(); report("tolerance %.1f&deg;", tolDeg); break;
    case 'S': minSpeed = constrain(v, 20, 100); saveTuning(); report("min speed %d%%", minSpeed); break;
    case 'Q':
      symmetric = v != 0; saveTuning();
      if (calibrated) report(symmetric ? "symmetric: both sides &plusmn;%.0f&deg;" : "full range: L %.0f&deg; / R %.0f&deg;",
                             symmetric ? effR() : rangeL(), rangeR());
      else report(symmetric ? "symmetric mode on" : "full range mode on");
      break;
    case 'k': if (v >= 1 && v <= 3) markPoint(v); break;
    case 'K':
      stopAll(nullptr);
      haveL = haveR = haveC = calibrated = false; saveCal();
      report("calibration cleared");
      break;
    case 'e': nudgeEyes(v); break;
    case 'R': startJob(J_SWEEP); break;
    case 'h': pathMode = constrain(v, 0, 2); saveTuning(); report("path: %s", PATH_NAMES[pathMode]); break;
    case 'u':
      if (v == 1 || v == 2) prefRight = v == 1 ? 1 : -1;
      if (v == 3 || v == 4) prefLeft = v == 3 ? 1 : -1;
      saveTuning();
      report("moving right: disc %s, moving left: disc %s", prefRight > 0 ? "forward" : "reverse", prefLeft > 0 ? "forward" : "reverse");
      break;
    case 'Y': encResetStats(); report("sensor stats reset: turn the magnet (or run the cam disc) a full circle"); break;
    case 'E':
      ledStep = 0; ledStepAt = millis();
      report("LED steps: 100, 50, 25, 12, 6, 3, 2, 1%%, 2.5 s each. Note where they vanish.");
      report("LED step: %d%%", LED_STEPS[0]);
      break;
    case 'b': ledPct = constrain(v, 0, 100); if (ledMode == L_OFF && ledPct) ledMode = L_ON; ledStep = -1; break;
    case 'M': ledMode = (LedMode)constrain(v, 0, 4); if (ledMode != L_OFF && ledPct == 0) ledPct = 60; ledStep = -1; break;
    case 'P': ledPeriod = constrain(v, 200, 10000); break;
    case 'W': memset(cardHash, 0, sizeof cardHash); sendPageState(); break;   // new page: resend everything
    case 'Z': logCount = resCount = 0; if (canPush()) { stream("update", "log", ""); stream("update", "results", ""); } break;
    case '?': status(); break;
  }
}

// ---------------------------------------------------------------- web + WiFi
void setupWeb() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *r) {
    // Reached by name (freddy.local)? Send the browser to the IP: .local lookups can stall
    // for ~5 s at random, and after this one the page and its requests skip them.
    String ip = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
    String host = r->host();
    if (host.length() && host != ip && !host.startsWith(ip + ":")) {
      r->redirect(("http://" + ip + "/").c_str());
      return;
    }
    r->send(200, "text/html", (const uint8_t *)PAGE_HTML, strlen(PAGE_HTML));
  });
  server.on("/cmd", HTTP_POST, [](AsyncWebServerRequest *r) {
    String a = r->hasParam("a", true) ? r->getParam("a", true)->value() : "";
    String vs = r->hasParam("v", true) ? r->getParam("v", true)->value() : "0";
    int v = vs.toInt();
    int t = r->hasParam("t", true) ? r->getParam("t", true)->value().toInt() : 0;
    static const struct { const char *name; char code; } MAP[] = {
      {"stop", 'x'}, {"fwd", 'f'}, {"rev", 'r'}, {"speed", 's'}, {"jogf", 'j'}, {"jogr", 'j'},
      {"goto", 'G'}, {"dart", 'd'}, {"tour", 'T'}, {"tol", 'O'}, {"minspd", 'S'}, {"sym", 'Q'}, {"mark", 'k'}, {"clearcal", 'K'}, {"nudge", 'e'}, {"range", 'R'}, {"path", 'h'}, {"pref", 'u'},
      {"encreset", 'Y'}, {"ledsteps", 'E'}, {"bright", 'b'}, {"ledmode", 'M'}, {"period", 'P'},
      {"clearlog", 'Z'}};
    Cmd c = {0, v, t};
    for (auto &m : MAP) if (a == m.name) c.a = m.code;
    if (a == "jogr") c.t = -1;
    if (a == "jogf") c.t = 1;
    if (a == "tol") c.v = (int)lroundf(vs.toFloat() * 10);   // tenths of a degree
    if (c.a) xQueueSend(cmdQueue, &c, 0);
    // Live state comes back over the WebSocket; the reply only clears or sets the message line.
    String body = "<turbo-stream action=\"update\" target=\"msg\"><template>" +
                  (c.a ? String("") : String("Unknown command: ") + a) + "</template></turbo-stream>";
    r->send(200, "text/vnd.turbo-stream.html", body);
  });
  ws.onEvent([](AsyncWebSocket *s, AsyncWebSocketClient *client, AwsEventType type, void *, uint8_t *, size_t) {
    if (type == WS_EVT_CONNECT) { Cmd c = {'W', 0, 0}; xQueueSend(cmdQueue, &c, 0); }
  });
  server.addHandler(&ws);
  server.onNotFound([](AsyncWebServerRequest *r) { r->send(404, "text/plain", "not found"); });
  server.begin();
}

void setupWifi() {
  WiFi.setHostname(HOSTNAME);
  bool joined = false;
#ifdef HAVE_WIFI_SECRETS
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("# joining WiFi \"%s\"", WIFI_SSID);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) { delay(250); Serial.print("."); }
  Serial.println();
  joined = WiFi.status() == WL_CONNECTED;
#endif
  if (joined) {
    WiFi.setSleep(false);
    Serial.printf("# WiFi connected: http://%s.local  or  http://%s\n", HOSTNAME, WiFi.localIP().toString().c_str());
  } else {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.printf("# access point \"%s\" (password %s): http://%s\n", AP_SSID, AP_PASS, WiFi.softAPIP().toString().c_str());
  }
  if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);
}

// ---------------------------------------------------------------- setup / loop
void setup() {
  ledcAttach(PIN_M1, 20000, 8);
  ledcAttach(PIN_M2, 20000, 8);
  ledcAttach(PIN_L1, 20000, LED_BITS);
  ledcAttach(PIN_L2, 20000, LED_BITS);
  motorBrake();
  ledOut(0);
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);
  cmdQueue = xQueueCreate(8, sizeof(Cmd));
  resetReason = (int)esp_reset_reason();

  prefs.begin("headtest3", false);
  bool oldCal = prefs.getBool("cal", false);               // v3 saved one flag for all three points
  haveL = prefs.getBool("hl", oldCal);
  haveR = prefs.getBool("hr", oldCal);
  haveC = prefs.getBool("hc", oldCal);
  rightDeg = prefs.getFloat("r", 0);
  leftDeg = prefs.getFloat("l", 0);
  centerDeg = prefs.getFloat("c", 0);
  calibrated = calValid();
  tolDeg = prefs.getFloat("tol", 1.0f);
  minSpeed = prefs.getInt("minspd", 50);
  symmetric = prefs.getBool("sym", false);
  pathMode = constrain(prefs.getInt("path", 2), 0, 2);
  prefRight = prefs.getInt("pr", -1) < 0 ? -1 : 1;
  prefLeft = prefs.getInt("pl", 1) < 0 ? -1 : 1;

  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  Wire.setTimeOut(5);

  delay(1500);
  Serial.printf("\n# freddy_head_web v%s (%s) | reset reason %d | pins: %s\n", FW_VERSION, FW_BUILD, resetReason, USE_PCB_PINS ? "PCB" : "breadboard");
  setupWifi();
  setupWeb();
  // Watchdog: if loop() hangs, reset. A reset releases the motor pins.
  esp_task_wdt_config_t wdt = {.timeout_ms = 2000, .idle_core_mask = 0, .trigger_panic = true};
  if (esp_task_wdt_reconfigure(&wdt) != ESP_OK) esp_task_wdt_init(&wdt);
  esp_task_wdt_add(NULL);
  report("booted v%s. %s", FW_VERSION, calibrated ? "Calibration loaded." : "Not calibrated: set full left, center and full right.");
}

void loop() {
  esp_task_wdt_reset();
  Cmd c;
  while (xQueueReceive(cmdQueue, &c, 0) == pdTRUE) runCmd(c.a, c.v, c.t);
  while (Serial.available()) {
    char k = Serial.read();
    if (k == ' ' || k == 'x') runCmd('x', 0, 0);
    else if (k == '?') runCmd('?', 0, 0);
  }
  uint32_t now = millis();
  encPoll(now);

  // moves
  uint32_t in = since(mv.start, now);
  switch (mv.type) {
    case MV_TIMED: case MV_PAUSE: if (in >= mv.ms) endMove(true, nullptr); break;
    case MV_RUN: if (in >= mv.ms) endMove(true, "30 s limit: stopped"); break;
    case MV_SEEK: seekStep(now); break;
    case MV_NONE: break;
  }

  // LEDs
  if (ledStep >= 0) {
    if (now - ledStepAt >= 2500) {
      ledStepAt = now;
      if (++ledStep >= (int)(sizeof LED_STEPS / sizeof LED_STEPS[0])) { ledStep = -1; report("LED steps done"); }
      else report("LED step: %d%%", LED_STEPS[ledStep]);
    }
    if (ledStep >= 0) ledOut(LED_STEPS[ledStep]);
  }
  if (ledStep < 0) {
    switch (ledMode) {
      case L_OFF: ledOut(0); break;
      case L_ON: ledOut(ledPct); break;
      case L_BREATHE: ledOut(ledPct * (0.5f - 0.5f * cosf((now % ledPeriod) * 2 * PI / ledPeriod))); break;
      case L_BLINK: ledOut((now % ledPeriod) < (uint32_t)ledPeriod / 2 ? ledPct : 0); break;
      case L_FLICKER: {
        static uint32_t nextFlip = 0; static bool on = false;
        if (now >= nextFlip) { on = !on; nextFlip = now + (on ? random(20, 120) : random(20, 200)); }
        ledOut(on ? ledPct : 0);
        break;
      }
    }
  }

  // status LED: blue = moving, green = calibrated with a good reading, amber = otherwise
  if (motorOn()) rgb(0, 0, 40); else if (calibrated && magnetOk()) rgb(0, 20, 0); else rgb(30, 15, 0);

  static uint32_t lastCheck = 0;
  if (now - lastCheck >= 50) { lastCheck = now; pushCards(now); }
  static uint32_t lastClean = 0;
  if (now - lastClean >= 1000) { lastClean = now; ws.cleanupClients(); }
}
