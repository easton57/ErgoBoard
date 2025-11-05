/*
 * Split Keyboard - Right Side (ESP32)
 * Main cluster: 5 rows × 6 columns (includes thumb cluster on row 4)
 * 
 * Pin Configuration:
 * Main Matrix:
 *   Rows (Outputs): 2, 4, 5, 18 (main), 19 (thumb cluster)
 *   Columns (Inputs): 23, 27, 26, 25, 33, 32 (avoiding I2C pins 21, 22)
 * 
 * I2C Communication: DISABLED for right-side-only development
 *   SDA: 21 (I2C0 SDA - default ESP32 I2C)
 *   SCL: 22 (I2C0 SCL - default ESP32 I2C)
 *   
 * Note: Pin 34 is INPUT-ONLY and cannot be used as OUTPUT (row pin)
 */

#include <BleKeyboard.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
// #include <Wire.h>  // I2C disabled for right-side-only development

// Create BLEKeyboard instance
BleKeyboard bleKeyboard("Right Split Keyboard", "Split Keyboard", 100);

// Special key constants for thumb keys
// These are NOT HID codes - they're just markers to identify special keys
// We'll convert them to proper HID codes in asciiToHIDKeycode()
#define KEY_DELETE    0xF0  // Custom marker for Delete key
#define KEY_HOME      0xF1  // Custom marker for Home key  
#define KEY_SPACE     0xF2  // Custom marker for Space key
#define KEY_BACKTICK  0xF3  // Custom marker for Backtick key
#define KEY_TILDE     0xF4  // Custom marker for Tilde key

// Matrix dimensions
const int MAIN_ROWS = 5;
const int MAIN_COLS = 6;

// Pin definitions
// Row pins: 2, 4, 5, 18 (main), 19 (thumb cluster)
const int mainRowPins[MAIN_ROWS] = {2, 4, 5, 18, 19};
const int mainColPins[MAIN_COLS] = {23, 27, 26, 25, 33, 32}; // Avoiding I2C pins

// I2C pins (default ESP32 I2C0) - DISABLED for right-side-only development
// const int I2C_SDA = 21; // I2C0 SDA
// const int I2C_SCL = 22; // I2C0 SCL

// Key matrix - define your keys here
const char mainKeyMap[MAIN_ROWS][MAIN_COLS] = {
  {'6', '7', '8', '9', '0', '-'},
  {'y', 'u', 'i', 'o', 'p', '='},
  {'h', 'j', 'k', 'l', ';', '\"'},
  {'n', 'm', ',', '.', '/', '\\'},
  {0 , '#', '`', KEY_DELETE, KEY_HOME, KEY_SPACE}
};

// Modifier key mapping - these need special handling
const int mainModifierMap[MAIN_ROWS][MAIN_COLS] = {
  {'^', '&', '*', '(', ')', '_'},           // 6, 7, 8, 9, 0, -
  {'Y', 'U', 'I', 'O', 'P', '+'},           // y, u, i, o, p, +
  {'H', 'J', 'K', 'L', ':', '\''},           // h, j, k, l, ;, "
  {'N', 'M', '<', '>', '?', '|'},            // n, m, ,, ., /, '\\'
  {0, '~', '@', KEY_DELETE, KEY_HOME, KEY_SPACE}
};

// Key states
bool mainKeyStates[MAIN_ROWS][MAIN_COLS] = {false};
bool lastMainKeyStates[MAIN_ROWS][MAIN_COLS] = {false};

// Right side modifier states
bool rightCtrlPressed = false;
bool rightShiftPressed = false;
bool rightAltPressed = false;
bool rightSuperPressed = false;
bool rightFnPressed = false;

// Left side key states (received via I2C)
bool leftMainKeyStates[4][6] = {false};
bool leftModifiers[5] = {false}; // ctrl, shift, alt, super, fn

// Debounce delay
const unsigned long DEBOUNCE_DELAY = 5;

// BLE variables for left side communication
BLEServer* pServer = nullptr;
BLECharacteristic* pDataCharacteristic; // For receiving data from left side
bool deviceConnected = false;
bool oldDeviceConnected = false;

// Track last sent keys to detect changes
char lastKeys[6] = {0};
uint8_t lastSpecialKeys[6] = {0};
uint8_t lastModifier = 0;

// Service and characteristic UUIDs for communication
#define SERVICE_UUID        "12345678-1234-1234-1234-123456789abc"
#define CHARACTERISTIC_UUID "87654321-4321-4321-4321-cba987654321"

// KeyData struct for bluetooth communication
struct KeyData {
  bool mainKeys[4][6];
  bool modifiers[5];  // ctrl, shift, alt, super, fn
  unsigned long timestamp;
};

KeyData leftKeyData;

// Forward declarations
char mapLeftSideKey(int row, int col);
String getKeyName(char key, int row = -1, int col = -1);
uint8_t asciiToHIDKeycode(char key);

// BLE Server callbacks for left side communication service
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("\n=== Left side ESP32 connected ===");
    }

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("\n=== Left side ESP32 disconnected ===");
    }
};

// BLE Characteristic callbacks for receiving data from left side
class MyCharacteristicCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String rxValue = pCharacteristic->getValue();
      
      if (rxValue.length() == sizeof(KeyData)) {
        // Copy received data to leftKeyData
        memcpy(&leftKeyData, rxValue.c_str(), sizeof(KeyData));
        
        // Update left side key states
        for (int row = 0; row < 4; row++) {
          for (int col = 0; col < 6; col++) {
            bool wasPressed = leftMainKeyStates[row][col];
            leftMainKeyStates[row][col] = leftKeyData.mainKeys[row][col];
            
            // Debug: Print left side key changes
            if (leftMainKeyStates[row][col] && !wasPressed) {
              Serial.print("Left Key PRESSED: Row ");
              Serial.print(row);
              Serial.print(", Col ");
              Serial.print(col);
              Serial.print(" -> ");
              Serial.println(mapLeftSideKey(row, col));
            } else if (!leftMainKeyStates[row][col] && wasPressed) {
              Serial.print("Left Key RELEASED: Row ");
              Serial.print(row);
              Serial.print(", Col ");
              Serial.print(col);
              Serial.print(" -> ");
              Serial.println(mapLeftSideKey(row, col));
            }
          }
        }
        
        // Update left side modifier states
        for (int i = 0; i < 5; i++) {
          bool wasActive = leftModifiers[i];
          leftModifiers[i] = leftKeyData.modifiers[i];
          
          // Debug: Print modifier changes
          if (leftModifiers[i] && !wasActive) {
            Serial.print("Left Modifier ACTIVATED: ");
            switch(i) {
              case 0: Serial.println("Ctrl"); break;
              case 1: Serial.println("Shift"); break;
              case 2: Serial.println("Alt"); break;
              case 3: Serial.println("Super/GUI"); break;
              case 4: Serial.println("Fn"); break;
            }
          } else if (!leftModifiers[i] && wasActive) {
            Serial.print("Left Modifier DEACTIVATED: ");
            switch(i) {
              case 0: Serial.println("Ctrl"); break;
              case 1: Serial.println("Shift"); break;
              case 2: Serial.println("Alt"); break;
              case 3: Serial.println("Super/GUI"); break;
              case 4: Serial.println("Fn"); break;
            }
          }
        }
      }
    }
};

void setup() {
  // Debug output (must be first for early debugging)
  Serial.begin(115200);
  delay(1000); // Give serial time to initialize
  Serial.println("\n\nRight Split Keyboard - ESP32");
  Serial.println("Main: 5 rows × 6 columns");
  
  // Initialize main matrix row pins as outputs
  for (int i = 0; i < MAIN_ROWS; i++) {
    pinMode(mainRowPins[i], OUTPUT);
    digitalWrite(mainRowPins[i], HIGH);
  }
  
  // Initialize main matrix column pins as inputs with pull-up
  for (int i = 0; i < MAIN_COLS; i++) {
    pinMode(mainColPins[i], INPUT_PULLUP);
  }
  
  // Initialize BLE Keyboard (this initializes BLEDevice internally)
  Serial.println("Starting BLE Keyboard...");
  bleKeyboard.begin();
  
  // Wait a bit for BLE to initialize
  delay(500);
  
  // Check initial connection status
  Serial.print("Initial BLE connection status: ");
  Serial.println(bleKeyboard.isConnected() ? "CONNECTED" : "NOT CONNECTED");
  
  // Note: BleKeyboard already initializes BLEDevice, so we need to wait
  // and then create a separate BLE server for left side communication
  // Create the BLE Server for left side communication
  // Note: We can't have two BLE devices with the same name, so we'll use
  // the existing BLEDevice instance that BleKeyboard created
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  
  // Create custom service for communication with left side
  BLEService *pCommService = pServer->createService(SERVICE_UUID);
  
  // Create characteristic for receiving data from left side
  pDataCharacteristic = pCommService->createCharacteristic(
                         CHARACTERISTIC_UUID,
                         BLECharacteristic::PROPERTY_READ |
                         BLECharacteristic::PROPERTY_WRITE
                       );
  pDataCharacteristic->setCallbacks(new MyCharacteristicCallbacks());
  
  // Start the communication service
  pCommService->start();
  
  // Start advertising for left side communication
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(pCommService->getUUID());
  pAdvertising->setScanResponse(true);
  BLEDevice::startAdvertising();
  
  Serial.println("BLE Keyboard ready - waiting for connection...");
  Serial.println("Scan for 'Right Split Keyboard' and connect");
}

void loop() {
  // Handle left side BLE connection state changes
  if (!deviceConnected && oldDeviceConnected) {
    delay(500);
    pServer->startAdvertising();
    Serial.println("Start advertising for left side");
    oldDeviceConnected = deviceConnected;
  }
  
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
    Serial.println("Left side ESP32 connected");
  }
  
  // Always scan the right side matrix
  scanMainMatrix();
  
  // Always check for key state changes (for logging/debugging)
  checkMainKeyChanges();
  
  // Update right side modifier states
  updateRightModifierStates();
  
  // Check BLE keyboard connection status
  // Note: isConnected() may be unreliable, so we'll try sending keys anyway
  static bool wasConnected = false;
  bool isConnected = bleKeyboard.isConnected();
  
  // Always try to process keys - the connection callback may not be working
  // but the connection might actually be active
  processAllKeys();
  
  // Still log connection status changes for debugging
  if (isConnected != wasConnected) {
    if (isConnected) {
      Serial.println("\n=== BLE Keyboard CONNECTED (status reported) ===");
    } else {
      Serial.println("\n=== BLE Keyboard DISCONNECTED (status reported) ===");
    }
    wasConnected = isConnected;
  }
  
  // Log status occasionally
  static unsigned long lastStatusTime = 0;
  if (millis() - lastStatusTime > 5000) {
    Serial.print("BLE Status: ");
    Serial.println(isConnected ? "Reported as CONNECTED" : "Reported as NOT CONNECTED (but trying anyway)");
    lastStatusTime = millis();
  }
  
  // Small delay to allow BLE stack to process events
  delay(10);
}

// Left side key data is received via BLE callbacks in MyCharacteristicCallbacks

void scanMainMatrix() {
  // Scan each row
  for (int row = 0; row < MAIN_ROWS; row++) {
    // Ensure all other rows are HIGH before scanning
    for (int i = 0; i < MAIN_ROWS; i++) {
      if (i != row) {
        digitalWrite(mainRowPins[i], HIGH);
      }
    }
    
    // Small delay for signal stability
    delayMicroseconds(5);
    
    // Set current row to LOW
    digitalWrite(mainRowPins[row], LOW);
    
    // Small delay for signal stability
    delayMicroseconds(10);
    
    // Read each column in the current row
    for (int col = 0; col < MAIN_COLS; col++) {
      // Read the column pin (LOW = pressed due to pull-up)
      bool isPressed = (digitalRead(mainColPins[col]) == LOW);
      
      // Apply debouncing - if state changed, verify it's stable
      if (isPressed != lastMainKeyStates[row][col]) {
        delay(DEBOUNCE_DELAY);
        // Read again to verify
        bool secondRead = (digitalRead(mainColPins[col]) == LOW);
        // Only accept change if both reads agree
        if (isPressed == secondRead) {
          mainKeyStates[row][col] = isPressed;
        } else {
          // State not stable, keep previous state
          mainKeyStates[row][col] = lastMainKeyStates[row][col];
        }
      } else {
        // State unchanged, use current reading
        mainKeyStates[row][col] = isPressed;
      }
    }
    
    // Set row back to HIGH before moving to next row
    digitalWrite(mainRowPins[row], HIGH);
    
    // Small delay to ensure row pin is fully HIGH
    delayMicroseconds(5);
  }
}

void checkMainKeyChanges() {
  for (int row = 0; row < MAIN_ROWS; row++) {
    for (int col = 0; col < MAIN_COLS; col++) {
      // Check if key state changed
      if (mainKeyStates[row][col] != lastMainKeyStates[row][col]) {
        char key = mainKeyMap[row][col];
        unsigned char ukey = (unsigned char)key;
        String keyName = getKeyName(key, row, col);
        
        if (mainKeyStates[row][col]) {
          // Key pressed - for thumb cluster, check if entire column is reporting
          if (row == 4) {
            // Debug: check if multiple keys in same column are pressed
            int pressedCount = 0;
            for (int r = 0; r < MAIN_ROWS; r++) {
              if (mainKeyStates[r][col]) pressedCount++;
            }
            if (pressedCount > 1) {
              Serial.print("WARNING: Multiple keys in column ");
              Serial.print(col);
              Serial.print(" reporting as pressed! (");
              Serial.print(pressedCount);
              Serial.println(" keys)");
            }
          }
          
          Serial.print("Right Main Pressed: Row ");
          Serial.print(row);
          Serial.print(", Col ");
          Serial.print(col);
          Serial.print(" -> Key: 0x");
          Serial.print(ukey, HEX);
          Serial.print(" (");
          Serial.print(ukey);
          Serial.print(") -> ");
          Serial.println(keyName);
        } else {
          // Key released
          Serial.print("Right Main Released: Row ");
          Serial.print(row);
          Serial.print(", Col ");
          Serial.print(col);
          Serial.print(" -> Key: 0x");
          Serial.print(ukey, HEX);
          Serial.print(" (");
          Serial.print(ukey);
          Serial.print(") -> ");
          Serial.println(keyName);
        }
        
        // Update last state
        lastMainKeyStates[row][col] = mainKeyStates[row][col];
      }
    }
  }
}


void updateRightModifierStates() {
  // Currently no modifiers on right side main matrix
  // All right side modifiers are false
  rightCtrlPressed = false;
  rightShiftPressed = false;
  rightAltPressed = false;
  rightSuperPressed = false;
  rightFnPressed = false;
}

void processAllKeys() {
  // Build modifier byte
  uint8_t modifier = 0x00;
  
  // Process left side modifiers
  if (leftModifiers[0]) modifier |= 0x01; // Left Ctrl
  if (leftModifiers[1]) modifier |= 0x02; // Left Shift
  if (leftModifiers[2]) modifier |= 0x04; // Left Alt
  if (leftModifiers[3]) modifier |= 0x08; // Left GUI/Super
  
  // Process right side modifiers
  if (rightCtrlPressed) modifier |= 0x10;  // Right Ctrl
  if (rightShiftPressed) modifier |= 0x20; // Right Shift
  if (rightAltPressed) modifier |= 0x40;   // Right Alt
  if (rightSuperPressed) modifier |= 0x80; // Right GUI/Super
  
  // Collect all pressed keys (store as chars for BleKeyboard, special keys as HID codes)
  char keys[6] = {0, 0, 0, 0, 0, 0};
  uint8_t specialKeys[6] = {0, 0, 0, 0, 0, 0}; // For special keys that need HID codes
  int keyIndex = 0;
  int specialIndex = 0;
  
  // Process left side main keys
  for (int row = 0; row < 4; row++) {
    for (int col = 0; col < 6; col++) {
      if (leftMainKeyStates[row][col] && keyIndex < 6) {
        char key = mapLeftSideKey(row, col);
        if (key != 0) {
          // Check for special keys first (before ASCII check)
          if (key == KEY_DELETE || key == KEY_HOME || key == KEY_SPACE || 
              key == KEY_BACKTICK || key == KEY_TILDE) {
            // Special key - convert to HID keycode
            uint8_t hidKeycode = asciiToHIDKeycode(key);
            if (hidKeycode != 0 && specialIndex < 6) {
              specialKeys[specialIndex++] = hidKeycode;
            }
          } else if (key >= 0x20 && key <= 0x7E) {
            // Regular ASCII character
            keys[keyIndex++] = key;
          } else {
            // Other special key
            uint8_t hidKeycode = asciiToHIDKeycode(key);
            if (hidKeycode != 0 && specialIndex < 6) {
              specialKeys[specialIndex++] = hidKeycode;
            }
          }
        }
      }
    }
  }
  
  // Process right side main keys (rows 0-3)
  for (int row = 0; row < MAIN_ROWS - 1; row++) {
    for (int col = 0; col < MAIN_COLS; col++) {
      if (keyIndex >= 6) break;
      
      char key = mainKeyMap[row][col];
      if (key == 0) continue;
      
      if (mainKeyStates[row][col]) {
        // Check if shift is pressed and use modifier map
        char keyToSend = key;
        if (leftModifiers[1]) { // Shift is pressed
          char modifierKey = mainModifierMap[row][col];
          if (modifierKey != 0) {
            keyToSend = modifierKey;
          }
        }
        
        // Check for special keys first (before ASCII check)
        if (keyToSend == KEY_DELETE || keyToSend == KEY_HOME || keyToSend == KEY_BACKTICK || keyToSend == KEY_TILDE) {
          // Special key - convert to HID keycode
          uint8_t hidKeycode = asciiToHIDKeycode(keyToSend);
          if (hidKeycode != 0 && specialIndex < 6) {
            specialKeys[specialIndex++] = hidKeycode;
          }
        } else if (keyToSend >= 0x20 && keyToSend <= 0x7E) {
          // Regular ASCII character
          keys[keyIndex++] = keyToSend;
        } else {
          // Other special key
          uint8_t hidKeycode = asciiToHIDKeycode(keyToSend);
          if (hidKeycode != 0 && specialIndex < 6) {
            specialKeys[specialIndex++] = hidKeycode;
          }
        }
      }
    }
    if (keyIndex >= 6) break;
  }
  
  // Process thumb cluster (row 4)
  if (keyIndex < 6 || specialIndex < 6) {
    for (int col = 0; col < MAIN_COLS; col++) {
      if (keyIndex >= 6 && specialIndex >= 6) break;
      
      char key = mainKeyMap[4][col];
      if (key == 0 || key == ' ') continue;
      
      if (mainKeyStates[4][col]) {
        // Check if shift is pressed and use modifier map
        // NOTE: This requires the left ESP32 to be connected and sending shift state
        // via leftModifiers[1]. Until then, modifier map won't be applied.
        char keyToSend = key;
        if (leftModifiers[1]) { // Shift is pressed (from left ESP32)
          char modifierKey = mainModifierMap[4][col];
          if (modifierKey != 0 && modifierKey != KEY_DELETE && modifierKey != KEY_HOME && modifierKey != KEY_SPACE) {
            keyToSend = modifierKey;
          }
        }
        
        // Check for special keys first (before ASCII check)
        // Note: These constants may overlap with ASCII, so check them specifically
        if (keyToSend == KEY_SPACE) {
          // Space key - send as ASCII space character ' ' (0x20)
          // BleKeyboard will handle the conversion to HID 0x2C
          if (keyIndex < 6) keys[keyIndex++] = ' ';
        } else if (keyToSend == KEY_DELETE || keyToSend == KEY_HOME || keyToSend == KEY_BACKTICK || keyToSend == KEY_TILDE) {
          // Special key - convert to HID keycode
          uint8_t hidKeycode = asciiToHIDKeycode(keyToSend);
          if (hidKeycode != 0 && specialIndex < 6) {
            specialKeys[specialIndex++] = hidKeycode;
          }
        } else if (keyToSend >= 0x20 && keyToSend <= 0x7E) {
          // Regular ASCII character
          if (keyIndex < 6) keys[keyIndex++] = keyToSend;
        } else {
          // Other special key
          uint8_t hidKeycode = asciiToHIDKeycode(keyToSend);
          if (hidKeycode != 0 && specialIndex < 6) {
            specialKeys[specialIndex++] = hidKeycode;
          }
        }
      }
    }
  }
  
  // Check if anything changed
  bool changed = (modifier != lastModifier);
  if (!changed) {
    for (int i = 0; i < 6; i++) {
      if (keys[i] != lastKeys[i] || specialKeys[i] != lastSpecialKeys[i]) {
        changed = true;
        break;
      }
    }
  }
  
  // Only send if changed
  if (changed) {
    // Release keys that are no longer pressed
    for (int i = 0; i < 6; i++) {
      if (lastKeys[i] != 0) {
        // Check if this key is still in the current keys array
        bool stillPressed = false;
        for (int j = 0; j < 6; j++) {
          if (keys[j] == lastKeys[i]) {
            stillPressed = true;
            break;
          }
        }
        if (!stillPressed) {
          // Key was released, release it
          bleKeyboard.release(lastKeys[i]);
        }
      }
    }
    
    // Release special keys that are no longer pressed
    for (int i = 0; i < 6; i++) {
      if (lastSpecialKeys[i] != 0) {
        // Check if this key is still in the current specialKeys array
        bool stillPressed = false;
        for (int j = 0; j < 6; j++) {
          if (specialKeys[j] == lastSpecialKeys[i]) {
            stillPressed = true;
            break;
          }
        }
        if (!stillPressed) {
          // Key was released, release it
          bleKeyboard.release(lastSpecialKeys[i]);
        }
      }
    }
    
    // Small delay to ensure releases are sent
    delay(5);
    
    // Note: We DON'T manually set shift modifier here because:
    // 1. For regular ASCII keys, BleKeyboard will automatically add shift for uppercase
    // 2. For modifier map characters like '@' and '~', we want to send them directly
    //    without shift modifier, as they're already the shifted characters
    
    // Press new keys that weren't previously pressed
    for (int i = 0; i < 6; i++) {
      if (keys[i] != 0) {
        // Check if this key was already pressed
        bool wasPressed = false;
        for (int j = 0; j < 6; j++) {
          if (lastKeys[j] == keys[i]) {
            wasPressed = true;
            break;
          }
        }
        if (!wasPressed) {
          // New key pressed
          bleKeyboard.press(keys[i]);
        }
      }
    }
    
    // Press new special keys that weren't previously pressed
    for (int i = 0; i < 6; i++) {
      if (specialKeys[i] != 0) {
        // Check if this key was already pressed
        bool wasPressed = false;
        for (int j = 0; j < 6; j++) {
          if (lastSpecialKeys[j] == specialKeys[i]) {
            wasPressed = true;
            break;
          }
        }
        if (!wasPressed) {
          // New key pressed
          bleKeyboard.press(specialKeys[i]);
        }
      }
    }
    
    // Debug output
    static unsigned long lastDebugTime = 0;
    if (millis() - lastDebugTime > 100) {
      Serial.print("SENDING Keys: Mod=0x");
      Serial.print(modifier, HEX);
      Serial.print(" ASCII=[");
      for (int i = 0; i < 6; i++) {
        if (keys[i] != 0) {
          Serial.print("'");
          Serial.print((char)keys[i]);
          Serial.print("' ");
        }
      }
      Serial.print("] HID=[");
      for (int i = 0; i < 6; i++) {
        if (specialKeys[i] != 0) {
          Serial.print("0x");
          Serial.print(specialKeys[i], HEX);
          Serial.print(" ");
        }
      }
      Serial.println("]");
      lastDebugTime = millis();
    }
    
    // Save state
    lastModifier = modifier;
    memcpy(lastKeys, keys, 6);
    memcpy(lastSpecialKeys, specialKeys, 6);
  } else if (modifier == 0 && keys[0] == 0 && specialKeys[0] == 0) {
    // No keys pressed - make sure we release everything
    static unsigned long lastReleaseCheck = 0;
    if (millis() - lastReleaseCheck > 100) {
      // Check if we need to release (in case something got stuck)
      if (lastModifier != 0 || lastKeys[0] != 0 || lastSpecialKeys[0] != 0) {
        bleKeyboard.releaseAll();
        lastModifier = 0;
        memset(lastKeys, 0, 6);
        memset(lastSpecialKeys, 0, 6);
        Serial.println("Released all keys (idle state)");
      }
      lastReleaseCheck = millis();
    }
  }
}

// Get readable name for a key (especially for thumb keys)
String getKeyName(char key, int row, int col) {
  // Convert to unsigned char to handle HID keycodes properly
  unsigned char ukey = (unsigned char)key;
  
  // Thumb cluster is row 4 - check for HID keycodes there
  if (row == 4) {
    // This is the thumb cluster row
    if (ukey == KEY_DELETE)   return "DELETE";
    if (ukey == KEY_HOME)     return "HOME";
    if (ukey == KEY_SPACE)    return "SPACE";
    if (ukey == KEY_BACKTICK) return "BACKTICK (`)";
    if (ukey == KEY_TILDE)    return "TILDE (~)";
    if (ukey == ' ')          return "SPACE (char)";
  } else {
    // Regular keys - handle 0x2C as comma, not KEY_SPACE
    // KEY_SPACE (0x2C) and ',' (0x2C) have the same value
    // So we check row to distinguish: if row != 4, it's a comma
    if (ukey == 0x2C && row != 4) {
      return ",";  // This is a comma, not KEY_SPACE
    }
  }
  
  // Check for HID keycodes (only if not already handled above)
  if (ukey == KEY_DELETE)   return "DELETE";
  if (ukey == KEY_HOME)     return "HOME";
  if (ukey == KEY_BACKTICK) return "BACKTICK (`)";
  if (ukey == KEY_TILDE)    return "TILDE (~)";
  
  // Handle regular characters
  if (ukey == ' ') return "SPACE";
  if (ukey == 0)   return "NULL";
  
  // Printable ASCII characters
  if (ukey >= 32 && ukey <= 126) {
    return String((char)ukey);
  }
  
  // Non-printable, return hex value
  return "0x" + String(ukey, HEX);
}

// Convert ASCII character to HID keycode
uint8_t asciiToHIDKeycode(char key) {
  // Handle special keys - use BleKeyboard library constants
  // These checks must come BEFORE ASCII range checks since the constants
  // overlap with ASCII values
  if (key == KEY_DELETE) return KEY_BACKSPACE; // Delete key sends backspace (0xB2)
  if (key == KEY_HOME) return KEY_LEFT_GUI; // Home key sends Windows/GUI/Super key (0x83)
  if (key == KEY_SPACE) {
    // KEY_SPACE is 0xF2, but we need to send the actual space HID code
    // Space ' ' (0x20) maps to 0x2C in HID, but we should pass ' ' to bleKeyboard
    // So return 0 to indicate we should handle it as ASCII space instead
    return 0; // Will be handled as ASCII space character
  }
  if (key == KEY_BACKTICK) return 0x35; // Backtick/Grave (0x35 is correct)
  if (key == KEY_TILDE) return 0x35; // Tilde (same as backtick with shift)
  
  // Handle other special keys
  if (key == 0x29) return KEY_ESC; // ESC using BleKeyboard constant
  if (key == 0x2B) return KEY_TAB; // TAB using BleKeyboard constant
  
  // Convert lowercase letters (a-z) to HID keycodes
  if (key >= 'a' && key <= 'z') {
    return (key - 'a' + 0x04); // 'a' = 0x04, 'b' = 0x05, etc.
  }
  
  // Convert uppercase letters (A-Z) - handled via shift modifier + lowercase
  if (key >= 'A' && key <= 'Z') {
    return (key - 'A' + 0x04); // Same as lowercase, shift modifier handles case
  }
  
  // Convert numbers (0-9) to HID keycodes
  if (key >= '0' && key <= '9') {
    if (key == '0') return 0x27;
    return (key - '1' + 0x1E); // '1' = 0x1E, '2' = 0x1F, ..., '9' = 0x26
  }
  
  // Special characters
  switch (key) {
    case ' ': return 0x2C; // Space
    case '-': return 0x2D; // Minus
    case '=': return 0x2E; // Equals
    case '[': return 0x2F; // Left bracket
    case ']': return 0x30; // Right bracket
    case '\\': return 0x31; // Backslash
    case ';': return 0x33; // Semicolon
    case '\'': return 0x34; // Apostrophe
    case '`': return 0x35; // Grave accent/backtick
    case ',': return 0x36; // Comma
    case '.': return 0x37; // Period
    case '/': return 0x38; // Slash
    case '\"': return 0x34; // Quote (same as apostrophe with shift)
    case '#': return 0x32; // Hash/number sign (US: Shift+3, UK: Shift+3)
    case '@': return 0x1F; // At sign (US: Shift+2, UK: Shift+')
    default: return 0x00; // Unknown key
  }
}

// Map left side main keys to appropriate characters
char mapLeftSideKey(int row, int col) {
  // Skip modifier keys (they're handled separately)
  if ((row == 3 && col == 0) ||  // shift
      (row == 2 && col == 0)) {  // super
    return 0; // Don't send these as regular keys
  }
  
  // Map other keys
  switch (row) {
    case 0: // Top row
      switch (col) {
        case 0: return 0x29; // ESC key (HID usage ID)
        case 1: return '1';
        case 2: return '2';
        case 3: return '3';
        case 4: return '4';
        case 5: return '5';
      }
      break;
    case 1: // Second row
      switch (col) {
        case 0: return 0x2B; // TAB key (HID usage ID)
        case 1: return 'q';
        case 2: return 'w';
        case 3: return 'e';
        case 4: return 'r';
        case 5: return 't';
      }
      break;
    case 2: // Third row
      switch (col) {
        case 0: return 0; // super key - handled as modifier
        case 1: return 'a';
        case 2: return 's';
        case 3: return 'd';
        case 4: return 'f';
        case 5: return 'g';
      }
      break;
    case 3: // Fourth row
      switch (col) {
        case 0: return 0; // shift key - handled as modifier
        case 1: return 'z';
        case 2: return 'x';
        case 3: return 'c';
        case 4: return 'v';
        case 5: return 'b';
      }
      break;
  }
  return 0;
}
