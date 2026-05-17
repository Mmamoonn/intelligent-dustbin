#include <ESP32Servo.h>

// ==================== PIN DEFINITIONS ====================
#define TRIG_USER 5
#define ECHO_USER 18
#define TRIG_FILL 12
#define ECHO_FILL 14
#define SERVO_PIN 13
#define GREEN_LED 15
#define YELLOW_LED 2
#define RED_LED 4
// ==================== CONFIGURATION ====================
// Distance thresholds (cm)
const int USER_DETECT_DISTANCE = 25;
const int BIN_FULL_DISTANCE = 8;
const int SAFETY_CLEAR_DISTANCE = 30;

// Timing constants (milliseconds)
const unsigned long LID_OPEN_DURATION = 1200;
const unsigned long YELLOW_WARNING_TIME = 500;
const unsigned long SAFETY_RECHECK_INTERVAL = 250;
const unsigned long SENSOR_READ_INTERVAL = 150;
const unsigned long USER_DEBOUNCE_TIME = 300;

// Servo angles
const int LID_OPEN_ANGLE = 90;
const int LID_CLOSE_ANGLE = 0;

// Sensor filtering
const int SENSOR_SAMPLES = 3;
const int SENSOR_TIMEOUT = 30000; // 30ms timeout for ultrasonic
const int MAX_VALID_DISTANCE = 200; // Max reasonable distance (cm)
const int CONSECUTIVE_READS = 2; // Readings needed to confirm state change

// ==================== SYSTEM STATE MACHINE ====================
enum SystemState {
  STATE_IDLE,
  STATE_LID_OPENING,
  STATE_LID_OPEN,
  STATE_WARNING,
  STATE_SAFETY_CHECK,
  STATE_LID_CLOSING,
  STATE_BIN_FULL
};

// ==================== GLOBAL OBJECTS & VARIABLES ====================
Servo lidServo;
SystemState currentState = STATE_IDLE;

// State tracking
bool isBinFull = false;
bool isLidOpen = false;
int consecutiveFullReadings = 0;
int consecutiveEmptyReadings = 0;
int consecutiveUserReadings = 0;
int consecutiveNoUserReadings = 0;

// Timing variables
unsigned long stateStartTime = 0;
unsigned long lastSensorReadTime = 0;
unsigned long lastUserDetectTime = 0;

// Distance cache
long lastUserDistance = 999;
long lastFillDistance = 999;

// ==================== UTILITY FUNCTIONS ====================

/**
 * Enhanced distance measurement with noise filtering and error handling
 */
long measureDistance(int trigPin, int echoPin) {
  long sum = 0;
  int validReadings = 0;
  
  for (int i = 0; i < SENSOR_SAMPLES; i++) {
    // Trigger pulse
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);
    
    // Measure echo with timeout
    long duration = pulseIn(echoPin, HIGH, SENSOR_TIMEOUT);
    
    if (duration > 0) {
      long distance = (duration * 0.034) / 2;
      
      // Validate reading
      if (distance > 0 && distance < MAX_VALID_DISTANCE) {
        sum += distance;
        validReadings++;
      }
    }
    
    delay(10); // Small delay between readings
  }
  
  // Return average of valid readings, or error code
  if (validReadings > 0) {
    return sum / validReadings;
  }
  return -1; // Error: no valid readings
}

/**
 * Check if user is present with debouncing
 */
bool isUserPresent() {
  long distance = measureDistance(TRIG_USER, ECHO_USER);
  
  if (distance > 0 && distance <= USER_DETECT_DISTANCE) {
    consecutiveUserReadings++;
    consecutiveNoUserReadings = 0;
    if (consecutiveUserReadings >= CONSECUTIVE_READS) {
      lastUserDistance = distance;
      return true;
    }
  } else {
    consecutiveNoUserReadings++;
    consecutiveUserReadings = 0;
  }
  
  return false;
}

/**
 * Check if bin is full with debouncing
 */
bool checkBinFull() {
  long distance = measureDistance(TRIG_FILL, ECHO_FILL);
  
  if (distance > 0 && distance <= BIN_FULL_DISTANCE) {
    consecutiveFullReadings++;
    consecutiveEmptyReadings = 0;
    if (consecutiveFullReadings >= CONSECUTIVE_READS) {
      lastFillDistance = distance;
      return true;
    }
  } else if (distance > BIN_FULL_DISTANCE) {
    consecutiveEmptyReadings++;
    consecutiveFullReadings = 0;
    if (consecutiveEmptyReadings >= CONSECUTIVE_READS) {
      lastFillDistance = distance;
      return false;
    }
  }
  
  return isBinFull; // Maintain current state if uncertain
}

/**
 * Control servo with smooth movement
 */
void setLidPosition(int angle) {
  lidServo.write(angle);
  isLidOpen = (angle == LID_OPEN_ANGLE);
}

/**
 * Update LED indicators based on state
 */
void updateLEDs(bool green, bool yellow, bool red) {
  digitalWrite(GREEN_LED, green ? HIGH : LOW);
  digitalWrite(YELLOW_LED, yellow ? HIGH : LOW);
  digitalWrite(RED_LED, red ? HIGH : LOW);
}

/**
 * Transition to new state
 */
void changeState(SystemState newState) {
  currentState = newState;
  stateStartTime = millis();
  
  // Debug output
  Serial.print("State: ");
  switch(newState) {
    case STATE_IDLE: Serial.println("IDLE"); break;
    case STATE_LID_OPENING: Serial.println("LID_OPENING"); break;
    case STATE_LID_OPEN: Serial.println("LID_OPEN"); break;
    case STATE_WARNING: Serial.println("WARNING"); break;
    case STATE_SAFETY_CHECK: Serial.println("SAFETY_CHECK"); break;
    case STATE_LID_CLOSING: Serial.println("LID_CLOSING"); break;
    case STATE_BIN_FULL: Serial.println("BIN_FULL"); break;
  }
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Intelligent Dustbin System Initializing ===");
  
  // Configure pins
  pinMode(TRIG_USER, OUTPUT);
  pinMode(ECHO_USER, INPUT);
  pinMode(TRIG_FILL, OUTPUT);
  pinMode(ECHO_FILL, INPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(YELLOW_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  
  // Initialize servo
  ESP32PWM::allocateTimer(0);
  lidServo.setPeriodHertz(50);
  lidServo.attach(SERVO_PIN, 500, 2400);
  setLidPosition(LID_CLOSE_ANGLE);
  
  // Initial LED state
  updateLEDs(true, false, false);
  
  Serial.println("=== System Ready ===\n");
  
  // Initial sensor calibration
  delay(500);
  lastFillDistance = measureDistance(TRIG_FILL, ECHO_FILL);
  Serial.print("Initial fill distance: ");
  Serial.print(lastFillDistance);
  Serial.println(" cm");
}

// ==================== MAIN LOOP ====================
void loop() {
  unsigned long currentTime = millis();
  
  // Periodic sensor monitoring (non-blocking)
  if (currentTime - lastSensorReadTime >= SENSOR_READ_INTERVAL) {
    lastSensorReadTime = currentTime;
    
    // Always monitor bin fill level
    bool wasFull = isBinFull;
    isBinFull = checkBinFull();
    
    // If bin just became full, transition to full state
    if (!wasFull && isBinFull) {
      Serial.println("! BIN FULL DETECTED !");
      changeState(STATE_BIN_FULL);
    }
    // If bin was full and now has space
    else if (wasFull && !isBinFull) {
      Serial.println("Bin has capacity again");
      changeState(STATE_IDLE);
    }
  }
  
  // ==================== STATE MACHINE ====================
  switch (currentState) {
    
    // ------------------ IDLE STATE ------------------
    case STATE_IDLE:
      updateLEDs(true, false, false); // Green LED on
      
      if (isBinFull) {
        changeState(STATE_BIN_FULL);
      }
      else if (isUserPresent()) {
        if (currentTime - lastUserDetectTime > USER_DEBOUNCE_TIME) {
          lastUserDetectTime = currentTime;
          Serial.println("User detected - Opening lid");
          changeState(STATE_LID_OPENING);
        }
      }
      break;
    
    // ------------------ LID OPENING ------------------
    case STATE_LID_OPENING:
      updateLEDs(true, false, false);
      setLidPosition(LID_OPEN_ANGLE);
      changeState(STATE_LID_OPEN);
      break;
    
    // ------------------ LID OPEN ------------------
    case STATE_LID_OPEN:
      updateLEDs(true, false, false);
      
      // Wait for disposal time
      if (currentTime - stateStartTime >= LID_OPEN_DURATION) {
        Serial.println("Hold time complete - Starting warning");
        changeState(STATE_WARNING);
      }
      // Emergency close if bin becomes full
      else if (isBinFull) {
        Serial.println("Bin full during disposal - Closing");
        changeState(STATE_WARNING);
      }
      break;
    
    // ------------------ WARNING PHASE ------------------
    case STATE_WARNING:
      updateLEDs(false, true, false); // Yellow LED on
      
      if (currentTime - stateStartTime >= YELLOW_WARNING_TIME) {
        changeState(STATE_SAFETY_CHECK);
      }
      break;
    
    // ------------------ SAFETY CHECK ------------------
    case STATE_SAFETY_CHECK: {
      updateLEDs(false, true, false); // Yellow LED stays on
      
      // Check for obstruction
      long checkDist = measureDistance(TRIG_USER, ECHO_USER);
      
      if (checkDist < 0) {
        // Sensor error - wait and retry
        Serial.println("Sensor read error - retrying");
        delay(SAFETY_RECHECK_INTERVAL);
      }
      else if (checkDist > USER_DETECT_DISTANCE || checkDist > SAFETY_CLEAR_DISTANCE) {
        // Path is clear - safe to close
        Serial.println("Path clear - Closing lid");
        changeState(STATE_LID_CLOSING);
      }
      else {
        // Obstruction detected - keep waiting
        Serial.print("Obstruction at ");
        Serial.print(checkDist);
        Serial.println(" cm - Waiting");
        delay(SAFETY_RECHECK_INTERVAL);
      }
      break;
    }
    
    // ------------------ LID CLOSING ------------------
    case STATE_LID_CLOSING:
      updateLEDs(false, true, false);
      setLidPosition(LID_CLOSE_ANGLE);
      delay(500); // Allow servo to complete movement
      
      Serial.println("Lid closed safely\n");
      changeState(STATE_IDLE);
      break;
    
    // ------------------ BIN FULL ------------------
    case STATE_BIN_FULL:
      updateLEDs(false, false, true); // Red LED on
      
      // Close lid if it's open
      if (isLidOpen) {
        Serial.println("Closing lid - Bin full");
        setLidPosition(LID_CLOSE_ANGLE);
      }
      
      // Stay in this state until bin is emptied
      if (!isBinFull) {
        changeState(STATE_IDLE);
      }
      break;
  }
  
  // Small delay to prevent CPU hogging
  delay(10);
}