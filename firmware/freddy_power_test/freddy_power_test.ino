/*
  freddy_power_test: Phase 2 breadboard power test

  Question: does the ESP32's 5V rail (Pololu S7V7F5) stay up while a shoulder
  lift drags the adapter down to ~4.9V?

  Runs shoulder lift/lower cycles (stock-style, using the shoulder switch) and
  logs the main rail (VMOT), the Pololu 5V output, and motor current. Counts
  resets in flash so a run with USB unplugged can be checked afterwards.

  Board: ESP32S3 Dev Module, USB CDC On Boot: Enabled. Arduino-ESP32 core 3.x.
  Serial: 115200.  picocom -b 115200 --imap lfcrlf /dev/ttyACM0

  Wiring (see docs/phase2-power-test.md):
    GPIO1   <- VMOT divider     (VMOT - 47k - GPIO1 - 22k - GND)
    GPIO10  <- 5V divider       (Pololu VOUT - 47k - GPIO10 - 22k - GND)
    GPIO2   <- DRV8876 IPROPI   (through 1k)
    GPIO11  -> DRV8876 IN1      (lift side: OUT1 -> cable 1 black)
    GPIO12  -> DRV8876 IN2      (lower side: OUT2 -> cable 1 red)
    GPIO5   <- shoulder switch  (cable 3 yellow - 1k - GPIO5, 100k to GND;
                                 cable 3 red -> 3V3, black -> GND)
    GPIO17  -> elbow signal     (optional, cable 2 signal through 1k)
    GPIO9   <- PIR orange       (optional, through 1k, 100k to GND)
    DRV8876 nSLEEP and PMODE -> 3V3 (PMODE high = IN/IN PWM mode)
    ESP32 5V pin <- 1N5819 diode <- Pololu VOUT.  ALL GROUNDS TOGETHER.

  Keys:
    g  start / stop cycling        x or space  STOP (motor off)
    l  one lift      o  one lower  (each stops on the switch or a timeout)
    [ ]  lift duty -/+ 10%         e  toggle elbow moves during cycles
    v  toggle the 100 Hz CSV log   s  stats      c  clear reset counters
*/

#include <Preferences.h>
#include "esp_system.h"

// ---------------- pins
const int PIN_VMON   = 1;
const int PIN_V5MON  = 10;
const int PIN_CS     = 2;
const int PIN_IN1    = 11;
const int PIN_IN2    = 12;
const int PIN_SW     = 5;
const int PIN_ELBOW  = 17;
const int PIN_PIR    = 9;
#ifdef RGB_BUILTIN
const int PIN_RGB    = RGB_BUILTIN;
#else
const int PIN_RGB    = 48;
#endif

// ---------------- tuning
const float DIV        = 69.0f / 22.0f;   // 47k/22k divider -> x3.136
float CS_MV_PER_A      = 1500.0f;         // DRV8876: 1000 uA/A x R_IPROPI. 1.5k -> 1500 mV/A. CHECK YOUR MODULE
int   liftDuty         = 50;              // %; stock lift is ~3V across the motor (~45%)
const int LOWER_DUTY   = 100;             // stock lowers at full drive
const uint32_t LIFT_EXTRA_MS   = 800;     // keep lifting after the switch goes high (stock ~1200)
const uint32_t LIFT_TIMEOUT_MS = 6000;
const uint32_t LOWER_TIMEOUT_MS = 4000;
const uint32_t HOLD_MS  = 1500;
const uint32_t REST_MS  = 2000;
const bool AUTORUN      = true;           // start cycling 5 s after boot (needed for the USB-unplugged pass)

// ---------------- elbow (right arm range; home -700)
const int ELBOW_HOME = -700, ELBOW_UP = 0;
bool elbowOn = false;

// ---------------- state
enum State { IDLE, LIFT, LIFT_EXTRA, HOLD, LOWER, REST };
State state = IDLE;
bool cycling = false, singleMove = false, logOn = true;
uint32_t stateStart = 0, cycles = 0, lastLog = 0, lastElbow = 0;
int elbowTarget = ELBOW_HOME;

// per-cycle extremes
float minVmot, minV5, maxAmps;
float allMinVmot = 99, allMinV5 = 99, allMaxAmps = 0;

Preferences prefs;
uint32_t boots = 0, brownouts = 0, otherResets = 0;

// ---------------------------------------------------------------- helpers
float readV(int pin) { return analogReadMilliVolts(pin) / 1000.0f * DIV; }
float readAmps()     { return analogReadMilliVolts(PIN_CS) / CS_MV_PER_A; }
bool  switchHigh()   { return digitalRead(PIN_SW) == HIGH; }

void rgb(uint8_t r, uint8_t g, uint8_t b) { rgbLedWrite(PIN_RGB, r, g, b); }

void motor(int in1Duty, int in2Duty) {     // duty in %, IN/IN mode
  ledcWrite(PIN_IN1, in1Duty * 255 / 100);
  ledcWrite(PIN_IN2, in2Duty * 255 / 100);
}
void motorOff() { motor(0, 0); }           // both low = coast

void elbowSend(int pos, int ms) {
  uint8_t f[9] = {0xAB, 0x01, 0x09, 0x41,
                  (uint8_t)(pos & 0xFF), (uint8_t)((pos >> 8) & 0xFF),
                  (uint8_t)(ms & 0xFF), (uint8_t)((ms >> 8) & 0xFF), 0};
  uint32_t c = 0xAF;
  for (int i = 0; i < 8; i++) c += (9 - i) * f[i];
  f[8] = c & 0xFF;
  Serial1.write(f, 9);
}

void resetExtremes() { minVmot = 99; minV5 = 99; maxAmps = 0; }

void enter(State s) {
  state = s;
  stateStart = millis();
  switch (s) {
    case LIFT:
      resetExtremes();
      motor(liftDuty, 0);
      if (elbowOn) { elbowTarget = ELBOW_UP; elbowSend(elbowTarget, 1500); }
      rgb(0, 0, 40);
      break;
    case LIFT_EXTRA: break;
    case HOLD:  motorOff(); break;
    case LOWER:
      motor(0, LOWER_DUTY);
      if (elbowOn) { elbowTarget = ELBOW_HOME; elbowSend(elbowTarget, 1500); }
      break;
    case REST:
      motorOff();
      cycles++;
      if (minVmot < allMinVmot) allMinVmot = minVmot;
      if (minV5 < allMinV5) allMinV5 = minV5;
      if (maxAmps > allMaxAmps) allMaxAmps = maxAmps;
      Serial.printf("# cycle %lu: VMOT min %.2fV | 5V min %.2fV | peak %.2fA %s\n",
                    cycles, minVmot, minV5, maxAmps, minV5 < 4.75f ? "<-- 5V SAGGED" : "ok");
      rgb(0, 20, 0);
      break;
    case IDLE:
      motorOff();
      rgb(0, 20, 0);
      break;
  }
}

// Start a cycle from wherever the arm is: if it's already raised, lower it first
// (LOWER -> REST -> LIFT) so the lift always starts from home.
void startCycle() {
  cycling = true; singleMove = false;
  Serial.println("# cycling");
  if (switchHigh()) { resetExtremes(); enter(LOWER); }
  else enter(LIFT);
}

void stopAll(const char *why) {
  cycling = false; singleMove = false;
  enter(IDLE);
  Serial.printf("# STOP (%s)\n", why);
}

void printStats() {
  Serial.printf("# boots since clear: %lu | brownout resets: %lu | other resets: %lu\n",
                boots, brownouts, otherResets);
  Serial.printf("# this boot: %lu cycles | VMOT min %.2fV | 5V min %.2fV | peak %.2fA\n",
                cycles, allMinVmot, allMinV5, allMaxAmps);
  Serial.printf("# lift duty %d%% | elbow %s | now VMOT %.2fV 5V %.2fV switch %s\n",
                liftDuty, elbowOn ? "on" : "off", readV(PIN_VMON), readV(PIN_V5MON),
                switchHigh() ? "HIGH (raised)" : "low (home)");
}

void blink(int n, uint8_t r, uint8_t g, uint8_t b) {
  for (int i = 0; i < n; i++) { rgb(r, g, b); delay(250); rgb(0, 0, 0); delay(250); }
}

// ---------------------------------------------------------------- setup
void setup() {
  motorOff();                                  // pins float until attached; attach first thing
  ledcAttach(PIN_IN1, 20000, 8);
  ledcAttach(PIN_IN2, 20000, 8);
  motorOff();

  pinMode(PIN_SW, INPUT);                      // external 100k pulldown
  analogSetAttenuation(ADC_11db);
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);                    // never block when USB is unplugged
  Serial1.begin(9600, SERIAL_8N1, -1, PIN_ELBOW);

  // reset bookkeeping
  esp_reset_reason_t why = esp_reset_reason();
  prefs.begin("pwrtest", false);
  boots = prefs.getUInt("boots", 0) + 1;
  brownouts = prefs.getUInt("brown", 0);
  otherResets = prefs.getUInt("other", 0);
  if (why == ESP_RST_BROWNOUT) brownouts++;
  else if (why != ESP_RST_POWERON && why != ESP_RST_SW && why != ESP_RST_USB) otherResets++;
  prefs.putUInt("boots", boots);
  prefs.putUInt("brown", brownouts);
  prefs.putUInt("other", otherResets);

  delay(1500);                                 // let USB serial attach
  Serial.printf("\n# freddy_power_test | reset reason %d | boots since clear %lu | brownouts %lu | other %lu\n",
                (int)why, boots, brownouts, otherResets);
  Serial.println("# keys: g start/stop, x stop, l lift, o lower, [ ] duty, e elbow, v log, s stats, c clear");
  Serial.println("t_ms,state,vmot,v5,amps,switch,pir_mV");

  // LED report: one red blink per boot since the counters were cleared, then
  // yellow blinks for brownouts. A clean run shows exactly ONE red blink.
  blink(boots > 10 ? 10 : boots, 40, 0, 0);
  delay(500);
  blink(brownouts > 10 ? 10 : brownouts, 40, 25, 0);

  if (AUTORUN) {
    Serial.println("# autorun: cycling starts in 5 s (press x to cancel)");
    uint32_t t0 = millis();
    while (millis() - t0 < 5000) {
      rgb(0, 0, ((millis() / 250) % 2) ? 30 : 0);
      if (Serial.available()) { Serial.read(); Serial.println("# autorun cancelled"); enter(IDLE); return; }
    }
    startCycle();
  } else {
    enter(IDLE);
  }
}

// ---------------------------------------------------------------- loop
void loop() {
  uint32_t now = millis(), inState = now - stateStart;

  // keys
  while (Serial.available()) {
    char k = Serial.read();
    switch (k) {
      case 'x': case ' ': stopAll("key"); break;
      case 'g':
        if (cycling) stopAll("g");
        else startCycle();
        break;
      case 'l':
        if (state != IDLE) break;
        if (switchHigh()) Serial.println("# already raised (switch high): press o to lower first");
        else { singleMove = true; enter(LIFT); }
        break;
      case 'o': if (state == IDLE) { singleMove = true; enter(LOWER); } break;
      case '[': liftDuty = max(20, liftDuty - 10); Serial.printf("# lift duty %d%%\n", liftDuty); break;
      case ']': liftDuty = min(100, liftDuty + 10); Serial.printf("# lift duty %d%%\n", liftDuty); break;
      case 'e': elbowOn = !elbowOn; Serial.printf("# elbow moves %s\n", elbowOn ? "ON (stock board must be disconnected)" : "off"); break;
      case 'v': logOn = !logOn; break;
      case 's': printStats(); break;
      case 'c':
        prefs.putUInt("boots", 0); prefs.putUInt("brown", 0); prefs.putUInt("other", 0);
        boots = 0; brownouts = 0; otherResets = 0;
        Serial.println("# reset counters cleared (the next boot counts as 1)");
        break;
    }
  }

  // measure
  float vmot = readV(PIN_VMON), v5 = readV(PIN_V5MON), amps = readAmps();
  if (state != IDLE && state != REST && state != HOLD) {
    if (vmot < minVmot) minVmot = vmot;
    if (v5 < minV5) minV5 = v5;
    if (amps > maxAmps) maxAmps = amps;
  }

  // state machine (stock logic: lift until the switch goes high, a bit more, hold, lower until low)
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

  // elbow keep-alive (the stock board resends about once a second)
  if (elbowOn && state != IDLE && now - lastElbow > 1000) {
    elbowSend(elbowTarget, 1500);
    lastElbow = now;
  }

  // 100 Hz CSV log
  if (logOn && Serial && now - lastLog >= 10) {
    lastLog = now;
    Serial.printf("%lu,%d,%.2f,%.2f,%.2f,%d,%lu\n", now, (int)state, vmot, v5, amps,
                  switchHigh() ? 1 : 0, analogReadMilliVolts(PIN_PIR));
  }
}
