/*
  freddy_head_web: head bench test with a web dashboard

  The eye home switch is broken (its contacts fell out and can't be reached), so this
  version positions the eyes by time alone, with center set by eye:

    1. Jog with short pulses until the eyes look straight ahead, then "Set center here".
    2. Learn the turn length: "Spin N turns", jog back to dead center, "Back at center".
       The firmware divides all the travel by N, so 10 turns makes jog error 10x smaller.
    3. Jog to full right and "Mark right"; same for left. Go to center/right/left then
       works as a timed forward move.

  Position is a time: ms of forward travel at full speed past center, 0..turn length.
  It's tracked while the motor runs at full speed and saved in flash when a move ends,
  so it survives a reboot (the gearbox shouldn't move with the power off). Timing
  errors add up with every start and stop, so re-center by eye now and then.

  v2: a calibration wizard on the page walks through all of that (center, turn length,
  full right, full left, a check tour), plus a one-step Quick re-center for drift.

  Also: free running at any speed (position becomes unknown below 100%), dart,
  LED modes (off, on, breathe, flicker, blink), LED steps.

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

  Serial: x or space = stop, ? = status.
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

// Bump FW_VERSION with each change; the build time tells two builds of one version apart.
#define FW_VERSION "2.0.0"
const char *FW_BUILD = __DATE__ " " __TIME__;
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
#ifdef RGB_BUILTIN
const int PIN_RGB = RGB_BUILTIN;
#else
const int PIN_RGB = 48;
#endif

const int LED_BITS = 10, LED_MAX = (1 << LED_BITS) - 1;   // 10 bits for smooth dimming
const int POS_SPEED = 100;                 // positioning always runs at full speed
const uint32_t RUN_LIMIT_MS = 30000;       // free running stops by itself after this

// ---------------- settings (saved in flash)
int speedPct = 100;                        // free-run speed
// Defaults measured on the bench head (Nov 2026); anything saved in flash wins.
const int DEF_TURN_MS = 4224, DEF_RIGHT_MS = 1394, DEF_LEFT_MS = 2957;   // right 33%, left 70% of a turn
int turnMs = DEF_TURN_MS;                  // one full turn at full speed
int rightMs = DEF_RIGHT_MS, leftMs = DEF_LEFT_MS;   // marked positions, ms past center; -1 = not marked
int aimLeadMs = 0;                         // stop this much early on go-to moves (coast after braking)
int ledPct = 60, ledPeriod = 3000;
enum LedMode { L_OFF, L_ON, L_BREATHE, L_FLICKER, L_BLINK };
const char *LED_MODES[] = {"off", "on", "breathe", "flicker", "blink"};
LedMode ledMode = L_OFF;

// ---------------- position
float posMs = 0;                           // ms of forward travel past center, kept in 0..turnMs
float odoMs = 0;                           // net travel since center was last set (for learning the turn)
bool posKnown = false, posRestored = false;
bool posDirty = false;                     // changed since it was last saved
uint32_t lastMoveEnd = 0;

// ---------------- moves
enum MoveType { MV_NONE, MV_RUN, MV_TIMED, MV_PAUSE };
struct Move {
  MoveType type = MV_NONE;
  int dir = 1, pct = 100;
  uint32_t start = 0, ms = 0;              // TIMED/PAUSE length, RUN limit
} mv;
bool motorOn() { return mv.type == MV_RUN || mv.type == MV_TIMED; }
// Time since t, never negative (a move started later in this pass can be "in the future").
uint32_t since(uint32_t t, uint32_t now) { return (int32_t)(now - t) > 0 ? now - t : 0; }

// ---------------- jobs
enum Job { J_NONE, J_DART, J_SPIN, J_TOUR };
const char *JOB_NAMES[] = {"none", "dart", "spin", "tour"};

// ---------------- calibration wizard
// 0 = not running; 1 center, 2 turn length, 3 full right, 4 full left, 5 check.
// Quick re-center is step 1 alone.
int wizStep = 0;
bool wizQuick = false;
int wizSnapTurn, wizSnapRight, wizSnapLeft;   // restored on Cancel
bool wizSpun = false;
Job job = J_NONE;
int jStep = 0, jArg = 0;
int spinTurns = 0;
// LED steps (runs alongside anything else)
const int LED_STEPS[] = {100, 50, 25, 12, 6, 3, 2, 1, 0};
int ledStep = -1; uint32_t ledStepAt = 0;

Preferences prefs;
int resetReason = 0;

// ---------------- web
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
struct Cmd { char a; int v; int t; };
QueueHandle_t cmdQueue;                    // web callbacks run in another task: hand commands to loop()

void jobStep();
void startJob(Job j);

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

int pctOf(float ms) { return (int)lroundf(100.0f * ms / turnMs) % 100; }

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
  snprintf(b, sizeof b, "<input type=\"number\" name=\"v\" min=\"0\" max=\"500\" step=\"5\" value=\"%d\">", aimLeadMs);
  stream("update", "f_lead", b);
  snprintf(b, sizeof b, "<input type=\"number\" name=\"v\" min=\"1000\" max=\"20000\" step=\"1\" value=\"%d\">", turnMs);
  stream("update", "f_turn", b);
}

// Live values: each card is sent only when its HTML changes, and at most every
// 100 ms (sys every 2 s). Values are rounded first so noise doesn't count as change.
enum { C_EYES, C_POS, C_CAL, C_LED, C_SYS, C_WIZ, N_CARDS };
const char *CARD_IDS[N_CARDS] = {"c_eyes", "c_pos", "c_cal", "c_led", "c_sys", "wiz"};
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

// ---------------------------------------------------------------- wizard card
const char JOG_HTML[] =
  "<form action=\"/cmd\" method=\"post\" class=\"jog\">"
  "<button name=\"a\" value=\"jogr\" onclick=\"this.form.v.value=250\">&laquo; 250</button>"
  "<button name=\"a\" value=\"jogr\" onclick=\"this.form.v.value=100\">100</button>"
  "<button name=\"a\" value=\"jogr\" onclick=\"this.form.v.value=50\">50</button>"
  "<button name=\"a\" value=\"jogr\" onclick=\"this.form.v.value=20\">20 &lsaquo;</button><span class=\"gap\"></span>"
  "<button name=\"a\" value=\"jogf\" onclick=\"this.form.v.value=20\">&rsaquo; 20</button>"
  "<button name=\"a\" value=\"jogf\" onclick=\"this.form.v.value=50\">50</button>"
  "<button name=\"a\" value=\"jogf\" onclick=\"this.form.v.value=100\">100</button>"
  "<button name=\"a\" value=\"jogf\" onclick=\"this.form.v.value=250\">250 &raquo;</button>"
  "<input type=\"hidden\" name=\"v\" value=\"50\"></form>";

// one wizard button: a = wizard action number
String wbtn(int a, const char *label, const char *cls = "") {
  return String("<button name=\"v\" value=\"") + a + "\" class=\"" + cls + "\">" + label + "</button>";
}
// wizard actions: 0 start, 1 quick re-center, 2 next, 3 back, 4 cancel, 5 skip, 6 spin, 7 tour, 8 go near mark
String wform(const String &buttons) {
  return "<form action=\"/cmd\" method=\"post\" class=\"wbtns\"><input type=\"hidden\" name=\"a\" value=\"wiz\">" + buttons + "</form>";
}

void pushWizard(uint32_t now) {
  String h;
  h.reserve(2600);
  const char *pct = "%";
  if (wizStep == 0) {
    h = "<h3>Calibration</h3><p>" + String(posKnown ? "" : "<b>Position unknown.</b> ") +
        "The wizard walks through center, turn length, full right and full left. "
        "Quick re-center just finds center again, for when the eyes have drifted.</p>" +
        wform(wbtn(0, "Start calibration wizard", "primary") + wbtn(1, "Quick re-center"));
  } else {
    String title, text, buttons;
    char tmp[200];
    switch (wizStep) {
      case 1:
        title = wizQuick ? "Quick re-center" : "Step 1 of 5 &middot; Center";
        text = "Jog until the eyes look straight ahead. Big steps to get close, then 20 ms taps.";
        buttons = wbtn(2, wizQuick ? "Done: this is center" : "Next: this is center", "primary");
        break;
      case 2:
        snprintf(tmp, sizeof tmp, "Spin 10 turns from center: they should come back to center. If they're off, jog them back to "
                 "dead center, then Next. The whole trip divided by 10 gives the turn length (now %d ms).", turnMs);
        title = "Step 2 of 5 &middot; Turn length";
        text = tmp;
        buttons = wbtn(6, "Spin 10 turns") + wbtn(2, "Next: back at center", wizSpun ? "primary" : "") +
                  wbtn(5, (String("Skip: keep ") + turnMs + " ms").c_str());
        break;
      case 3: case 4: {
        bool right = wizStep == 3;
        int mark = right ? rightMs : leftMs;
        snprintf(tmp, sizeof tmp, "Jog until the eyes are as far %s as they go: past that they start coming back. "
                 "Forward jogs work best; use reverse to back up if you overshoot.", right ? "right" : "left");
        title = right ? "Step 3 of 5 &middot; Full right" : "Step 4 of 5 &middot; Full left";
        text = tmp;
        if (mark >= 0) buttons += wbtn(8, (String("Start near the old mark (") + pctOf(mark) + pct + ")").c_str());
        buttons += wbtn(2, right ? "Next: this is full right" : "Next: this is full left", "primary");
        break;
      }
      case 5:
        snprintf(tmp, sizeof tmp, "The tour goes center, right, left, center, pausing at each. If the eyes stop a little past "
                 "each spot, raise the aim lead (now %d ms) and run it again.", aimLeadMs);
        title = "Step 5 of 5 &middot; Check";
        text = tmp;
        buttons = wbtn(7, "Run the tour") + wbtn(2, "Finish", "primary");
        break;
    }
    if (wizStep > 1) buttons += wbtn(3, "Back");
    buttons += wbtn(4, "Cancel");
    h = "<h3>" + title + "</h3><p>" + text + "</p>";
    if (wizStep <= 4) h += JOG_HTML;
    if (wizStep == 5)
      h += String("<form action=\"/cmd\" method=\"post\"><input type=\"hidden\" name=\"a\" value=\"lead\"><label>Aim lead</label>"
                  "<input type=\"number\" name=\"v\" min=\"0\" max=\"500\" step=\"5\" value=\"") + aimLeadMs +
           "\"> ms<button>Set</button></form>";
    h += wform(buttons);
  }
  pushCard(C_WIZ, h.c_str(), now);
}

void pushCards(uint32_t now) {
  char b[900], m[160];
  const char *mvName[] = {"stopped", "running", "moving", "pausing"};

  snprintf(b, sizeof b, "<div class=\"k\">Eyes</div><div class=\"v\">%s%s</div><div class=\"s\">%s%s</div>",
           job != J_NONE ? JOB_NAMES[job] : mvName[mv.type], motorOn() ? (mv.dir > 0 ? " &rarr;" : " &larr;") : "",
           motorOn() ? "" : "free-run speed ", motorOn() ? (String(mv.pct) + "%").c_str() : (String(speedPct) + "%").c_str());
  pushCard(C_EYES, b, now);

  if (posKnown) {
    int p = pctOf(posMs);
    int n = snprintf(m, sizeof m, "%s", "");
    if (rightMs >= 0) n += snprintf(m + n, sizeof m - n, "<b class=\"r\" style=\"left:%d%%\"></b>", pctOf(rightMs));
    if (leftMs >= 0) n += snprintf(m + n, sizeof m - n, "<b class=\"l\" style=\"left:%d%%\"></b>", pctOf(leftMs));
    snprintf(b, sizeof b, "<div class=\"k\">Position%s</div><div class=\"v\">%d%%</div><div class=\"s\">%d ms past center</div>"
             "<div class=\"bar\"><b class=\"c\" style=\"left:0%%\"></b>%s<i style=\"left:%d%%\"></i></div>",
             posRestored ? " (restored)" : "", p, (int)(posMs / 10) * 10, m, p);
  } else {
    snprintf(b, sizeof b, "<div class=\"k\">Position</div><div class=\"v\">?</div><div class=\"s\">jog to center, then Set center here</div>");
  }
  pushCard(C_POS, b, now);

  snprintf(b, sizeof b, "<div class=\"k\">Calibration</div><div class=\"v\">%d ms</div><div class=\"s\">per turn &middot; R %s &middot; L %s &middot; lead %d ms</div>",
           turnMs, rightMs >= 0 ? (String(pctOf(rightMs)) + "%").c_str() : "&ndash;",
           leftMs >= 0 ? (String(pctOf(leftMs)) + "%").c_str() : "&ndash;", aimLeadMs);
  pushCard(C_CAL, b, now);

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
  pushWizard(now);
}

// ---------------------------------------------------------------- settings in flash
void saveSettings() {
  prefs.putInt("turn", turnMs);
  prefs.putInt("right", rightMs);
  prefs.putInt("left", leftMs);
  prefs.putInt("lead", aimLeadMs);
}
void savePos() {
  prefs.putFloat("pos", posMs);
  prefs.putFloat("odo", odoMs);
  prefs.putBool("posk", posKnown);
  posDirty = false;
}

// ---------------------------------------------------------------- moves
void startMove(MoveType t, int dir, int pct, uint32_t ms) {
  mv.type = t; mv.dir = dir; mv.pct = pct; mv.ms = ms; mv.start = millis();
  if (t == MV_PAUSE) { motorBrake(); return; }
  if (pct != POS_SPEED && posKnown) { posKnown = false; posDirty = true; report("running below full speed: position no longer tracked"); }
  motorDrive(dir, pct);
}

void endMove() {
  bool wasMotor = motorOn();
  mv.type = MV_NONE;
  motorBrake();
  if (wasMotor) lastMoveEnd = millis();
  if (job != J_NONE) jobStep();
}

void stopAll(const char *why) {
  bool busy = mv.type != MV_NONE || job != J_NONE;
  if (motorOn()) lastMoveEnd = millis();
  job = J_NONE;
  mv.type = MV_NONE;
  motorBrake();
  if (busy && why) report("STOP (%s)", why);
}

// A forward timed move to a position (ms past center), stopping aimLeadMs early.
void gotoMs(float target, const char *name) {
  if (!posKnown) { report("go to %s: position unknown, set center first", name); return; }
  float d = target - posMs;
  while (d < 0) d += turnMs;
  while (d >= turnMs) d -= turnMs;
  float t = d - aimLeadMs;
  if (t < 15) { report("go to %s: already there", name); return; }
  report("go to %s: forward %lu ms", name, (uint32_t)t);
  startMove(MV_TIMED, 1, POS_SPEED, (uint32_t)t);
}

// ---------------------------------------------------------------- jobs
void startJob(Job j) {
  stopAll(nullptr);
  job = j; jStep = 0;
  jobStep();
}

void jobStep() {
  switch (job) {
    case J_DART: {                                           // glance one way and back, then the other
      static const float SEQ[] = {1, -1, -1, 1, 0.66f, -0.66f};
      if (jStep >= 12) { job = J_NONE; report("dart done"); break; }
      if (jStep % 2 == 0) { float s = SEQ[jStep / 2]; startMove(MV_TIMED, s > 0 ? 1 : -1, POS_SPEED, (uint32_t)(fabsf(s) * jArg)); }
      else startMove(MV_PAUSE, 1, 0, 350);
      jStep++;
      break;
    }
    case J_SPIN:
      if (jStep == 0) {
        jStep = 1;
        uint32_t t = (uint32_t)max(15, spinTurns * turnMs - aimLeadMs);
        report("spin %d turns: %lu ms. Should end where it started; if not, jog to center and press Back at center.", spinTurns, t);
        startMove(MV_TIMED, 1, POS_SPEED, t);
      } else {
        job = J_NONE;
        report("spin done");
      }
      break;
    case J_TOUR: {                                           // center, right, left, center with pauses
      static const char *NAMES[] = {"center", "right", "left", "center"};
      if (jStep >= 8) { job = J_NONE; report("tour done"); break; }
      if (jStep % 2 == 0) {
        int i = jStep / 2;
        float target = i == 1 ? rightMs : i == 2 ? leftMs : 0;
        jStep++;
        if ((i == 1 && rightMs < 0) || (i == 2 && leftMs < 0) || !posKnown) { jobStep(); break; }
        Job keep = job;
        gotoMs(target, NAMES[i]);
        job = keep;
        if (mv.type == MV_NONE) jobStep();                   // already there: next stop
      } else {
        jStep++;
        startMove(MV_PAUSE, 1, 0, 1200);
      }
      break;
    }
    default: break;
  }
}

// ---------------------------------------------------------------- calibration helpers
void setCenter() {
  posMs = 0; odoMs = 0; posKnown = true; posRestored = false; savePos();
  report("center set here");
}

bool learnTurn() {
  char b1[64], b2[128];
  stopAll(nullptr);
  if (!posKnown) { report("set center first"); return false; }
  int turns = (int)lroundf(odoMs / turnMs);
  if (turns < 1) { report("back at center: less than a turn since center was set. Spin some turns first."); return false; }
  int old = turnMs;
  turnMs = constrain((int)lroundf(odoMs / turns), 1000, 20000);
  snprintf(b1, sizeof b1, "%d ms per turn", turnMs);
  snprintf(b2, sizeof b2, "%.0f ms over %d turns (was %d ms, %+d)", odoMs, turns, old, turnMs - old);
  resultRow("learned turn length", b1, b2);
  posMs = 0; odoMs = 0; posRestored = false;
  saveSettings(); savePos();
  return true;
}

void markHere(bool right) {
  char b1[48], b2[64], b3[48];
  int &mark = right ? rightMs : leftMs;
  mark = (int)lroundf(posMs);
  saveSettings();
  snprintf(b1, sizeof b1, "marked %s", right ? "full right" : "full left");
  snprintf(b2, sizeof b2, "%d ms past center", mark);
  snprintf(b3, sizeof b3, "%d%% of a turn", pctOf(mark));
  resultRow(b1, b2, b3);
}

// a: 0 start, 1 quick re-center, 2 next, 3 back, 4 cancel, 5 skip, 6 spin, 7 tour, 8 go near mark
void wizard(int a) {
  char b[160];
  switch (a) {
    case 0: case 1:
      stopAll(nullptr);
      wizSnapTurn = turnMs; wizSnapRight = rightMs; wizSnapLeft = leftMs;
      wizQuick = a == 1; wizStep = 1; wizSpun = false;
      report(wizQuick ? "re-center: jog to dead center" : "calibration wizard started");
      break;
    case 2:                                                  // next: commit this step
      stopAll(nullptr);
      if (wizStep == 1) {
        setCenter();
        if (wizQuick) { wizStep = 0; report("re-centered"); break; }
        wizStep = 2; wizSpun = false;
      } else if (wizStep == 2) {
        if (!wizSpun) { report("spin the 10 turns first (or Skip)"); break; }
        if (learnTurn()) wizStep = 3;
      } else if (wizStep == 3 || wizStep == 4) {
        if (!posKnown) { report("position unknown: go back to step 1"); break; }
        markHere(wizStep == 3);
        wizStep++;
        if (wizStep == 5) startJob(J_TOUR);                  // show the result straight away
      } else if (wizStep == 5) {
        snprintf(b, sizeof b, "%d ms per turn", turnMs);
        char n[96]; snprintf(n, sizeof n, "right %d%%, left %d%%, aim lead %d ms", pctOf(rightMs), pctOf(leftMs), aimLeadMs);
        resultRow("calibration saved", b, n);
        wizStep = 0;
      }
      break;
    case 3:                                                  // back (learning the turn needs center, so 3 -> 1)
      stopAll(nullptr);
      wizStep = wizStep <= 3 ? 1 : wizStep - 1;
      wizSpun = false;
      break;
    case 4:
      stopAll(nullptr);
      turnMs = wizSnapTurn; rightMs = wizSnapRight; leftMs = wizSnapLeft; saveSettings();
      wizStep = 0;
      report("calibration wizard cancelled: previous turn length and marks kept");
      break;
    case 5:                                                  // skip the turn length
      if (wizStep == 2) { stopAll(nullptr); wizStep = 3; report("kept turn length %d ms", turnMs); }
      break;
    case 6:                                                  // spin 10 turns from center
      if (wizStep != 2) break;
      odoMs = 0; posMs = 0;                                  // step 1 just set center here
      wizSpun = true;
      spinTurns = 10; startJob(J_SPIN);
      break;
    case 7: startJob(J_TOUR); break;
    case 8:
      stopAll(nullptr);
      if (wizStep == 3 && rightMs >= 0) gotoMs(rightMs, "old right mark");
      if (wizStep == 4 && leftMs >= 0) gotoMs(leftMs, "old left mark");
      break;
  }
}

// ---------------------------------------------------------------- commands
void status() {
  report("pos %s %.0f ms (%d%%) | turn %d ms | right %d | left %d | lead %d ms | led %s %d%%",
         posKnown ? "" : "unknown", posMs, pctOf(posMs), turnMs, rightMs, leftMs, aimLeadMs, LED_MODES[ledMode], ledPct);
}

void runCmd(char a, int v, int t) {
  char b1[96], b2[160];
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
    case 'j': stopAll(nullptr); startMove(MV_TIMED, t < 0 ? -1 : 1, POS_SPEED, constrain(abs(v), 10, 2000)); break;
    case 'c': stopAll(nullptr); setCenter(); break;          // set center here
    case 'g': learnTurn(); break;                            // back at center: learn the turn length
    case 'w': wizard(v); break;
    case 'k':                                                // mark: v = 1 right, 2 left
      stopAll(nullptr);
      if (!posKnown) { report("set center first"); break; }
      markHere(v == 1);
      break;
    case 'G': {                                              // go to: v = 0 center, 1 right, 2 left, 3 = t%
      stopAll(nullptr);
      if (v == 1 && rightMs < 0) { report("mark full right first"); break; }
      if (v == 2 && leftMs < 0) { report("mark full left first"); break; }
      if (v == 3) { snprintf(b1, sizeof b1, "%d%%", constrain(t, 0, 99)); gotoMs(turnMs * constrain(t, 0, 99) / 100.0f, b1); }
      else gotoMs(v == 1 ? rightMs : v == 2 ? leftMs : 0, v == 1 ? "right" : v == 2 ? "left" : "center");
      break;
    }
    case 'N': spinTurns = constrain(v ? v : 1, 1, 50); startJob(J_SPIN); break;
    case 'T':
      turnMs = constrain(v, 1000, 20000); saveSettings();
      report("turn length set to %d ms", turnMs);
      break;
    case 'L': aimLeadMs = constrain(v, 0, 500); saveSettings(); report("aim lead %d ms", aimLeadMs); break;
    case 'd': jArg = constrain(v ? v : 180, 40, 800); startJob(J_DART); break;
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
    int v = r->hasParam("v", true) ? r->getParam("v", true)->value().toInt() : 0;
    int t = r->hasParam("t", true) ? r->getParam("t", true)->value().toInt() : 0;
    static const struct { const char *name; char code; } MAP[] = {
      {"stop", 'x'}, {"fwd", 'f'}, {"rev", 'r'}, {"speed", 's'}, {"jogf", 'j'}, {"jogr", 'j'},
      {"center", 'c'}, {"learn", 'g'}, {"mark", 'k'}, {"goto", 'G'}, {"spin", 'N'}, {"turn", 'T'},
      {"lead", 'L'}, {"dart", 'd'}, {"ledsteps", 'E'}, {"bright", 'b'}, {"ledmode", 'M'}, {"period", 'P'},
      {"clearlog", 'Z'}, {"wiz", 'w'}};
    Cmd c = {0, v, t};
    for (auto &m : MAP) if (a == m.name) c.a = m.code;
    if (a == "jogr") c.t = -1;
    if (a == "jogf") c.t = 1;
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

  prefs.begin("headtest2", false);
  turnMs = prefs.getInt("turn", DEF_TURN_MS);
  rightMs = prefs.getInt("right", DEF_RIGHT_MS);
  leftMs = prefs.getInt("left", DEF_LEFT_MS);
  aimLeadMs = prefs.getInt("lead", 0);
  posKnown = prefs.getBool("posk", true);   // nothing saved yet: assume the eyes are centered
  posMs = prefs.getFloat("pos", 0);
  odoMs = prefs.getFloat("odo", 0);
  posRestored = posKnown;

  delay(1500);
  Serial.printf("\n# freddy_head_web v%s (%s) | reset reason %d | pins: %s\n", FW_VERSION, FW_BUILD, resetReason, USE_PCB_PINS ? "PCB" : "breadboard");
  setupWifi();
  setupWeb();
  // Watchdog: if loop() hangs, reset. A reset releases the motor pins.
  esp_task_wdt_config_t wdt = {.timeout_ms = 2000, .idle_core_mask = 0, .trigger_panic = true};
  if (esp_task_wdt_reconfigure(&wdt) != ESP_OK) esp_task_wdt_init(&wdt);
  esp_task_wdt_add(NULL);
  if (posRestored) report("booted v%s. Position restored (%d%% of a turn): check the eyes agree.", FW_VERSION, pctOf(posMs));
  else report("booted v%s. Jog the eyes to dead center, then press Set center here.", FW_VERSION);
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
  static uint32_t lastLoop = now;
  uint32_t dt = since(lastLoop, now);
  lastLoop = now;

  // position: count travel while the motor runs at full speed
  if (motorOn() && mv.pct == POS_SPEED && posKnown) {
    float d = mv.dir * (float)dt;
    posMs += d; odoMs += d;
    while (posMs < 0) posMs += turnMs;
    while (posMs >= turnMs) posMs -= turnMs;
    posDirty = true;
    posRestored = false;
  }

  // move timeouts
  uint32_t in = since(mv.start, now);
  switch (mv.type) {
    case MV_TIMED: case MV_PAUSE: if (in >= mv.ms) endMove(); break;
    case MV_RUN: if (in >= mv.ms) { endMove(); report("30 s limit: stopped"); } break;
    case MV_NONE: break;
  }

  // save the position once the eyes have been still for a second (not during moves: flash wear)
  if (posDirty && !motorOn() && since(lastMoveEnd, now) > 1000) savePos();

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

  // status LED: blue = moving, green = position known, amber = unknown
  if (motorOn()) rgb(0, 0, 40); else if (posKnown) rgb(0, 20, 0); else rgb(30, 15, 0);

  static uint32_t lastCheck = 0;
  if (now - lastCheck >= 50) { lastCheck = now; pushCards(now); }
  static uint32_t lastClean = 0;
  if (now - lastClean >= 1000) { lastClean = now; ws.cleanupClients(); }
}
