/*
  freddy_web: the power test with a web dashboard (first step toward the final firmware)

  Same test as freddy_power_test (shoulder lift/lower cycles while watching the
  main rail, the Pololu 5V rail and motor current, and counting resets), but
  controlled from a browser over WiFi:

    http://freddy.local   (or the IP printed on the serial monitor)

  The page uses Hotwired Turbo: live readings arrive as Turbo Stream messages
  over a WebSocket (/ws), and the buttons are plain forms that POST to /cmd.
  Turbo is embedded in flash (turbo_js.h), so the page works without internet,
  including on Freddy's own access point.

  Because WiFi is running, the ESP32 draws more current than in the serial-only
  test, which makes the power test closer to the real thing.

  Board: ESP32S3 Dev Module, USB CDC On Boot: Enabled. Arduino-ESP32 core 3.x.
  Libraries (Library Manager): "ESP Async WebServer" and "Async TCP", both by ESP32Async.

  WiFi: copy secrets.h.example to secrets.h and fill it in. Without it (or if
  joining fails within 15 s), Freddy starts an access point named "Freddy"
  (password "fazbear1987" unless AP_PASS is set) at http://192.168.4.1

  Wiring: identical to freddy_power_test (see docs/phase2-power-test.md).
  Serial keys still work: x stop, g cycle, l lift, o lower, [ ] duty, e elbow, s stats, c clear.
*/

#include <WiFi.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include "esp_system.h"
#include "page.h"
#include "turbo_js.h"

#if __has_include("secrets.h")
#include "secrets.h"
#define HAVE_WIFI_SECRETS 1
#endif
#ifndef AP_PASS
#define AP_PASS "fazbear1987"
#endif
const char *HOSTNAME = "freddy";
const char *AP_SSID = "Freddy";

// ---------------- pins (same as freddy_power_test)
const int PIN_VMON = 1, PIN_V5MON = 10, PIN_CS = 2;
const int PIN_IN1 = 11, PIN_IN2 = 12;
const int PIN_SW = 5, PIN_ELBOW = 17;
#ifdef RGB_BUILTIN
const int PIN_RGB = RGB_BUILTIN;
#else
const int PIN_RGB = 48;
#endif

// ---------------- tuning
const float DIV = 69.0f / 22.0f;            // 47k/22k divider
const float CS_MV_PER_A = 1163.0f;          // = CS-to-GND resistance in ohms: board 2.47k || added 2.2k = 1.16k (limit ~2.8A)
int liftDuty = 50;                          // %, stock lift is ~3V across the motor
const int LOWER_DUTY = 100;
const uint32_t LIFT_EXTRA_MS = 800, LIFT_TIMEOUT_MS = 6000, LOWER_TIMEOUT_MS = 4000;
const uint32_t HOLD_MS = 1500, REST_MS = 2000;
const float V5_MIN_OK = 4.75f;
const int ELBOW_HOME = -700, ELBOW_UP = 0;  // right arm range is -700..350, left -700..550

// ---------------- state
enum State { IDLE, LIFT, LIFT_EXTRA, HOLD, LOWER, REST };
const char *STATE_NAMES[] = {"idle", "lifting", "lifting", "holding", "lowering", "resting"};
State state = IDLE;
bool cycling = false, singleMove = false, elbowOn = false;
uint32_t stateStart = 0, cycles = 0, lastElbow = 0, lastPush = 0;
int elbowTarget = ELBOW_HOME;
float vmot = 0, v5 = 0, amps = 0;
float minVmot, minV5, maxAmps;                       // this cycle
float allMinVmot = 99, allMinV5 = 99, allMaxAmps = 0; // this boot

Preferences prefs;
uint32_t boots = 0, brownouts = 0, otherResets = 0;
int resetReason = 0;

// ---------------- web
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
struct Cmd { char a; int v; };
QueueHandle_t cmdQueue;                     // web callbacks run in another task: hand commands to loop()

// ---------------------------------------------------------------- hardware helpers
float readV(int pin) { return analogReadMilliVolts(pin) / 1000.0f * DIV; }
float readAmps()     { return analogReadMilliVolts(PIN_CS) / CS_MV_PER_A; }
bool switchHigh()    { return digitalRead(PIN_SW) == HIGH; }
void rgb(uint8_t r, uint8_t g, uint8_t b) { rgbLedWrite(PIN_RGB, r, g, b); }

// Slow-decay PWM, like the stock board: hold one side high and pulse the other
// between low (drive) and high (brake). At 50% this really delivers about half the
// voltage. (Pulsing to "coast" instead wastes most of the push at partial duty.)
// Writing 255 at 8-bit resolution means fully on in Arduino-ESP32 3.x.
void drive(int dir, int pct) {
  int brakeDuty = 255 - constrain(pct, 0, 100) * 255 / 100;
  if (dir > 0) { ledcWrite(PIN_IN1, 255); ledcWrite(PIN_IN2, brakeDuty); }   // OUT1 high: lift
  else         { ledcWrite(PIN_IN2, 255); ledcWrite(PIN_IN1, brakeDuty); }   // OUT2 high: lower
}
void motorOff() { ledcWrite(PIN_IN1, 0); ledcWrite(PIN_IN2, 0); }          // both low = coast

void elbowSend(int pos, int ms) {
  uint8_t f[9] = {0xAB, 0x01, 0x09, 0x41, (uint8_t)(pos & 0xFF), (uint8_t)((pos >> 8) & 0xFF),
                  (uint8_t)(ms & 0xFF), (uint8_t)((ms >> 8) & 0xFF), 0};
  uint32_t c = 0xAF;
  for (int i = 0; i < 8; i++) c += (9 - i) * f[i];
  f[8] = c & 0xFF;
  Serial1.write(f, 9);
}

// ---------------------------------------------------------------- Turbo Stream output
bool canPush() { return ws.count() > 0 && ws.availableForWriteAll(); }

// One log line: serial monitor + prepended to the page's log.
void report(const char *fmt, ...) {
  char msg[200];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);
  Serial.printf("# %s\n", msg);
  if (canPush()) {
    char b[320];
    snprintf(b, sizeof b, "<turbo-stream action=\"prepend\" target=\"log\"><template><li>%7.1fs %s</li></template></turbo-stream>",
             millis() / 1000.0f, msg);
    ws.textAll(b);
  }
}

void pushLive() {
  float shownVmot = allMinVmot, shownV5 = allMinV5, shownAmps = allMaxAmps;
  bool v5bad = shownV5 < V5_MIN_OK, resetBad = boots > 1 || brownouts > 0;
  char b[1600];
  snprintf(b, sizeof b,
    "<turbo-stream action=\"update\" target=\"live\"><template>"
    "<div class=\"card\"><div class=\"k\">Main rail</div><div class=\"v\">%.2f V</div><div class=\"s\">min %s</div></div>"
    "<div class=\"card%s\"><div class=\"k\">5V rail</div><div class=\"v\">%.2f V</div><div class=\"s\">min %s</div></div>"
    "<div class=\"card\"><div class=\"k\">Motor</div><div class=\"v\">%.2f A</div><div class=\"s\">peak %.2f A</div></div>"
    "<div class=\"card\"><div class=\"k\">Arm</div><div class=\"v\">%s</div><div class=\"s\">switch %s</div></div>"
    "<div class=\"card\"><div class=\"k\">Cycles</div><div class=\"v\">%lu</div><div class=\"s\">%s &middot; lift %d%%%s</div></div>"
    "<div class=\"card%s\"><div class=\"k\">Boots since clear</div><div class=\"v\">%lu</div><div class=\"s\">brownouts %lu &middot; other %lu</div></div>"
    "</template></turbo-stream>",
    vmot, shownVmot > 90 ? "&ndash;" : String(shownVmot, 2).c_str(),
    v5bad ? " warn" : "", v5, shownV5 > 90 ? "&ndash;" : String(shownV5, 2).c_str(),
    amps, shownAmps,
    STATE_NAMES[state], switchHigh() ? "raised" : "home",
    cycles, cycling ? "cycling" : "stopped", liftDuty, elbowOn ? " &middot; elbow" : "",
    resetBad ? " warn" : "", boots, brownouts, otherResets);
  ws.textAll(b);
}

void pushCycleRow() {
  if (!canPush()) return;
  bool bad = minV5 < V5_MIN_OK;
  char b[400];
  snprintf(b, sizeof b,
    "<turbo-stream action=\"prepend\" target=\"cycles\"><template><tr><td>%lu</td><td>%.2f V</td><td>%.2f V</td>"
    "<td>%.2f A</td><td class=\"%s\">%s</td></tr></template></turbo-stream>",
    cycles, minVmot, minV5, maxAmps, bad ? "bad" : "ok", bad ? "5V sagged" : "ok");
  ws.textAll(b);
}

// ---------------------------------------------------------------- state machine
void resetExtremes() { minVmot = 99; minV5 = 99; maxAmps = 0; }

void enter(State s) {
  state = s;
  stateStart = millis();
  switch (s) {
    case LIFT:
      resetExtremes();
      drive(+1, liftDuty);
      if (elbowOn) { elbowTarget = ELBOW_UP; elbowSend(elbowTarget, 1500); }
      rgb(0, 0, 40);
      break;
    case LIFT_EXTRA: break;
    case HOLD: motorOff(); break;
    case LOWER:
      drive(-1, LOWER_DUTY);
      if (elbowOn) { elbowTarget = ELBOW_HOME; elbowSend(elbowTarget, 1500); }
      break;
    case REST:
      motorOff();
      cycles++;
      report("cycle %lu: main rail min %.2fV | 5V min %.2fV | peak %.2fA %s", cycles, minVmot, minV5, maxAmps,
             minV5 < V5_MIN_OK ? "<-- 5V SAGGED" : "ok");
      pushCycleRow();
      rgb(0, 20, 0);
      break;
    case IDLE:
      motorOff();
      rgb(0, 20, 0);
      break;
  }
}

void startCycle() {
  cycling = true; singleMove = false;
  report("cycling started (lift duty %d%%)", liftDuty);
  if (switchHigh()) { resetExtremes(); enter(LOWER); }   // always start from home
  else enter(LIFT);
}

void stopAll(const char *why) {
  bool moving = state != IDLE && state != REST && state != HOLD;
  bool wasActive = state != IDLE || cycling;
  cycling = false; singleMove = false;
  enter(IDLE);
  if (moving) report("STOP (%s). This move: main rail min %.2fV | 5V min %.2fV | peak %.2fA", why, minVmot, minV5, maxAmps);
  else if (wasActive) report("STOP (%s)", why);
}

void clearCounters() {
  prefs.putUInt("boots", 0); prefs.putUInt("brown", 0); prefs.putUInt("other", 0);
  boots = 0; brownouts = 0; otherResets = 0;
  allMinVmot = 99; allMinV5 = 99; allMaxAmps = 0;
  report("reset counters and min/peak readings cleared (the next boot counts as 1)");
}

// One place for every command, from the web or the serial monitor.
void runCmd(char a, int v) {
  switch (a) {
    case 'x': stopAll("stop pressed"); break;
    case 'g': if (!cycling) startCycle(); break;
    case 'l':
      if (state != IDLE) break;
      if (switchHigh()) report("already raised: lower first");
      else { singleMove = true; enter(LIFT); report("single lift"); }
      break;
    case 'o': if (state == IDLE) { singleMove = true; enter(LOWER); report("single lower"); } break;
    case 'd': liftDuty = constrain(v, 20, 100); report("lift duty %d%%", liftDuty); break;
    case '[': runCmd('d', liftDuty - 10); break;
    case ']': runCmd('d', liftDuty + 10); break;
    case 'e': elbowOn = !elbowOn; report("elbow moves %s", elbowOn ? "ON (stock board must be disconnected)" : "off"); break;
    case 'c': clearCounters(); break;
    case 's':
      report("boots %lu | brownouts %lu | other %lu | cycles %lu | main min %.2fV | 5V min %.2fV | peak %.2fA",
             boots, brownouts, otherResets, cycles, allMinVmot, allMinV5, allMaxAmps);
      break;
  }
}

// ---------------------------------------------------------------- web setup
void setupWeb() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *r) {
    r->send(200, "text/html", (const uint8_t *)PAGE_HTML, strlen(PAGE_HTML));
  });
  server.on("/turbo.js", HTTP_GET, [](AsyncWebServerRequest *r) {
    AsyncWebServerResponse *res = r->beginResponse(200, "application/javascript", TURBO_JS_GZ, TURBO_JS_GZ_LEN);
    res->addHeader("Content-Encoding", "gzip");
    res->addHeader("Cache-Control", "max-age=86400");
    r->send(res);
  });
  server.on("/cmd", HTTP_POST, [](AsyncWebServerRequest *r) {
    String a = r->hasParam("a", true) ? r->getParam("a", true)->value() : "";
    int v = r->hasParam("v", true) ? r->getParam("v", true)->value().toInt() : 0;
    Cmd c = {0, v};
    if (a == "stop") c.a = 'x';
    else if (a == "lift") c.a = 'l';
    else if (a == "lower") c.a = 'o';
    else if (a == "cycle") c.a = 'g';
    else if (a == "duty") c.a = 'd';
    else if (a == "elbow") c.a = 'e';
    else if (a == "clear") c.a = 'c';
    if (c.a) xQueueSend(cmdQueue, &c, 0);
    String body = "<turbo-stream action=\"update\" target=\"msg\"><template>" +
                  (c.a ? String("Sent: ") + a : String("Unknown command")) + "</template></turbo-stream>";
    r->send(200, "text/vnd.turbo-stream.html", body);
  });
  ws.onEvent([](AsyncWebSocket *s, AsyncWebSocketClient *client, AwsEventType type, void *, uint8_t *, size_t) {
    if (type == WS_EVT_CONNECT) { Cmd c = {'s', 0}; xQueueSend(cmdQueue, &c, 0); }  // greet with stats
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
    WiFi.setSleep(false);                 // snappier page; costs a little current (fine for a power test)
    Serial.printf("# WiFi connected: http://%s.local  or  http://%s\n", HOSTNAME, WiFi.localIP().toString().c_str());
  } else {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.printf("# access point \"%s\" (password %s): http://%s\n", AP_SSID, AP_PASS, WiFi.softAPIP().toString().c_str());
  }
  if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);
}

// ---------------------------------------------------------------- setup / loop
void blink(int n, uint8_t r, uint8_t g, uint8_t b) {
  for (int i = 0; i < n; i++) { rgb(r, g, b); delay(250); rgb(0, 0, 0); delay(250); }
}

void setup() {
  ledcAttach(PIN_IN1, 20000, 8);
  ledcAttach(PIN_IN2, 20000, 8);
  motorOff();
  pinMode(PIN_SW, INPUT);
  analogSetAttenuation(ADC_11db);
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);
  Serial1.begin(9600, SERIAL_8N1, -1, PIN_ELBOW);
  cmdQueue = xQueueCreate(8, sizeof(Cmd));

  esp_reset_reason_t why = esp_reset_reason();
  resetReason = (int)why;
  prefs.begin("pwrtest", false);
  boots = prefs.getUInt("boots", 0) + 1;
  brownouts = prefs.getUInt("brown", 0);
  otherResets = prefs.getUInt("other", 0);
  if (why == ESP_RST_BROWNOUT) brownouts++;
  else if (why != ESP_RST_POWERON && why != ESP_RST_SW && why != ESP_RST_USB) otherResets++;
  prefs.putUInt("boots", boots); prefs.putUInt("brown", brownouts); prefs.putUInt("other", otherResets);

  delay(1500);
  Serial.printf("\n# freddy_web | reset reason %d | boots %lu | brownouts %lu | other %lu\n", resetReason, boots, brownouts, otherResets);
  // Same LED report as freddy_power_test: one red blink per boot since clearing, yellow per brownout.
  blink(boots > 10 ? 10 : boots, 40, 0, 0);
  delay(300);
  blink(brownouts > 10 ? 10 : brownouts, 40, 25, 0);

  setupWifi();
  setupWeb();
  enter(IDLE);
  report("booted (reset reason %d). Ready: press Start cycling or Lift.", resetReason);
}

void loop() {
  // commands from the page
  Cmd c;
  while (xQueueReceive(cmdQueue, &c, 0) == pdTRUE) runCmd(c.a, c.v);
  // commands from the serial monitor
  while (Serial.available()) {
    char k = Serial.read();
    if (k == ' ') k = 'x';
    runCmd(k, 0);
  }

  // Read the clock AFTER running commands: a command that starts a move resets
  // stateStart, and an older 'now' would make the new move look ancient (instant timeout).
  uint32_t now = millis(), inState = now - stateStart;

  // measure every pass, so short dips are caught
  vmot = readV(PIN_VMON); v5 = readV(PIN_V5MON); amps = readAmps();
  if (state != IDLE && state != REST && state != HOLD) {
    minVmot = min(minVmot, vmot); minV5 = min(minV5, v5); maxAmps = max(maxAmps, amps);
    // session extremes too, so they survive a timeout or STOP (not just completed cycles)
    allMinVmot = min(allMinVmot, vmot); allMinV5 = min(allMinV5, v5); allMaxAmps = max(allMaxAmps, amps);
  }

  // stock-style motion: lift until the switch goes high, a bit more, hold, lower until it goes low
  switch (state) {
    case LIFT:
      if (switchHigh()) enter(LIFT_EXTRA);
      else if (inState > LIFT_TIMEOUT_MS) stopAll("lift timeout: switch never went high");
      break;
    case LIFT_EXTRA:
      if (inState > LIFT_EXTRA_MS) enter(HOLD);
      break;
    case HOLD:
      if (singleMove) { singleMove = false; enter(REST); enter(IDLE); }
      else if (inState > HOLD_MS) enter(LOWER);
      break;
    case LOWER:
      if (!switchHigh() && inState > 300) {
        if (singleMove) { singleMove = false; enter(REST); enter(IDLE); }
        else enter(REST);
      } else if (inState > LOWER_TIMEOUT_MS) stopAll("lower timeout: switch never went low");
      break;
    case REST:
      if (cycling && inState > REST_MS) enter(LIFT);
      break;
    case IDLE: break;
  }

  if (elbowOn && state != IDLE && now - lastElbow > 1000) { elbowSend(elbowTarget, 1500); lastElbow = now; }

  // live dashboard: 4 updates a second
  if (now - lastPush >= 250) {
    lastPush = now;
    if (canPush()) pushLive();
    ws.cleanupClients();
  }
}
