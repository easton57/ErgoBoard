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

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
// #include <Wire.h>  // I2C disabled for right-side-only development

// HID keycode constants for thumb keys
#define KEY_DELETE    0x2A
#define KEY_HOME      0x4A
#define KEY_SPACE     0x2C
#define KEY_BACKTICK  0x35
#define KEY_TILDE     0x64

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

// BLE variables
BLEServer* pServer = nullptr;
BLECharacteristic* pInputCharacteristic;
BLECharacteristic* pDataCharacteristic; // For receiving data from left side
bool deviceConnected = false;
bool oldDeviceConnected = false;

// Track last sent report to detect changes
uint8_t lastReport[9] = {0};
bool reportChanged = false;

// Service and characteristic UUIDs for communication
#define SERVICE_UUID        "12345678-1234-1234-1234-123456789abc"
#define CHARACTERISTIC_UUID "87654321-4321-4321-4321-cba987654321"

// HID Report Descriptor for Keyboard
uint8_t _hidReportDescriptor[63] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x05, 0x07,        //   Usage Page (Key Codes)
    0x19, 0xE0,        //   Usage Minimum (224)
    0x29, 0xE7,        //   Usage Maximum (231)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data, Variable, Absolute)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x01,        //   Input (Constant)
    0x95, 0x06,        //   Report Count (6)
    0x75, 0x08,        //   Report Size (8)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x65,        //   Logical Maximum(101)
    0x05, 0x07,        //   Usage Page (Key Codes)
    0x19, 0x00,        //   Usage Minimum (0)
    0x29, 0x65,        //   Usage Maximum (101)
    0x81, 0x00,        //   Input (Data, Array)
    0xC0               // End Collection
};

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

// BLE Server callbacks
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("\n=== BLE device connected (keyboard ready) ===");
      // Reset last report when connecting
      memset(lastReport, 0, 9);
    }

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("\n=== BLE device disconnected ===");
      // Reset last report when disconnecting
      memset(lastReport, 0, 9);
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
  
  // Initialize BLE
  BLEDevice::init("Right Split Keyboard");
  
  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  
  // Create HID Service
  BLEService *pHIDService = pServer->createService(BLEUUID((uint16_t)0x1812));
  
  // Create HID Information Characteristic
  uint8_t hidInfo[] = {0x11, 0x01, 0x00, 0x01}; // HID version 1.11, country code 0, flags 1
  BLECharacteristic* pHIDInfoCharacteristic = new BLECharacteristic(BLEUUID((uint16_t)0x2A4A), BLECharacteristic::PROPERTY_READ);
  pHIDInfoCharacteristic->setValue(hidInfo, 4);
  pHIDService->addCharacteristic(pHIDInfoCharacteristic);
  
  // Create Report Map Characteristic
  BLECharacteristic* pHIDReportMapCharacteristic = new BLECharacteristic(BLEUUID((uint16_t)0x2A4B), BLECharacteristic::PROPERTY_READ);
  pHIDReportMapCharacteristic->setValue(_hidReportDescriptor, sizeof(_hidReportDescriptor));
  pHIDService->addCharacteristic(pHIDReportMapCharacteristic);
  
  // Create Input Report Characteristic
  pInputCharacteristic = new BLECharacteristic(BLEUUID((uint16_t)0x2A4D), BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  pInputCharacteristic->addDescriptor(new BLE2902());
  pHIDService->addCharacteristic(pInputCharacteristic);
  
  // Start the service
  pHIDService->start();
  
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
  
  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(pHIDService->getUUID());
  pAdvertising->addServiceUUID(pCommService->getUUID());
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
  pAdvertising->setMinPreferred(0x12);
  pAdvertising->setMinInterval(0x20);    // Reduce connection interval for better stability
  pAdvertising->setMaxInterval(0x40);
  BLEDevice::startAdvertising();
  
  Serial.println("BLE HID Keyboard ready - waiting for connection...");
  Serial.println("Scan for 'Right Split Keyboard' and connect");
}

void loop() {
  // Handle BLE connection state changes first
  if (!deviceConnected && oldDeviceConnected) {
    // Disconnected - give the bluetooth stack time to clean up
    delay(500);
    pServer->startAdvertising();
    Serial.println("Start advertising");
    oldDeviceConnected = deviceConnected;
  }
  
  if (deviceConnected && !oldDeviceConnected) {
    // Just connected
    oldDeviceConnected = deviceConnected;
    Serial.println("Connection established - ready for input");
  }
  
  // Always scan the right side matrix (even without BLE connection for testing)
  scanMainMatrix();
  
  // Always check for key state changes (for logging/debugging)
  checkMainKeyChanges();
  
  // Update right side modifier states
  updateRightModifierStates();
  
  // Process all keys only if BLE is connected
  if (deviceConnected) {
    processAllKeys();
  } else {
    // Only print waiting message occasionally to avoid spam
    static unsigned long lastMessage = 0;
    if (millis() - lastMessage > 5000) {
      Serial.println("Waiting for BLE connection... (Right side scanning active)");
      lastMessage = millis();
    }
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
      
      // Apply debouncing
      if (isPressed != lastMainKeyStates[row][col]) {
        delay(DEBOUNCE_DELAY);
        isPressed = (digitalRead(mainColPins[col]) == LOW);
      }
      
      mainKeyStates[row][col] = isPressed;
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
  // Build HID keyboard report
  uint8_t modifier = 0x00;
  uint8_t keys[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  
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
  
  // Process left side main keys (only send if not modifiers)
  int keyIndex = 0;
  for (int row = 0; row < 4; row++) {
    for (int col = 0; col < 6; col++) {
      if (leftMainKeyStates[row][col] && keyIndex < 6) {
        // Map left side keys to appropriate characters
        char key = mapLeftSideKey(row, col);
        if (key != 0) {
          uint8_t hidKeycode = asciiToHIDKeycode(key);
          if (hidKeycode != 0) {
            keys[keyIndex++] = hidKeycode;
          }
        }
      }
    }
  }
  
  // Process right side main keys (rows 0-3)
  for (int row = 0; row < MAIN_ROWS - 1; row++) {
    for (int col = 0; col < MAIN_COLS; col++) {
      if (keyIndex >= 6) break; // Maximum 6 keys in HID report
      
      char key = mainKeyMap[row][col];
      
      // Skip empty keys
      if (key == 0) {
        continue;
      }
      
      if (mainKeyStates[row][col]) {
        uint8_t hidKeycode = asciiToHIDKeycode(key);
        if (hidKeycode != 0) {
          keys[keyIndex++] = hidKeycode;
        }
      }
    }
    if (keyIndex >= 6) break; // Exit outer loop if full
  }
  
  // Process thumb cluster (row 4)
  if (keyIndex < 6) {
    for (int col = 0; col < MAIN_COLS; col++) {
      if (keyIndex >= 6) break;
      
      char key = mainKeyMap[4][col];
      
      // Skip empty keys
      if (key == 0 || key == ' ') {
        continue;
      }
      
      if (mainKeyStates[4][col]) {
        uint8_t hidKeycode = asciiToHIDKeycode(key);
        if (hidKeycode != 0) {
          keys[keyIndex++] = hidKeycode;
        }
      }
    }
  }
  
  // Build HID report (Report ID + Modifier + Reserved + 6 keycodes)
  uint8_t report[9] = {0x01, modifier, 0x00, keys[0], keys[1], keys[2], keys[3], keys[4], keys[5]};
  
  // Check if report has changed
  reportChanged = false;
  for (int i = 0; i < 9; i++) {
    if (report[i] != lastReport[i]) {
      reportChanged = true;
      break;
    }
  }
  
  // Always send report if changed, and send empty report periodically to maintain connection
  static unsigned long lastReportTime = 0;
  bool shouldSend = reportChanged;
  
  // Send empty report every 50ms when no keys pressed to maintain connection
  // This prevents the host from thinking the keyboard disconnected
  if (!reportChanged) {
    if (millis() - lastReportTime > 50) {
      shouldSend = true;
      lastReportTime = millis();
    }
  } else {
    lastReportTime = millis();
  }
  
  if (shouldSend) {
    // Send the report
    pInputCharacteristic->setValue(report, 9);
    pInputCharacteristic->notify();
    
    // Debug: print report details (only when keys are actually pressed)
    if (reportChanged) {
      static unsigned long lastDebugTime = 0;
      if (millis() - lastDebugTime > 100) { // Limit debug output
        Serial.print("HID Report: Mod=");
        Serial.print(modifier, HEX);
        Serial.print(" Keys=[");
        for (int i = 0; i < 6; i++) {
          if (keys[i] != 0) {
            Serial.print("0x");
            Serial.print(keys[i], HEX);
            Serial.print(" ");
          }
        }
        Serial.println("]");
        lastDebugTime = millis();
      }
    }
    
    // Copy to last report
    memcpy(lastReport, report, 9);
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
  // Handle special HID keycodes that are already in HID format
  if (key == 0x29) return 0x29; // ESC
  if (key == 0x2B) return 0x2B; // TAB
  if (key == KEY_DELETE) return 0x4C; // Delete (Backspace in HID)
  if (key == KEY_HOME) return 0x4A; // Home
  if (key == KEY_SPACE) return 0x2C; // Space
  if (key == KEY_BACKTICK) return 0x35; // Backtick/Grave
  if (key == KEY_TILDE) return 0x35; // Tilde (same as backtick with shift)
  
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
