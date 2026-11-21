/*
  freddy_head_web: head bench test with a web dashboard

  Everything freddy_head_test does (eye motor speed, reverse, aim, glances; LED
  dimming, breathing, flicker), controlled from a browser, plus diagnostics:

    Measure       time 3 sweeps at the current speed (calibrates aiming)
    Speed sweep   measure every speed from 100% down until the cam stalls, forward or reverse
    Aim check     look at 25% and 75% three times each and measure how far past the
                  target the eyes stop; suggests an aim lead
    Soak          run continuously for N minutes and flag odd sweeps (missed switch, bounce, stall)
    LED steps     step the LEDs down 100 -> 1% so you can see where they disappear

  Calibration (sweep time per speed) is saved in flash, so aiming works after a reboot.

    http://freddy.local   (or the IP printed on the serial monitor)

  The page uses Hotwired Turbo, loaded from a CDN. On Freddy's own access point there's
  no internet, so the page falls back to a few lines of its own JS that do the same job.
  Live values go out over a WebSocket only when they change (and at most 10 times a
  second per value).

  Board: ESP32S3 Dev Module, USB CDC On Boot: Enabled. Arduino-ESP32 core 3.x.
  Libraries (Library Manager): "ESP Async WebServer" and "Async TCP", both by ESP32Async.
  WiFi: copy secrets.h.example to secrets.h (same as freddy_web). Without it, Freddy
  starts an access point "Freddy" (password "fazbear1987") at http://192.168.4.1

  STOCK BOARD DISCONNECTED FROM THE HEAD. Wiring: docs/head-bench-test.md
  (breadboard pins below; set USE_PCB_PINS to 1 on the controller board).

  Serial: x or space = stop, h = home, ? = status.
*/

#include <WiFi.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include "esp_task_wdt.h"
#include "page.h"

#if __has_include("secrets.h")
#include "secrets.h"
#define HAVE_WIFI_SECRETS 1
#endif
#ifndef AP_PASS
#define AP_PASS "fazbear1987"
#endif
const char *HOSTNAME = "freddy";
const char *AP_SSID = "Freddy";

// ---------------- pins
#define USE_PCB_PINS 0                     // 0 = head bench breadboard, 1 = controller PCB
#if USE_PCB_PINS
const int PIN_M1 = 11, PIN_M2 = 12;        // TB6612 AIN1/AIN2: eye motor
const int PIN_L1 = 15, PIN_L2 = 16;        // TB6612 BIN1/BIN2: eye LEDs
#else
const int PIN_M1 = 38, PIN_M2 = 39;
const int PIN_L1 = 41, PIN_L2 = 42;
#endif
const int PIN_SW = 7;                      // eye home switch, LOW = closed (eyes straight ahead)
#ifdef RGB_BUILTIN
const int PIN_RGB = RGB_BUILTIN;
#else
const int PIN_RGB = 48;
#endif

const int LED_BITS = 10, LED_MAX = (1 << LED_BITS) - 1;   // 10 bits for smooth dimming
const uint32_t RUN_LIMIT_MS = 30000;       // free running stops by itself after this
const uint32_t DEBOUNCE_MS = 15;

// ---------------- settings
int speedPct = 100;                        // motor duty, steps of 5
int aimLeadMs = 0;                         // stop this much early when aiming (brake overshoot)
int ledPct = 60, ledPeriod = 3000;
enum LedMode { L_OFF, L_ON, L_BREATHE, L_FLICKER, L_BLINK };
const char *LED_MODES[] = {"off", "on", "breathe", "flicker", "blink"};
LedMode ledMode = L_OFF;

// calibration: steady sweep time (ms) per speed (index = pct/5), forward [0] and reverse [1].
// spin = how much longer the first sweep from standstill takes. 0 = not measured.
uint32_t cal[2][21];
uint16_t calSpin[2][21];
int calVersion = 0;

// ---------------- switch and position
bool swClosed = false;
uint32_t lastEdge = 0, lastClose = 0, closures = 0, lastPulse = 0;
uint32_t lastSweep = 0; int lastSweepPct = 0, lastSweepDir = 1;
uint32_t runStart = 0; int runDir = 0, runPct = 0;   // the current continuous run, for sweep validity
float phase = 0;                           // estimated position: fraction of a sweep past center
bool posKnown = false;

// ---------------- moves: the building blocks every command and test is made of
enum MoveType { MV_NONE, MV_RUN, MV_HOME, MV_TIMED, MV_SWEEPS, MV_PAUSE };
struct Move {
  MoveType type = MV_NONE;
  int dir = 1, pct = 100;
  uint32_t start = 0, ms = 0, timeout = 0;   // ms: TIMED/PAUSE length, RUN limit; timeout: HOME / per sweep
  int n = 0, got = 0;
  uint32_t t[4];                              // switch closure times (MV_SWEEPS)
} mv;
// Time since t, never negative: a move started later in this pass has a start time a
// little after 'now', and plain unsigned subtraction would make it look 49 days old.
uint32_t since(uint32_t t, uint32_t now) { return (int32_t)(now - t) > 0 ? now - t : 0; }
bool motorOn() { return mv.type == MV_RUN || mv.type == MV_HOME || mv.type == MV_TIMED || mv.type == MV_SWEEPS; }

// ---------------- jobs: multi-step commands and tests
enum Job { J_NONE, J_LOOK, J_DART, J_MEASURE, J_SPEEDS, J_AIM, J_SOAK };
const char *JOB_NAMES[] = {"none", "look", "dart", "measure", "speed sweep", "aim check", "soak"};
Job job = J_NONE;
int jStep = 0, jDir = 1, jIdx = 0, jArg = 0;
const int SPEEDS[] = {100, 90, 80, 70, 60, 50, 40, 35, 30, 25, 20, 15, 10, 5};
const int N_SPEEDS = sizeof SPEEDS / sizeof SPEEDS[0];
uint32_t jPrevSweep = 0;
// aim check
const int AIM_TARGETS[] = {25, 75}, AIM_REPS = 3;
int aimTrial = 0; uint32_t aimRun = 0; float aimOver[2][AIM_REPS];
// soak
uint32_t soakN = 0, soakSum = 0, soakMin = 0, soakMax = 0, soakLong = 0, soakShort = 0;
// LED steps (runs alongside anything else)
const int LED_STEPS[] = {100, 50, 25, 12, 6, 3, 2, 1, 0};
int ledStep = -1; uint32_t ledStepAt = 0;
bool lastFailed = false;

Preferences prefs;
int resetReason = 0;

// ---------------- web
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
struct Cmd { char a; int v; int t; };
QueueHandle_t cmdQueue;                    // web callbacks run in another task: hand commands to loop()

void startJob(Job j);
void jobStep(bool ok, const char *why);

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

// A log line: serial monitor + prepended to the page's log. Events always go out.
void report(const char *fmt, ...) {
  char msg[220], b[260];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);
  Serial.printf("# %s\n", msg);
  if (!canPush()) return;
  snprintf(b, sizeof b, "<li>%7.1fs %s</li>", millis() / 1000.0f, msg);
  stream("prepend", "log", b);
}

void resultRow(const char *test, const char *setting, const char *result, const char *note) {
  Serial.printf("# RESULT %s | %s | %s | %s\n", test, setting, result, note);
  if (!canPush()) return;
  char b[400];
  snprintf(b, sizeof b, "<tr><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>", test, setting, result, note);
  stream("prepend", "results", b);
}

// Live values: each card is sent only when its HTML changes, and at most every
// 100 ms (sys every 2 s). Values are rounded first so noise doesn't count as change.
enum { C_EYES, C_POS, C_SW, C_SWEEP, C_LED, C_JOB, C_SYS, C_CAL, N_CARDS };
const char *CARD_IDS[N_CARDS] = {"c_eyes", "c_pos", "c_sw", "c_sweep", "c_led", "c_job", "c_sys", "cal"};
uint32_t cardHash[N_CARDS], cardSent[N_CARDS];

uint32_t fnv(const char *s) { uint32_t h = 2166136261u; while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; } return h; }

void pushCard(int i, const char *html, uint32_t now) {
  uint32_t h = fnv(html);
  if (h == cardHash[i]) return;                              // unchanged: send nothing
  if (now - cardSent[i] < (i == C_SYS ? 2000u : 100u)) return; // changed, but too soon: next pass
  if (!canPush()) return;                                    // no client, or its queue is full: retry later
  cardHash[i] = h;
  cardSent[i] = now;
  stream("update", CARD_IDS[i], html);
}

float rateFor(int dir, int pct);

void pushCards(uint32_t now) {
  char b[1800];
  const char *mvName[] = {"stopped", "running", "homing", "moving", "timing sweeps", "pausing"};

  snprintf(b, sizeof b, "<div class=\"k\">Eyes</div><div class=\"v\">%s%s</div><div class=\"s\">speed %d%% &middot; aim lead %d ms</div>",
           mvName[mv.type], motorOn() ? (mv.dir > 0 ? " &rarr;" : " &larr;") : "", speedPct, aimLeadMs);
  pushCard(C_EYES, b, now);

  if (posKnown) {
    int p = ((int)lroundf(phase * 100) / 5 * 5) % 100;      // 5% steps: plenty for a bar
    snprintf(b, sizeof b, "<div class=\"k\">Position (estimate)</div><div class=\"v\">%d%%</div>"
             "<div class=\"bar\"><b style=\"left:25%%\"></b><b style=\"left:50%%\"></b><b style=\"left:75%%\"></b><i style=\"left:%d%%\"></i></div>", p, p);
  } else {
    snprintf(b, sizeof b, "<div class=\"k\">Position (estimate)</div><div class=\"v\">?</div><div class=\"s\">%s</div>",
             rateFor(1, speedPct) > 0 ? "home once to find it" : "measure this speed to track it");
  }
  pushCard(C_POS, b, now);

  snprintf(b, sizeof b, "<div class=\"k\">Home switch</div><div class=\"v\">%s</div><div class=\"s\">%lu closures &middot; closed %lu ms</div>",
           swClosed ? "CLOSED" : "open", closures, lastPulse);
  pushCard(C_SW, b, now);

  if (lastSweep) snprintf(b, sizeof b, "<div class=\"k\">Last full sweep</div><div class=\"v\">%lu ms</div><div class=\"s\">%s at %d%%</div>",
                          lastSweep, lastSweepDir > 0 ? "forward" : "reverse", lastSweepPct);
  else snprintf(b, sizeof b, "<div class=\"k\">Last full sweep</div><div class=\"v\">&ndash;</div><div class=\"s\">needs two closures in one run</div>");
  pushCard(C_SWEEP, b, now);

  if (ledStep >= 0) snprintf(b, sizeof b, "<div class=\"k\">LEDs</div><div class=\"v\">steps</div><div class=\"s\">now %d%%</div>", LED_STEPS[ledStep]);
  else snprintf(b, sizeof b, "<div class=\"k\">LEDs</div><div class=\"v\">%s</div><div class=\"s\">%d%%%s</div>", LED_MODES[ledMode], ledPct,
                (ledMode == L_BREATHE || ledMode == L_BLINK) ? (String(" &middot; ") + ledPeriod + " ms").c_str() : "");
  pushCard(C_LED, b, now);

  char detail[120] = "";
  switch (job) {
    case J_SPEEDS: snprintf(detail, sizeof detail, "%s, %d%% (%d of %d)", jDir > 0 ? "forward" : "reverse", SPEEDS[min(jIdx, N_SPEEDS - 1)], jIdx + 1, N_SPEEDS); break;
    case J_AIM: snprintf(detail, sizeof detail, "trial %d of %d", aimTrial + 1, 2 * AIM_REPS); break;
    case J_SOAK: {
      uint32_t el = since(mv.start, now) / 10000 * 10;           // 10 s steps
      snprintf(detail, sizeof detail, "%lu:%02lu of %d:00 &middot; %lu sweeps%s", el / 60, el % 60, jArg, soakN,
               (soakLong + soakShort) ? " &middot; odd ones!" : "");
      break;
    }
    case J_MEASURE: snprintf(detail, sizeof detail, "%s at %d%%", jDir > 0 ? "forward" : "reverse", speedPct); break;
    default: break;
  }
  snprintf(b, sizeof b, "<div class=\"k\">Test</div><div class=\"v\">%s</div><div class=\"s\">%s</div>",
           job == J_NONE ? (lastFailed ? "failed" : "none") : JOB_NAMES[job], job == J_NONE ? (lastFailed ? "see the log" : "&nbsp;") : detail);
  pushCard(C_JOB, b, now);

  static int shownRssi = 0;                                  // RSSI wobbles: only move it 6 dB at a time
  int rssi = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
  if (abs(rssi - shownRssi) >= 6) shownRssi = rssi;
  snprintf(b, sizeof b, "<div class=\"k\">System</div><div class=\"v\">%s</div><div class=\"s\">heap %luk &middot; reset %d</div>",
           shownRssi ? (String(shownRssi) + " dBm").c_str() : "AP mode", ESP.getFreeHeap() / 10240 * 10, resetReason);
  pushCard(C_SYS, b, now);

  // calibration table
  int n = 0;
  n += snprintf(b + n, sizeof b - n, "<tr><th>Speed</th><th>Forward</th><th>Reverse</th><th>Start-up</th></tr>");
  bool any = false;
  for (int i = 20; i >= 1 && n < (int)sizeof b - 120; i--) {
    if (!cal[0][i] && !cal[1][i]) continue;
    any = true;
    n += snprintf(b + n, sizeof b - n, "<tr><td>%d%%</td><td>%s</td><td>%s</td><td>%d ms</td></tr>", i * 5,
                  cal[0][i] ? (String(cal[0][i]) + " ms").c_str() : "&ndash;",
                  cal[1][i] ? (String(cal[1][i]) + " ms").c_str() : "&ndash;", max(calSpin[0][i], calSpin[1][i]));
  }
  if (!any) snprintf(b + n, sizeof b - n, "<tr><td colspan=\"4\">Nothing measured yet: press Measure or run a speed sweep.</td></tr>");
  pushCard(C_CAL, b, now);
}

// ---------------------------------------------------------------- calibration
void saveCal() { prefs.putBytes("cal", cal, sizeof cal); prefs.putBytes("spin", calSpin, sizeof calSpin); calVersion++; }

void setCal(int dir, int pct, uint32_t sweep, uint32_t spin) {
  int d = dir > 0 ? 0 : 1, i = pct / 5;
  cal[d][i] = sweep;
  calSpin[d][i] = min(spin, (uint32_t)60000);
  saveCal();
}

// Fraction of a sweep per ms at this speed; interpolated between measured speeds. 0 = unknown.
float rateFor(int dir, int pct) {
  int d = dir > 0 ? 0 : 1, i = constrain(pct / 5, 1, 20);
  if (cal[d][i]) return 1.0f / cal[d][i];
  int lo = -1, hi = -1;
  for (int j = i - 1; j >= 1; j--) if (cal[d][j]) { lo = j; break; }
  for (int j = i + 1; j <= 20; j++) if (cal[d][j]) { hi = j; break; }
  if (lo < 0 || hi < 0) return 0;
  float rl = 1.0f / cal[d][lo], rh = 1.0f / cal[d][hi];
  return rl + (rh - rl) * (i - lo) / (float)(hi - lo);
}
uint32_t sweepAt(int dir, int pct) { float r = rateFor(dir, pct); return r > 0 ? (uint32_t)(1.0f / r) : 0; }
uint32_t spinAt(int dir, int pct) { return calSpin[dir > 0 ? 0 : 1][constrain(pct / 5, 1, 20)]; }
uint32_t homeTimeout(int pct) { uint32_t s = sweepAt(1, pct); return s ? s * 3 / 2 + 3000 : 15000; }

// ---------------------------------------------------------------- moves
void startMove(MoveType t, int dir, int pct, uint32_t ms = 0, int n = 0, uint32_t timeout = 15000) {
  uint32_t now = millis();
  bool continuing = motorOn() && mv.dir == dir && mv.pct == pct;
  mv.type = t; mv.dir = dir; mv.pct = pct; mv.ms = ms; mv.n = n; mv.got = 0; mv.timeout = timeout; mv.start = now;
  if (t == MV_PAUSE) { motorBrake(); return; }
  if (!continuing) { runStart = now; runDir = dir; runPct = pct; }
  motorDrive(dir, pct);
}

void endMove(bool ok, const char *why) {
  MoveType was = mv.type;
  mv.type = MV_NONE;
  motorBrake();
  if (job != J_NONE) jobStep(ok, why);
  else if (why && was != MV_PAUSE) report("%s", why);
  if (!ok) lastFailed = true;
}

void stopAll(const char *why) {
  bool busy = mv.type != MV_NONE || job != J_NONE;
  job = J_NONE;
  mv.type = MV_NONE;
  motorBrake();
  if (busy) report("STOP (%s)", why);
}

// ---------------------------------------------------------------- aiming
int lookTarget = 0;

void look(int target) {
  float tgt = constrain(target, 0, 99) / 100.0f;
  float rf = rateFor(1, speedPct), rr = rateFor(-1, speedPct);
  if (rf <= 0) { report("look: measure %d%% first (or run a speed sweep)", speedPct); job = J_NONE; return; }
  if (!posKnown) {
    lookTarget = target;
    job = J_LOOK;
    report("look %d%%: homing first", target);
    startMove(MV_HOME, 1, speedPct, 0, 0, homeTimeout(speedPct));
    return;
  }
  job = J_NONE;
  float df = tgt - phase; if (df < 0) df += 1;
  float tf = df / rf, tr = 1e9f;
  if (rr > 0) { float dr = phase - tgt; if (dr < 0) dr += 1; tr = dr / rr; }  // reverse too, if measured
  int dir = tr < tf ? -1 : 1;
  float t = min(tf, tr) - aimLeadMs;
  if (t < 15) { report("look %d%%: already there", target); return; }
  report("look %d%%: %s %lu ms", target, dir > 0 ? "forward" : "reverse", (uint32_t)t);
  startMove(MV_TIMED, dir, speedPct, (uint32_t)t);
}

// ---------------------------------------------------------------- jobs
void startJob(Job j) {
  stopAll("new test");
  lastFailed = false;
  job = j; jStep = 0; jIdx = 0; aimTrial = 0;
  jobStep(true, nullptr);
}

void endJob(const char *msg) {
  job = J_NONE;
  if (msg) report("%s", msg);
}

void jobStep(bool ok, const char *why) {
  char a[64], b[96], c[120];
  switch (job) {
    case J_LOOK:
      if (!ok) { endJob(why); break; }
      look(lookTarget);                                      // now at center: aim from here
      break;

    case J_DART: {                                           // glance one way and back, then the other
      static const float SEQ[] = {1, -1, -1, 1, 0.66f, -0.66f};
      if (!ok) { endJob(why); break; }
      if (jStep >= 12) { endJob("dart done"); break; }
      if (jStep % 2 == 0) { float s = SEQ[jStep / 2]; startMove(MV_TIMED, s > 0 ? 1 : -1, speedPct, (uint32_t)(fabsf(s) * jArg)); }
      else startMove(MV_PAUSE, 1, 0, 350);
      jStep++;
      break;
    }

    case J_MEASURE:
      if (!ok) { endJob(why); break; }
      if (jStep == 0) { jStep = 1; startMove(MV_HOME, 1, speedPct, 0, 0, homeTimeout(speedPct)); break; }
      if (jStep == 1) { jStep = 2; startMove(MV_SWEEPS, jDir, speedPct, 0, 3, max((uint32_t)15000, sweepAt(jDir, speedPct) * 3)); break; }
      {
        uint32_t s1 = mv.t[0] - mv.start, s2 = mv.t[1] - mv.t[0], s3 = mv.t[2] - mv.t[1], steady = (s2 + s3) / 2;
        setCal(jDir, speedPct, steady, s1 > steady ? s1 - steady : 0);
        snprintf(a, sizeof a, "%s %d%%", jDir > 0 ? "forward" : "reverse", speedPct);
        snprintf(b, sizeof b, "%lu ms per sweep", steady);
        snprintf(c, sizeof c, "sweeps %lu, %lu, %lu ms (first from standstill)", s1, s2, s3);
        resultRow("measure", a, b, c);
        endJob(nullptr);
      }
      break;

    case J_SPEEDS:
      if (jStep == 0) {                                      // start from center
        if (!ok) { endJob(why); break; }
        jStep = 1; jPrevSweep = 0;
        report("speed sweep %s: homing", jDir > 0 ? "forward" : "reverse");
        startMove(MV_HOME, 1, 100, 0, 0, 15000);
        break;
      }
      if (jStep == 2) {                                      // a speed just finished
        snprintf(a, sizeof a, "%s %d%%", jDir > 0 ? "forward" : "reverse", SPEEDS[jIdx]);
        if (!ok) {
          snprintf(b, sizeof b, "stalled");
          snprintf(c, sizeof c, jIdx ? "slowest that turns the cam: %d%%" : "didn't turn at 100%%: check wiring", jIdx ? SPEEDS[jIdx - 1] : 0);
          resultRow("speed sweep", a, b, c);
          endJob("speed sweep done (stalled)");
          break;
        }
        uint32_t s1 = mv.t[0] - mv.start, s2 = mv.t[1] - mv.t[0];
        setCal(jDir, SPEEDS[jIdx], s2, s1 > s2 ? s1 - s2 : 0);
        snprintf(b, sizeof b, "%lu ms per sweep", s2);
        snprintf(c, sizeof c, "start-up adds %lu ms", s1 > s2 ? s1 - s2 : 0);
        resultRow("speed sweep", a, b, c);
        jPrevSweep = s2;
        if (++jIdx >= N_SPEEDS) { endJob("speed sweep done"); break; }
      } else if (!ok) { endJob(why); break; }
      jStep = 2;
      // two closures: the first includes spin-up from standstill, the second is steady
      startMove(MV_SWEEPS, jDir, SPEEDS[jIdx], 0, 2, max((uint32_t)8000, jPrevSweep * 3));
      break;

    case J_AIM: {
      // per trial: [home] -> timed run to the target -> pause -> run on to center, timing it.
      // How long it takes to get back to center shows how far past the target the eyes stopped.
      uint32_t sweep = sweepAt(1, speedPct);
      int tgt = AIM_TARGETS[aimTrial / AIM_REPS];
      if (!ok) { endJob(why); break; }
      if (jStep == 0) {
        if (!sweep) { endJob("aim check: measure this speed first"); break; }
        aimTrial = 0; jStep = 1;
        startMove(MV_HOME, 1, speedPct, 0, 0, homeTimeout(speedPct));
        break;
      }
      if (jStep == 1) {                                      // at center: go to the target
        aimRun = (uint32_t)max(15, (int)(sweep * tgt / 100) - aimLeadMs);
        jStep = 2;
        startMove(MV_TIMED, 1, speedPct, aimRun);
        break;
      }
      if (jStep == 2) { jStep = 3; startMove(MV_PAUSE, 1, 0, 700); break; }
      if (jStep == 3) { jStep = 4; startMove(MV_HOME, 1, speedPct, 0, 0, homeTimeout(speedPct)); break; }
      // back at center: how long did the rest of the sweep take?
      uint32_t back = millis() - mv.start, spin = spinAt(1, speedPct);
      float over = (float)(sweep - aimRun) - ((float)back - spin);   // + = stopped past the target
      aimOver[aimTrial / AIM_REPS][aimTrial % AIM_REPS] = over;
      report("aim %d%% trial %d: ran %lu ms, %lu ms back to center: %+.0f ms past the target", tgt, aimTrial % AIM_REPS + 1, aimRun, back, over);
      if (++aimTrial < 2 * AIM_REPS) { jStep = 1; jobStep(true, nullptr); break; }
      float all = 0;
      for (int k = 0; k < 2; k++) {
        float mn = 1e9f, mx = -1e9f, sum = 0;
        for (int r = 0; r < AIM_REPS; r++) { float o = aimOver[k][r]; mn = min(mn, o); mx = max(mx, o); sum += o; }
        all += sum;
        snprintf(a, sizeof a, "look %d%% at %d%%", AIM_TARGETS[k], speedPct);
        snprintf(b, sizeof b, "%+.0f ms past (%.1f%% of a sweep)", sum / AIM_REPS, 100.0f * sum / AIM_REPS / sweep);
        snprintf(c, sizeof c, "spread %.0f ms over %d tries", mx - mn, AIM_REPS);
        resultRow("aim check", a, b, c);
      }
      int lead = constrain(aimLeadMs + (int)lroundf(all / (2 * AIM_REPS)), 0, 500);
      snprintf(b, sizeof b, "suggested aim lead: %d ms", lead);
      resultRow("aim check", "summary", b, "set it under Eyes, then run this again");
      endJob("aim check done");
      break;
    }

    case J_SOAK:
      if (jStep == 0) {
        soakN = soakSum = soakMax = soakLong = soakShort = 0; soakMin = 0xFFFFFFFF;
        jStep = 1;
        report("soak: %d min at %d%%", jArg, speedPct);
        startMove(MV_RUN, 1, speedPct, (uint32_t)jArg * 60000);
        break;
      }
      snprintf(a, sizeof a, "%d min at %d%%", jArg, speedPct);
      if (soakN) {
        snprintf(b, sizeof b, "%lu sweeps, avg %lu ms (%lu&ndash;%lu)", soakN, soakSum / soakN, soakMin, soakMax);
        snprintf(c, sizeof c, "%s%lu long, %lu short", ok ? "" : "STOPPED EARLY: ", soakLong, soakShort);
      } else { snprintf(b, sizeof b, "no full sweeps"); snprintf(c, sizeof c, "%s", why ? why : ""); }
      resultRow("soak", a, b, c);
      endJob(ok ? "soak done" : why);
      break;

    default: break;
  }
}

void soakAdd(uint32_t s) {
  if (soakN >= 3) {
    uint32_t avg = soakSum / soakN;
    if (s > avg * 16 / 10) { soakLong++; report("soak: long sweep %lu ms (avg %lu): missed switch or a stall?", s, avg); return; }
    if (s < avg * 6 / 10)  { soakShort++; report("soak: short sweep %lu ms (avg %lu): switch bounce?", s, avg); return; }
  }
  soakN++; soakSum += s; soakMin = min(soakMin, s); soakMax = max(soakMax, s);
}

// ---------------------------------------------------------------- switch
void onClose(uint32_t now) {
  closures++;
  bool valid = lastClose && lastClose >= runStart && motorOn();
  if (valid) { lastSweep = now - lastClose; lastSweepPct = runPct; lastSweepDir = runDir; }
  if (valid && job == J_SOAK) soakAdd(now - lastClose);
  lastClose = now;
  phase = 0;
  posKnown = true;
  if (mv.type == MV_HOME) endMove(true, "home");
  else if (mv.type == MV_SWEEPS) { mv.t[mv.got++] = now; if (mv.got >= mv.n) endMove(true, nullptr); }
}

// ---------------------------------------------------------------- commands
void status() {
  report("speed %d%% | lead %d ms | switch %s | %lu closures | pos %s %.0f%% | led %s %d%% | job %s",
         speedPct, aimLeadMs, swClosed ? "closed" : "open", closures, posKnown ? "" : "unknown", phase * 100,
         LED_MODES[ledMode], ledPct, JOB_NAMES[job]);
}

void runCmd(char a, int v, int t) {
  switch (a) {
    case 'x': stopAll("stop pressed"); break;
    case 'f': case 'r':
      stopAll("new move"); lastFailed = false;
      startMove(MV_RUN, a == 'f' ? 1 : -1, speedPct, RUN_LIMIT_MS);
      report("%s at %d%% (stops by itself after 30 s)", a == 'f' ? "forward" : "reverse", speedPct);
      break;
    case 's':
      speedPct = constrain((v + 2) / 5 * 5, 5, 100);
      if (mv.type == MV_RUN && job == J_NONE) { mv.pct = speedPct; runStart = millis(); runPct = speedPct; motorDrive(mv.dir, speedPct); }
      break;
    case 'h': stopAll("new move"); lastFailed = false; startMove(MV_HOME, 1, speedPct, 0, 0, homeTimeout(speedPct)); break;
    case 'l': stopAll("new move"); lastFailed = false; look(v); break;
    case 'n': {
      int ms = constrain(abs(v), 20, 2000);
      stopAll("new move"); lastFailed = false;
      startMove(MV_TIMED, t < 0 ? -1 : 1, speedPct, ms);
      break;
    }
    case 'd': jArg = constrain(v ? v : 180, 40, 800); startJob(J_DART); break;
    case 'L': aimLeadMs = constrain(v, 0, 500); prefs.putInt("lead", aimLeadMs); report("aim lead %d ms", aimLeadMs); break;
    case 'm': jDir = v ? -1 : 1; startJob(J_MEASURE); break;
    case 'S': jDir = v ? -1 : 1; startJob(J_SPEEDS); break;
    case 'A': startJob(J_AIM); break;
    case 'K': jArg = constrain(v ? v : 5, 1, 60); startJob(J_SOAK); break;
    case 'E':
      ledStep = 0; ledStepAt = millis();
      report("LED steps: 100, 50, 25, 12, 6, 3, 2, 1%%, 2.5 s each. Note where they vanish.");
      report("LED step: %d%%", LED_STEPS[0]);
      break;
    case 'b': ledPct = constrain(v, 0, 100); if (ledMode == L_OFF && ledPct) ledMode = L_ON; ledStep = -1; break;
    case 'M': ledMode = (LedMode)constrain(v, 0, 4); if (ledMode != L_OFF && ledPct == 0) ledPct = 60; ledStep = -1; break;
    case 'P': ledPeriod = constrain(v, 200, 10000); break;
    case 'C':
      memset(cal, 0, sizeof cal); memset(calSpin, 0, sizeof calSpin); saveCal(); posKnown = false;
      report("calibration cleared");
      break;
    case 'W': memset(cardHash, 0, sizeof cardHash); status(); break;   // new page: resend every card
    case '?': status(); break;
  }
}

// ---------------------------------------------------------------- web + WiFi
void setupWeb() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *r) {
    r->send(200, "text/html", (const uint8_t *)PAGE_HTML, strlen(PAGE_HTML));
  });
  server.on("/cmd", HTTP_POST, [](AsyncWebServerRequest *r) {
    String a = r->hasParam("a", true) ? r->getParam("a", true)->value() : "";
    int v = r->hasParam("v", true) ? r->getParam("v", true)->value().toInt() : 0;
    int t = r->hasParam("t", true) ? r->getParam("t", true)->value().toInt() : 0;
    static const struct { const char *name; char code; } MAP[] = {
      {"stop", 'x'}, {"fwd", 'f'}, {"rev", 'r'}, {"speed", 's'}, {"home", 'h'}, {"look", 'l'},
      {"nudgef", 'n'}, {"nudger", 'n'}, {"dart", 'd'}, {"lead", 'L'}, {"measure", 'm'}, {"speeds", 'S'},
      {"aim", 'A'}, {"soak", 'K'}, {"ledsteps", 'E'}, {"bright", 'b'}, {"ledmode", 'M'}, {"period", 'P'},
      {"clearcal", 'C'}};
    Cmd c = {0, v, t};
    for (auto &m : MAP) if (a == m.name) c.a = m.code;
    if (a == "nudger") c.t = -1;
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
  pinMode(PIN_SW, INPUT);                  // external 10k pull-up to 3V3
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);
  cmdQueue = xQueueCreate(8, sizeof(Cmd));
  resetReason = (int)esp_reset_reason();

  prefs.begin("headtest", false);
  prefs.getBytes("cal", cal, sizeof cal);
  prefs.getBytes("spin", calSpin, sizeof calSpin);
  aimLeadMs = prefs.getInt("lead", 0);

  delay(1500);
  Serial.printf("\n# freddy_head_web | reset reason %d | pins: %s\n", resetReason, USE_PCB_PINS ? "PCB" : "breadboard");
  swClosed = digitalRead(PIN_SW) == LOW;
  setupWifi();
  setupWeb();
  // Watchdog: if loop() hangs, reset. A reset releases the motor pins.
  esp_task_wdt_config_t wdt = {.timeout_ms = 2000, .idle_core_mask = 0, .trigger_panic = true};
  if (esp_task_wdt_reconfigure(&wdt) != ESP_OK) esp_task_wdt_init(&wdt);
  esp_task_wdt_add(NULL);
  report("booted (reset reason %d). Press Home to start.", resetReason);
}

void loop() {
  esp_task_wdt_reset();
  Cmd c;
  while (xQueueReceive(cmdQueue, &c, 0) == pdTRUE) runCmd(c.a, c.v, c.t);
  while (Serial.available()) {
    char k = Serial.read();
    if (k == ' ' || k == 'x') runCmd('x', 0, 0);
    else if (k == 'h') runCmd('h', 0, 0);
    else if (k == '?') runCmd('?', 0, 0);
  }
  // Read the clock AFTER commands: a new move sets mv.start, and an older 'now' would
  // underflow 'now - mv.start' and end it instantly.
  uint32_t now = millis();
  static uint32_t lastLoop = now;
  uint32_t dt = now - lastLoop;
  lastLoop = now;

  // position estimate: integrate the calibrated speed while the motor runs
  if (motorOn() && posKnown) {
    float r = rateFor(mv.dir, mv.pct);
    if (r > 0) { phase += mv.dir * r * dt; phase -= floorf(phase); }
    else posKnown = false;                 // running at a speed we haven't measured
  }

  // switch edges, debounced
  bool closed = digitalRead(PIN_SW) == LOW;
  if (closed != swClosed && now - lastEdge > DEBOUNCE_MS) {
    lastEdge = now;
    swClosed = closed;
    if (closed) onClose(now);
    else lastPulse = now - lastClose;
  }

  // move timeouts. Re-read the clock: the switch handler may have just started a new move.
  now = millis();
  uint32_t in = since(mv.start, now);
  switch (mv.type) {
    case MV_TIMED: case MV_PAUSE: if (in >= mv.ms) endMove(true, nullptr); break;
    case MV_RUN:
      if (in >= mv.ms) endMove(true, job == J_SOAK ? nullptr : "30 s limit: stopped");
      else if (job == J_SOAK) {
        uint32_t quiet = since(max(lastClose, mv.start), now), lim = soakN >= 3 ? soakSum / soakN * 3 : 15000;
        if (quiet > lim) endMove(false, "stalled: no switch closure");
      }
      break;
    case MV_HOME: if (in > mv.timeout) endMove(false, "home switch never closed: check cable 1, or the speed is too low to turn the cam"); break;
    case MV_SWEEPS: { uint32_t ref = mv.got ? mv.t[mv.got - 1] : mv.start; if (since(ref, now) > mv.timeout) endMove(false, "stalled: no switch closure"); } break;
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

  // status LED: blue = moving, red = last thing failed, green = idle
  if (motorOn()) rgb(0, 0, 40); else if (lastFailed) rgb(40, 0, 0); else rgb(0, 20, 0);

  // page: check every 50 ms; each card only goes out if it changed
  static uint32_t lastCheck = 0;
  if (now - lastCheck >= 50) {
    lastCheck = now;
    pushCards(now);
  }
  static uint32_t lastClean = 0;
  if (now - lastClean >= 1000) { lastClean = now; ws.cleanupClients(); }
}
