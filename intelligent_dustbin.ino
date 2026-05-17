/*
 * ============================================================
 * Intelligent Automatic Dustbin
 * Using Ultrasonic Sensors and Servo-Based Control
 * ============================================================
 * University  : University of Central Punjab (UCP)
 * Course      : Sensors & Actuators (RME321-F25-BS-RI-F23-A)
 * Semester    : 5th — BS Robotics & Intelligent Systems
 * Authors     : Muhammad Mamoon  (L1F23BSRI0014)
 *               Muhammad Asad Ali (L1F23BSRI0024)
 * Submission  : January 22, 2026
 * ============================================================
 *
 * Description:
 * Non-blocking 7-state FSM controlling an automatic dustbin.
 * Two HC-SR04 sensors: Sensor 1 detects user proximity,
 * Sensor 2 monitors bin fill level. MG90s servo opens/closes
 * the lid. Three LEDs provide real-time visual feedback.
 *
 * FSM States:
 *   STATE_IDLE         → Monitor, Green LED, lid closed
 *   STATE_LID_OPENING  → Send open command to servo
 *   STATE_LID_OPEN     → Hold open 1200ms, Green LED
 *   STATE_WARNING      → Yellow LED warning 500ms
 *   STATE_SAFETY_CHECK → Verify path clear before closing
 *   STATE_LID_CLOSING  → Close lid, Yellow LED, 500ms settle
 *   STATE_BIN_FULL     → Red LED, lid locked out
 *
 * Pin Map:
 *   TRIG_USER  = GPIO 5   | ECHO_USER  = GPIO 18
 *   TRIG_FILL  = GPIO 12  | ECHO_FILL  = GPIO 14
 *   SERVO_PIN  = GPIO 13
 *   LED_GREEN  = GPIO 15  | LED_YELLOW = GPIO 2
 *   LED_RED    = GPIO 4
 * ============================================================
 */

#include <ESP32Servo.h>

// ── Pin Definitions ───────────────────────────────────────────
// Ultrasonic Sensor 1 — User Detection (External)
const int TRIG_USER = 5;
const int ECHO_USER = 18;

// Ultrasonic Sensor 2 — Fill Level (Internal, pointing down)
const int TRIG_FILL = 12;
const int ECHO_FILL = 14;

// Servo Motor
const int SERVO_PIN = 13;

// LED Indicators
const int LED_GREEN  = 15;   // Normal operation
const int LED_YELLOW = 2;    // Warning / closing
const int LED_RED    = 4;    // Bin full

// ── System Constants ──────────────────────────────────────────
const int   USER_DISTANCE         = 25;    // cm — user detection threshold
const int   FILL_THRESHOLD        = 8;     // cm — bin full threshold (~85% capacity)
const int   SAFETY_CLEAR_DISTANCE = 30;    // cm — safe margin for lid closing
const long  LID_OPEN_DURATION     = 1200;  // ms — disposal window
const long  YELLOW_WARNING_TIME   = 500;   // ms — pre-close warning
const long  SAFETY_RECHECK        = 250;   // ms — obstruction recheck interval
const long  SENSOR_READ_INTERVAL  = 150;   // ms — polling frequency
const long  USER_DEBOUNCE_TIME    = 300;   // ms — detection confirmation
const int   LID_CLOSED_ANGLE      = 0;     // degrees
const int   LID_OPEN_ANGLE        = 90;    // degrees

// ── FSM State Definitions ─────────────────────────────────────
enum State {
  STATE_IDLE,
  STATE_LID_OPENING,
  STATE_LID_OPEN,
  STATE_WARNING,
  STATE_SAFETY_CHECK,
  STATE_LID_CLOSING,
  STATE_BIN_FULL
};

// ── Global Variables ──────────────────────────────────────────
State currentState    = STATE_IDLE;
long  stateStartTime  = 0;
long  lastSensorRead  = 0;
long  lastSafetyCheck = 0;

// Debounce counters
int consecutiveUserDetections   = 0;
int consecutiveNoUserDetections = 0;
int consecutiveFillDetections   = 0;

// Sensor readings cache
float userDistance = 999.0;
float fillDistance = 999.0;

Servo lidServo;

// ── Setup ─────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Intelligent Automatic Dustbin ===");
  Serial.println("Authors: Muhammad Mamoon | Muhammad Asad Ali");
  Serial.println("UCP — Sensors & Actuators — Fall 2025\n");

  // Ultrasonic sensor pins
  pinMode(TRIG_USER, OUTPUT);
  pinMode(ECHO_USER, INPUT);
  pinMode(TRIG_FILL, OUTPUT);
  pinMode(ECHO_FILL, INPUT);

  // LED pins
  pinMode(LED_GREEN,  OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_RED,    OUTPUT);

  // Servo initialization
  lidServo.attach(SERVO_PIN);
  lidServo.write(LID_CLOSED_ANGLE);
  delay(500);

  // Boot indication — flash all LEDs once
  allLEDsOn();  delay(500);
  allLEDsOff(); delay(200);

  changeState(STATE_IDLE);
  Serial.println("System ready. Monitoring...\n");
}

// ── Main Loop — Non-Blocking FSM ─────────────────────────────
void loop() {
  long currentTime = millis();

  // ── Periodic Sensor Reading ──────────────────────────────────
  if (currentTime - lastSensorRead >= SENSOR_READ_INTERVAL) {
    lastSensorRead  = currentTime;
    userDistance    = measureDistance(TRIG_USER, ECHO_USER);
    fillDistance    = measureDistance(TRIG_FILL, ECHO_FILL);
    updateDebounceCounters();
  }

  // ── FSM State Execution ───────────────────────────────────────
  switch (currentState) {

    case STATE_IDLE:
      setLEDs(HIGH, LOW, LOW);   // Green ON

      // Check bin full first
      if (isBinFull()) {
        changeState(STATE_BIN_FULL);

      // Check user presence
      } else if (isUserPresent()) {
        Serial.println("User detected → Opening lid");
        changeState(STATE_LID_OPENING);
      }
      break;

    // ──────────────────────────────────────────────────────────
    case STATE_LID_OPENING:
      setLEDs(HIGH, LOW, LOW);   // Green ON
      lidServo.write(LID_OPEN_ANGLE);
      Serial.println("Lid opening → 90°");
      changeState(STATE_LID_OPEN);
      break;

    // ──────────────────────────────────────────────────────────
    case STATE_LID_OPEN:
      setLEDs(HIGH, LOW, LOW);   // Green ON

      // Emergency: bin fills up while open
      if (isBinFull()) {
        Serial.println("Bin full detected while open → Warning");
        changeState(STATE_WARNING);
        break;
      }

      // Timer: close after disposal window
      if (currentTime - stateStartTime >= LID_OPEN_DURATION) {
        Serial.println("Disposal window expired → Warning");
        changeState(STATE_WARNING);
      }
      break;

    // ──────────────────────────────────────────────────────────
    case STATE_WARNING:
      setLEDs(LOW, HIGH, LOW);   // Yellow ON

      if (currentTime - stateStartTime >= YELLOW_WARNING_TIME) {
        Serial.println("Warning done → Safety check");
        changeState(STATE_SAFETY_CHECK);
      }
      break;

    // ──────────────────────────────────────────────────────────
    case STATE_SAFETY_CHECK:
      setLEDs(LOW, HIGH, LOW);   // Yellow ON

      if (currentTime - lastSafetyCheck >= SAFETY_RECHECK) {
        lastSafetyCheck = currentTime;

        float safetyDist = measureDistance(TRIG_USER, ECHO_USER);

        if (safetyDist < 0) {
          // Sensor error — retry
          Serial.println("Sensor error during safety check, retrying...");

        } else if (safetyDist <= SAFETY_CLEAR_DISTANCE) {
          // Obstruction detected — wait
          Serial.print("Obstruction at ");
          Serial.print(safetyDist);
          Serial.println("cm — holding...");

        } else {
          // Path clear — safe to close
          Serial.println("Path clear → Closing lid");
          changeState(STATE_LID_CLOSING);
        }
      }
      break;

    // ──────────────────────────────────────────────────────────
    case STATE_LID_CLOSING:
      setLEDs(LOW, HIGH, LOW);   // Yellow ON
      lidServo.write(LID_CLOSED_ANGLE);
      Serial.println("Lid closing → 0°");
      delay(500);   // Allow servo to complete travel
      Serial.println("Lid closed ✓ → IDLE");
      changeState(STATE_IDLE);
      break;

    // ──────────────────────────────────────────────────────────
    case STATE_BIN_FULL:
      setLEDs(LOW, LOW, HIGH);   // Red ON

      // If bin was open when full was detected, close immediately
      lidServo.write(LID_CLOSED_ANGLE);

      // Wait until bin is emptied
      if (!isBinFull()) {
        Serial.println("Bin emptied → Returning to IDLE");
        changeState(STATE_IDLE);
      }
      break;
  }
}

// ── Helper: Change FSM State ──────────────────────────────────
void changeState(State newState) {
  currentState   = newState;
  stateStartTime = millis();

  Serial.print("→ State: ");
  Serial.println(stateName(newState));
}

// ── Helper: Measure Distance (cm) ────────────────────────────
// Returns -1 on timeout/error
float measureDistance(int trigPin, int echoPin) {
  float total = 0;
  int   valid = 0;

  for (int i = 0; i < 3; i++) {
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);

    long duration = pulseIn(echoPin, HIGH, 30000);  // 30ms timeout

    if (duration > 0) {
      float dist = (duration * 0.0343) / 2.0;
      if (dist >= 0 && dist <= 200) {
        total += dist;
        valid++;
      }
    }
    delay(10);
  }

  return (valid > 0) ? (total / valid) : -1.0;
}

// ── Helper: Update Debounce Counters ─────────────────────────
void updateDebounceCounters() {
  // User detection debounce
  if (userDistance > 0 && userDistance <= USER_DISTANCE) {
    consecutiveUserDetections++;
    consecutiveNoUserDetections = 0;
  } else {
    consecutiveNoUserDetections++;
    consecutiveUserDetections = 0;
  }

  // Fill level debounce
  if (fillDistance > 0 && fillDistance <= FILL_THRESHOLD) {
    consecutiveFillDetections++;
  } else {
    consecutiveFillDetections = 0;
  }
}

// ── Helper: User present? (debounced) ────────────────────────
bool isUserPresent() {
  return (consecutiveUserDetections >= 2);
}

// ── Helper: Bin full? (debounced) ────────────────────────────
bool isBinFull() {
  return (consecutiveFillDetections >= 2);
}

// ── Helper: Set all LEDs ──────────────────────────────────────
void setLEDs(int green, int yellow, int red) {
  digitalWrite(LED_GREEN,  green);
  digitalWrite(LED_YELLOW, yellow);
  digitalWrite(LED_RED,    red);
}

void allLEDsOn()  { setLEDs(HIGH, HIGH, HIGH); }
void allLEDsOff() { setLEDs(LOW,  LOW,  LOW);  }

// ── Helper: State name for debug ─────────────────────────────
String stateName(State s) {
  switch (s) {
    case STATE_IDLE:         return "IDLE";
    case STATE_LID_OPENING:  return "LID_OPENING";
    case STATE_LID_OPEN:     return "LID_OPEN";
    case STATE_WARNING:      return "WARNING";
    case STATE_SAFETY_CHECK: return "SAFETY_CHECK";
    case STATE_LID_CLOSING:  return "LID_CLOSING";
    case STATE_BIN_FULL:     return "BIN_FULL";
    default:                 return "UNKNOWN";
  }
}
