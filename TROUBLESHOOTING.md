# 🔧 Troubleshooting Guide
## Intelligent Automatic Dustbin — Ultrasonic Sensors & Servo Control

---

## 📋 Table of Contents

1. [Lid / Servo Issues](#1-lid--servo-issues)
2. [Ultrasonic Sensor Issues](#2-ultrasonic-sensor-issues)
3. [LED Indicator Issues](#3-led-indicator-issues)
4. [FSM / Logic Issues](#4-fsm--logic-issues)
5. [Power Issues](#5-power-issues)
6. [ESP32 Upload Issues](#6-esp32-upload-issues)
7. [General Tuning Tips](#7-general-tuning-tips)

---

## 1. Lid / Servo Issues

### ❌ Lid doesn't open when user approaches
**Fix:**
- Open Serial Monitor (115200 baud) — check if `USER_DISTANCE` is being read
- Confirm user sensor (Sensor 1) is on the **external front panel** at ~15cm height
- Verify `consecutiveUserDetections` reaches 2 (requires 2 consecutive readings ≤25cm)
- Reduce `USER_DISTANCE` threshold if users must approach too close:
  ```cpp
  const int USER_DISTANCE = 30;  // Increase range
  ```

---

### ❌ Lid opens but won't close
**Cause:** `STATE_SAFETY_CHECK` detecting an object in closure path.
**Fix:**
- Clear the area in front of the sensor before lid closes
- Check Serial Monitor for "Obstruction detected" messages
- If no obstruction exists, user sensor may be misfiring — check wiring
- Reduce `SAFETY_CLEAR_DISTANCE` slightly:
  ```cpp
  const int SAFETY_CLEAR_DISTANCE = 25;  // Was 30
  ```

---

### ❌ Servo jitters or makes grinding noise
**Cause:** Insufficient power or PWM conflict.
**Fix:**
- Power servo directly from 5V supply, NOT from ESP32's 3.3V pin
- Add a **100µF–470µF capacitor** across servo VCC/GND rails
- Confirm `ESP32Servo` library is installed (not the standard `Servo.h`)

---

### ❌ Lid only partially opens or closes
**Cause:** Servo angle limits incorrect for your physical lid mechanism.
**Fix:**
- Adjust `LID_OPEN_ANGLE` in code:
  ```cpp
  const int LID_OPEN_ANGLE = 90;   // Try 80 or 100 for different lid designs
  ```
- Check servo horn is mounted at the correct neutral position before attaching lid

---

### ❌ Servo moves in reverse direction
**Fix:**
- Swap closed and open angles:
  ```cpp
  const int LID_CLOSED_ANGLE = 90;   // Reversed
  const int LID_OPEN_ANGLE   = 0;    // Reversed
  ```

---

## 2. Ultrasonic Sensor Issues

### ❌ Sensor always reads -1 (timeout)
**Cause:** Wiring error or sensor too far from any reflective surface.
**Fix:**
- Check TRIG and ECHO pins are not swapped
- Confirm VCC → 5V and GND → GND for both sensors
- Test with a flat surface placed directly in front of the sensor
- Increase `pulseIn()` timeout if testing at long range:
  ```cpp
  long duration = pulseIn(echoPin, HIGH, 60000);  // 60ms for longer range
  ```

---

### ❌ Fill sensor incorrectly triggers bin-full
**Cause:** Threshold too high or waste has irregular surface.
**Fix:**
- Adjust `FILL_THRESHOLD`:
  ```cpp
  const int FILL_THRESHOLD = 6;  // Stricter — requires waste closer to sensor
  ```
- Increase consecutive reading requirement:
  ```cpp
  if (consecutiveFillDetections >= 3)  // Was 2, now 3
  ```

---

### ❌ User sensor triggering from objects passing by
**Cause:** Threshold too wide or sensor angle too broad.
**Fix:**
- Reduce `USER_DISTANCE`:
  ```cpp
  const int USER_DISTANCE = 20;  // Tighter detection zone
  ```
- Add a cardboard baffle/funnel around the sensor to narrow its beam
- Increase debounce count:
  ```cpp
  if (consecutiveUserDetections >= 3)  // Was 2, now 3
  ```

---

### ❌ Both sensors interfering with each other
**Cause:** Both sensors firing simultaneously — acoustic crosstalk.
**Fix:**
- In `measureDistance()`, add a longer delay between sensors:
  ```cpp
  delay(20);  // Increase from 10ms to 20ms between samples
  ```
- Angle sensors slightly away from each other to reduce cross-reflection

---

## 3. LED Indicator Issues

### ❌ No LEDs light up
**Fix:**
- Verify 220Ω resistors are in series with each LED
- Check LED polarity — anode (long leg) → resistor → GPIO, cathode (short leg) → GND
- Test each LED: `digitalWrite(LED_GREEN, HIGH)` in `setup()`
- Confirm GPIO 15, 2, and 4 are being used (not ADC2 conflict with GPIO 2 on some ESP32 boards)

---

### ❌ GPIO 2 (Yellow LED) doesn't work reliably
**Note:** GPIO 2 is connected to the onboard LED on many ESP32 boards and may behave differently.
**Fix:**
- Change Yellow LED to GPIO 25 or GPIO 26:
  ```cpp
  const int LED_YELLOW = 26;  // Alternative pin
  ```

---

### ❌ Red LED never turns ON
**Cause:** Bin never reaches fill threshold.
**Fix:**
- Place a flat object (cardboard) inside the bin ≤8cm from the internal sensor
- Open Serial Monitor and check `fillDistance` readings
- Lower `FILL_THRESHOLD`:
  ```cpp
  const int FILL_THRESHOLD = 10;  // Trigger earlier
  ```

---

## 4. FSM / Logic Issues

### ❌ System stuck in STATE_SAFETY_CHECK forever
**Cause:** User sensor reading ≤30cm (SAFETY_CLEAR_DISTANCE) continuously.
**Fix:**
- Clear the area completely in front of the user sensor
- Check Serial Monitor — distance reading should exceed 30cm for path clear
- Reduce safety margin:
  ```cpp
  const int SAFETY_CLEAR_DISTANCE = 25;  // Less strict
  ```

---

### ❌ Lid keeps re-opening immediately after closing
**Cause:** User still within detection range when system returns to IDLE.
**Fix:**
- Add a post-close cooldown in `STATE_IDLE`:
  ```cpp
  // In STATE_IDLE, check if stateStartTime is recent
  if (currentTime - stateStartTime < 1000) break;  // 1 second cooldown
  ```

---

### ❌ System not exiting STATE_BIN_FULL after emptying
**Cause:** Fill sensor still reading ≤8cm — bin not fully emptied or sensor miscalibrated.
**Fix:**
- Ensure bin is visibly empty — fill sensor should read a large distance (e.g., 20cm+)
- Adjust threshold:
  ```cpp
  const int FILL_THRESHOLD = 6;  // Requires bin to be more empty before reset
  ```

---

## 5. Power Issues

### ❌ ESP32 resets when servo activates
**Cause:** Servo startup current exceeds USB power capacity.
**Fix:**
- Use a dedicated **5V/2A power adapter** instead of USB for full system power
- Add **470µF capacitor** across 5V and GND rails near the servo connector
- Power ESP32 and servo from separate rails with common ground

---

### ❌ Sensor readings erratic when servo moves
**Cause:** Servo PWM causing electrical noise on sensor lines.
**Fix:**
- Add 100nF decoupling capacitors between VCC and GND of each HC-SR04
- Run sensor signal wires away from servo power/signal wires
- Use ferrite beads on servo power lines if available

---

## 6. ESP32 Upload Issues

### ❌ "Failed to connect to ESP32"
**Fix:**
- Hold **BOOT button** on ESP32 while clicking Upload, release after connection
- Select **Tools → Board → ESP32 Dev Module**
- Try a different USB cable (data cable, not charge-only)
- Install CH340 drivers if using a clone ESP32

---

### ❌ ESP32Servo library not found
**Fix:**
- **Sketch → Include Library → Manage Libraries**
- Search **"ESP32Servo"** by Kevin Harrington → Install
- Do NOT use the standard `Servo.h` — it is not compatible with ESP32

---

### ❌ GPIO 2 causes upload issues
**Note:** GPIO 2 must be LOW during upload on some ESP32 boards.
**Fix:**
- Disconnect the Yellow LED from GPIO 2 during upload
- Or change `LED_YELLOW` to GPIO 26 permanently

---

## 7. General Tuning Tips

| Tip | Details |
|-----|---------|
| 📋 Serial Monitor | Run at 115200 baud — every state change and sensor reading is logged |
| 📏 Sensor placement | Sensor 1: front panel ~15cm height. Sensor 2: inside top center, pointing down |
| 🎚️ Threshold tuning | Start with defaults, then adjust based on Serial Monitor readings |
| 🔋 Stable power | Always use 5V/2A supply — USB is insufficient for servo + ESP32 + sensors |
| 🏗️ Rigid mounting | Secure both sensors firmly — vibration causes inconsistent readings |
| 🧪 Test states one by one | Comment out other states to isolate and debug individual FSM states |
| ⏱️ Timing adjustments | Increase `LID_OPEN_DURATION` if users need more time for disposal |
| 🔆 Indoor testing | HC-SR04 is unaffected by normal room lighting — reliable indoors |

---

## 📬 Still Having Issues?

- [HC-SR04 Datasheet](https://www.handsontec.com/dataspecs/HC-SR04-User-Guide.pdf)
- [ESP32 GPIO Reference](https://randomnerdtutorials.com/esp32-pinout-reference-gpios/)
- [ESP32Servo Library](https://github.com/madhephaestus/ESP32Servo)
- [MG90s Servo Datasheet](https://www.electronicoscaldas.com/datasheet/MG90S_Tower-Pro.pdf)

---

*Troubleshooting Guide — Intelligent Automatic Dustbin*
*Authors: Muhammad Mamoon (L1F23BSRI0014) | Muhammad Asad Ali (L1F23BSRI0024)*
*Sensors & Actuators — UCP Fall 2025*
