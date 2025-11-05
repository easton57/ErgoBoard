/*
 * Split Keyboard - Left Side (Arduino Uno)
 * Main cluster: 4 rows × 6 columns
 * Thumb cluster: 1 row × 5 columns (left 5 columns only)
 * 
 * Pin Configuration:
 * Main Matrix:
 *   Rows (Outputs): 2, 3, 4, 5
 *   Columns (Inputs): 6, 7, 8, 9, 10, 11
 * Thumb Cluster:
 *   Row (Output): 12
 *   Columns (Inputs): 6, 7, 8, 9, 10 (shared with main matrix)
 * 
 * I2C Communication:
 *   SDA: 20 (dedicated I2C data line)
 *   SCL: 21 (dedicated I2C clock line)
 */

#include <Wire.h>

// Matrix dimensions
const int MAIN_ROWS = 5;
const int MAIN_COLS = 6;

// Pin definitions
const int mainRowPins[MAIN_ROWS] = {12, 5, 4, 3, 2};
const int mainColPins[MAIN_COLS] = {11, 10, 9, 8, 7, 6};

// Key matrix - define your keys here
const char mainKeyMap[MAIN_ROWS][MAIN_COLS] = {
  {'esc', '1', '2', '3', '4', '5'},
  {'tab', 'q', 'w', 'e', 'r', 't'},
  {'super', 'a', 's', 'd', 'f', 'g'},
  {'shift', 'z', 'x', 'c', 'v', 'b'},
  {'ctrl', 'alt', 'fn', ']', '[', '\ '}
};

// Modifier key mapping - these need special handling
const int mainModifierMap[MAIN_ROWS][MAIN_COLS] = {
  {0, 0, 0, 0, 0, 0},           // esc, 1, 2, 3, 4, 5
  {0, 0, 0, 0, 0, 0},           // tab, q, w, e, r, t
  {0, 0, 0, 0, 0, 0},           // super, a, s, d, f, g
  {0, 0, 0, 0, 0, 0},           // shift, z, x, c, v, b
  {0, 0, 0, 0, 0, 0}            // ctrl, alt, fn, ], [
};

// Modifier key definitions
#define MODIFIER_NONE     0
#define MODIFIER_CTRL     1
#define MODIFIER_SHIFT    2
#define MODIFIER_ALT      3
#define MODIFIER_SUPER    4
#define MODIFIER_FN       5

// Key states
bool mainKeyStates[MAIN_ROWS][MAIN_COLS] = {false};
bool lastMainKeyStates[MAIN_ROWS][MAIN_COLS] = {false};

// Modifier states
bool ctrlPressed = false;
bool shiftPressed = false;
bool altPressed = false;
bool superPressed = false;
bool fnPressed = false;

// Debounce delay
const unsigned long DEBOUNCE_DELAY = 5;

// I2C address for this device
const int I2C_ADDRESS = 0x08;

// Key data structure to send via I2C
struct KeyData {
  bool mainKeys[MAIN_ROWS][MAIN_COLS];
  bool modifiers[5];  // ctrl, shift, alt, super, fn
  unsigned long timestamp;
};

KeyData keyData;

void setup() {
  // Initialize main matrix row pins as outputs
  for (int i = 0; i < MAIN_ROWS; i++) {
    pinMode(mainRowPins[i], OUTPUT);
    digitalWrite(mainRowPins[i], HIGH);
  }
  
  // Initialize main matrix column pins as inputs with pull-up resistors
  for (int i = 0; i < MAIN_COLS; i++) {
    pinMode(mainColPins[i], INPUT_PULLUP);
  }
  
  // Initialize I2C communication on dedicated pins
  Wire.begin(I2C_ADDRESS);
  Wire.onRequest(requestEvent);
  
  // Debug output
  Serial.begin(9600);
  Serial.println("Left Split Keyboard - Arduino Uno");
  Serial.println("Main: 5 rows × 6 columns");
  Serial.println("I2C Address: 0x08");
}

void loop() {
  // Scan the main matrix
  scanMainMatrix();
  
  // Check for key state changes
  checkMainKeyChanges();
  
  // Update modifier states
  updateModifierStates();
  
  // Update key data structure
  updateKeyData();
  
  // Small delay for stability
  delay(1);
}

void scanMainMatrix() {
  // Scan each row
  for (int row = 0; row < MAIN_ROWS; row++) {
    // Set current row to LOW
    digitalWrite(mainRowPins[row], LOW);
    
    // Small delay for signal stability
    delayMicroseconds(10);
    
    // Read each column in the current row
    for (int col = 0; col < MAIN_COLS; col++) {
      // Read the column pin (LOW = pressed due to pull-up)
      bool isPressed = (digitalRead(mainColPins[col]) == LOW);
      
      // Apply debouncing
      if (isPressed != lastMainKeyStates[row][col]) {
        delay(DEBOUNCE_DELAY);
        isPressed = (digitalRead(mainColPins[col]) == LOW);
      }
      
      mainKeyStates[row][col] = isPressed;
    }
    
    // Set row back to HIGH
    digitalWrite(mainRowPins[row], HIGH);
  }
}

void checkMainKeyChanges() {
  for (int row = 0; row < MAIN_ROWS; row++) {
    for (int col = 0; col < MAIN_COLS; col++) {
      // Check if key state changed
      if (mainKeyStates[row][col] != lastMainKeyStates[row][col]) {
        if (mainKeyStates[row][col]) {
          // Key pressed
          char key = mainKeyMap[row][col];
          Serial.print("Main Pressed: ");
          Serial.println(key);
        } else {
          // Key released
          char key = mainKeyMap[row][col];
          Serial.print("Main Released: ");
          Serial.println(key);
        }
        
        // Update last state
        lastMainKeyStates[row][col] = mainKeyStates[row][col];
      }
    }
  }
}

void updateModifierStates() {
  // Check main matrix modifiers
  if (mainKeyStates[3][0]) {  // shift key
    shiftPressed = true;
  } else {
    shiftPressed = false;
  }
  
  if (mainKeyStates[2][0]) {  // super key
    superPressed = true;
  } else {
    superPressed = false;
  }
  
  // Check thumb cluster modifiers
  if (mainKeyStates[4][0]) {  // ctrl
    ctrlPressed = true;
  } else {
    ctrlPressed = false;
  }
  
  if (mainKeyStates[4][1]) {  // alt
    altPressed = true;
  } else {
    altPressed = false;
  }
  
  if (mainKeyStates[4][2]) {  // fn
    fnPressed = true;
  } else {
    fnPressed = false;
  }
}
void updateKeyData() {
  // Copy main key states
  for (int row = 0; row < MAIN_ROWS; row++) {
    for (int col = 0; col < MAIN_COLS; col++) {
      keyData.mainKeys[row][col] = mainKeyStates[row][col];
    }
  }
  
  // Copy modifier states
  keyData.modifiers[0] = ctrlPressed;
  keyData.modifiers[1] = shiftPressed;
  keyData.modifiers[2] = altPressed;
  keyData.modifiers[3] = superPressed;
  keyData.modifiers[4] = fnPressed;
  
  // Update timestamp
  keyData.timestamp = millis();
}

// I2C request event - send key data when ESP32 requests it
void requestEvent() {
  //Serial.print("Data Requested");
  Wire.write((uint8_t*)&keyData, sizeof(keyData));
}

