/*
  freddy_head_test: explore what the head can do with a TB6612 driver

  The stock board only ever runs the eye motor forward at full power and stops
  it on the home switch. This sketch tries everything else: speed control,
  reverse, stopping at a chosen point in the sweep, short "glances", and LED
  dimming, breathing and flicker.

  Board: ESP32S3 Dev Module, USB CDC On Boot: Enabled. Arduino-ESP32 core 3.x.
  Powered from USB. The head runs off the adapter (VMOT) through the TB6612.
  Serial: picocom -b 115200 --imap lfcrlf /dev/ttyACM0

  STOCK BOARD DISCONNECTED FROM THE HEAD.

  Wiring (see docs/head-bench-test.md):
    TB6612 VM -> VMOT (adapter + after fuse)   VCC, STBY, PWMA, PWMB -> 3V3
    TB6612 AIN1 <- GPIO38, AIN2 <- GPIO39      AO1 -> head cable 3 yellow, AO2 -> cable 3 orange
    TB6612 BIN1 <- GPIO41, BIN2 <- GPIO42      BO1 -> 220R -> cable 2 yellow (LED +), BO2 -> cable 2 white (LED -)
    Eye home switch: cable 1 yellow -> 1k -> GPIO7, 10k GPIO7 -> 3V3; cable 1 white -> GND
    ALL GROUNDS TOGETHER (adapter -, TB6612 GND, ESP32 GND, cable 1 white).

  The home switch closes once per sweep, when the eyes face straight ahead.

  Commands (type, then Enter). x or space stops the motor instantly, no Enter needed.
    f / r          run forward (stock direction) / reverse, at the current speed
    x              stop (brake)
    speed 40       motor duty in % (default 100)
    home           run forward until the home switch closes, then brake
    measure        from home, time 3 sweeps at the current speed (needed for goto)
    goto 25        home, then run forward 25% of a sweep and stop
    nudge 150      forward pulse of 150 ms (negative = reverse), from wherever it is
    dart           glance demo: short forward/reverse pulses around center
    led 30         LED brightness in % (0 = off)
    breathe        LEDs fade in and out (any led command stops it)
    flicker        stock-style random flicker (any led command stops it)
    status         print the current state
*/

// ---------------- pins (same as the controller PCB)
const int PIN_AIN1 = 38, PIN_AIN2 = 39;    // eye motor
const int PIN_BIN1 = 41, PIN_BIN2 = 42;    // eye LEDs
const int PIN_SW   = 7;                    // eye home switch, LOW = closed (straight ahead)

const uint32_t RUN_LIMIT_MS = 30000;       // free-running safety stop
const uint32_t HOME_TIMEOUT_MS = 12000;

int speedPct = 100;
int ledPct = 0;
enum LedMode { LED_FIXED, LED_BREATHE, LED_FLICKER } ledMode = LED_FIXED;

enum Motion { M_STOP, M_RUN, M_HOME, M_TIMED, M_MEASURE } motion = M_STOP;
int dir = 1;                               // +1 forward, -1 reverse
uint32_t motionStart = 0, timedMs = 0;
int afterHomeGotoPct = -1;                 // goto: after homing, run this % of a sweep

// switch tracking
bool swClosed = false;
uint32_t lastClose = 0, lastOpen = 0;
uint32_t sweepMs = 0;                      // measured sweep time at measuredSpeed
int measuredSpeed = 0;
uint32_t measureTimes[4];
int measureCount = 0;

String line;

// ---------------------------------------------------------------- motor + LEDs
void motorDrive(int d, int pct) {
  int duty = constrain(pct, 0, 100) * 255 / 100;
  if (d > 0) { ledcWrite(PIN_AIN2, 0); ledcWrite(PIN_AIN1, duty); }
  else       { ledcWrite(PIN_AIN1, 0); ledcWrite(PIN_AIN2, duty); }
}
void motorBrake() { ledcWrite(PIN_AIN1, 255); ledcWrite(PIN_AIN2, 255); }

void stopMotor(const char *why) {
  motorBrake();
  if (motion != M_STOP) Serial.printf("# stop (%s) after %lu ms\n", why, millis() - motionStart);
  motion = M_STOP;
  afterHomeGotoPct = -1;
}

void startMotion(Motion m, int d) {
  motion = m;
  dir = d;
  motionStart = millis();
  motorDrive(dir, speedPct);
}

void ledWrite(int pct) { ledcWrite(PIN_BIN2, 0); ledcWrite(PIN_BIN1, constrain(pct, 0, 100) * 255 / 100); }

// ---------------------------------------------------------------- commands
void printStatus() {
  Serial.printf("# speed %d%% | motion %d | switch %s | sweep %lu ms (measured at %d%%) | led %d%% mode %d\n",
                speedPct, (int)motion, swClosed ? "CLOSED (center)" : "open", sweepMs, measuredSpeed,
                ledPct, (int)ledMode);
}

void runCommand(String c) {
  c.trim();
  if (c.length() == 0) return;
  int sp = c.indexOf(' ');
  String cmd = sp < 0 ? c : c.substring(0, sp);
  long arg = sp < 0 ? 0 : c.substring(sp + 1).toInt();
  bool hasArg = sp >= 0;

  if (cmd == "f")            { startMotion(M_RUN, 1);  Serial.printf("# forward at %d%%\n", speedPct); }
  else if (cmd == "r")       { startMotion(M_RUN, -1); Serial.printf("# reverse at %d%%\n", speedPct); }
  else if (cmd == "speed" && hasArg) {
    speedPct = constrain(arg, 5, 100);
    if (motion != M_STOP) motorDrive(dir, speedPct);
    Serial.printf("# speed %d%%\n", speedPct);
  }
  else if (cmd == "home")    { startMotion(M_HOME, 1); Serial.println("# homing"); }
  else if (cmd == "measure") {
    measureCount = 0;
    startMotion(M_MEASURE, 1);
    Serial.printf("# measuring 3 sweeps at %d%% (homes first)\n", speedPct);
  }
  else if (cmd == "goto" && hasArg) {
    if (sweepMs == 0 || measuredSpeed != speedPct) {
      Serial.println("# run 'measure' at this speed first"); return;
    }
    afterHomeGotoPct = constrain(arg, 0, 99);
    startMotion(M_HOME, 1);
    Serial.printf("# goto %d%%: homing first\n", afterHomeGotoPct);
  }
  else if (cmd == "nudge" && hasArg && arg != 0) {
    timedMs = labs(arg);
    startMotion(M_TIMED, arg > 0 ? 1 : -1);
  }
  else if (cmd == "dart") {
    // Glance one way and back, then the other way and back. Run from center (after 'home').
    int pulses[] = {180, -180, -180, 180, 120, -120};
    for (int p : pulses) {
      motorDrive(p > 0 ? 1 : -1, speedPct);
      delay(abs(p));
      motorBrake();
      delay(400);
    }
    Serial.println("# dart done");
  }
  else if (cmd == "led" && hasArg) { ledMode = LED_FIXED; ledPct = constrain(arg, 0, 100); ledWrite(ledPct); Serial.printf("# led %d%%\n", ledPct); }
  else if (cmd == "breathe") { ledMode = LED_BREATHE; if (ledPct == 0) ledPct = 100; Serial.println("# breathe"); }
  else if (cmd == "flicker") { ledMode = LED_FLICKER; if (ledPct == 0) ledPct = 100; Serial.println("# flicker"); }
  else if (cmd == "status")  { printStatus(); }
  else Serial.printf("# unknown: '%s'\n", c.c_str());
}

// ---------------------------------------------------------------- setup / loop
void setup() {
  for (int p : {PIN_AIN1, PIN_AIN2, PIN_BIN1, PIN_BIN2}) { ledcAttach(p, 20000, 8); ledcWrite(p, 0); }
  motorBrake();
  pinMode(PIN_SW, INPUT);                    // external 10k pull-up to 3V3
  Serial.begin(115200);
  delay(1500);
  swClosed = digitalRead(PIN_SW) == LOW;
  Serial.println("\n# freddy_head_test. Commands: f r x | speed N | home | measure | goto N | nudge N | dart | led N | breathe | flicker | status");
  printStatus();
}

void onSwitchClose(uint32_t now) {
  uint32_t since = lastClose ? now - lastClose : 0;
  lastClose = now;
  Serial.printf("# switch CLOSED at %lu ms (%lu ms since last close)\n", now, since);

  switch (motion) {
    case M_HOME:
      if (afterHomeGotoPct >= 0) {
        timedMs = sweepMs * afterHomeGotoPct / 100;
        afterHomeGotoPct = -1;
        if (timedMs == 0) { stopMotor("at center"); break; }
        motion = M_TIMED; motionStart = now;   // keep running forward from center
        Serial.printf("# center reached, running %lu ms more\n", timedMs);
      } else {
        stopMotor("home");
      }
      break;
    case M_MEASURE:
      measureTimes[measureCount++] = now;
      if (measureCount == 4) {
        stopMotor("measure done");
        uint32_t a = measureTimes[1] - measureTimes[0], b = measureTimes[2] - measureTimes[1], c = measureTimes[3] - measureTimes[2];
        sweepMs = (a + b + c) / 3;
        measuredSpeed = speedPct;
        Serial.printf("# sweeps: %lu, %lu, %lu ms -> average %lu ms at %d%%\n", a, b, c, sweepMs, speedPct);
      }
      break;
    default: break;
  }
}

void loop() {
  uint32_t now = millis();

  // input: x / space stop immediately; everything else is line based
  while (Serial.available()) {
    char ch = Serial.read();
    if ((ch == 'x' || ch == ' ') && line.length() == 0) { stopMotor("key"); continue; }
    if (ch == '\r' || ch == '\n') { runCommand(line); line = ""; }
    else line += ch;
  }

  // switch edges (debounced: ignore changes within 20 ms of the last one)
  bool closed = digitalRead(PIN_SW) == LOW;
  static uint32_t lastEdge = 0;
  if (closed != swClosed && now - lastEdge > 20) {
    lastEdge = now;
    swClosed = closed;
    if (closed) onSwitchClose(now);
    else {
      lastOpen = now;
      Serial.printf("# switch open (was closed %lu ms)\n", now - lastClose);
    }
  }

  // motion timeouts
  if (motion == M_TIMED && now - motionStart >= timedMs) stopMotor("timed");
  if ((motion == M_HOME || motion == M_MEASURE) && now - motionStart > HOME_TIMEOUT_MS + (motion == M_MEASURE ? 3 * 15000UL : 0))
    stopMotor("timeout: home switch never closed. Check cable 1, or the speed is too low to turn the cam");
  if (motion == M_RUN && now - motionStart > RUN_LIMIT_MS) stopMotor("30 s safety limit");

  // LED effects
  if (ledMode == LED_BREATHE) {
    float ph = (now % 3000) / 3000.0f;
    ledWrite((int)(ledPct * (0.5f - 0.5f * cosf(ph * 2 * PI))));
  } else if (ledMode == LED_FLICKER) {
    static uint32_t nextFlip = 0;
    static bool on = false;
    if (now >= nextFlip) {
      on = !on;
      ledWrite(on ? ledPct : 0);
      nextFlip = now + (on ? random(20, 120) : random(20, 200));
    }
  }
}
