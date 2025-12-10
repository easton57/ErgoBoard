/*
 * Split Keyboard - Right Side (ESP32)
 * Slave to Raspberry Pi 4 (Wireless via Bluetooth)
 * Main cluster: 5 rows × 6 columns (includes thumb cluster on row 4)
 * 
 * Architecture:
 *   - Left side: Raspberry Pi (handles left side keys)
 *   - Right side: ESP32 (this device, handles right side keys)
 *   - ESP32 sends right side key data to Raspberry Pi via BLE
 *   - Raspberry Pi combines both sides and sends to system
 * 
 * Pin Configuration:
 * Main Matrix:
 *   Rows (Outputs): 2, 4, 5, 18 (main), 19 (thumb cluster)
 *   Columns (Inputs): 23, 27, 26, 25, 33, 32 (avoiding I2C pins 21, 22)
 * 
 * BLE Communication:
 *   - BLE Client: Sends right side key data to Raspberry Pi 4
 *   
 * Note: Pin 34 is INPUT-ONLY and cannot be used as OUTPUT (row pin)
 */

#include <BLEDevice.h>
#include <BLEClient.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

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

// Debounce delay
const unsigned long DEBOUNCE_DELAY = 5;

// BLE variables for Raspberry Pi communication (BLE Client)
BLEClient* pRPiClient = nullptr;
BLERemoteCharacteristic* pRPiCharacteristic = nullptr;
bool rpiConnected = false;
bool doRPiConnect = false;
bool doRPiScan = false;
BLEAddress* pRPiServerAddress = nullptr;

// Service and characteristic UUIDs for communication with Raspberry Pi 4
#define RPI_SERVICE_UUID        "abcd1234-5678-90ef-1234-567890abcdef"
#define RPI_CHARACTERISTIC_UUID "fedc9876-5432-10fe-dcba-9876543210fe"

// KeyData struct for sending to Raspberry Pi
// Format: JSON-like string sent via BLE
// Example: {"keys":[[0,0,0,0,0,0],...],"mods":[0,0,0,0,0],"ts":12345}
struct KeyDataForRPi {
  bool keys[MAIN_ROWS][MAIN_COLS];
  bool mods[5];  // ctrl, shift, alt, super, fn
  unsigned long timestamp;
};

KeyDataForRPi keyDataForRPi;

// Forward declarations
String getKeyName(char key, int row = -1, int col = -1);
uint8_t asciiToHIDKeycode(char key);
void sendKeyDataToRPi();
String serializeKeyDataToJSON();
bool connectToRPi();

// BLE Client callbacks for Raspberry Pi connection
class MyRPiClientCallback: public BLEClientCallbacks {
  void onConnect(BLEClient* pClient) {
    Serial.println("\n=== Connected to Raspberry Pi 4 ===");
    rpiConnected = true;
  }

  void onDisconnect(BLEClient* pClient) {
    Serial.println("\n=== Disconnected from Raspberry Pi 4 ===");
    rpiConnected = false;
    doRPiScan = true;
  }
};

// BLE Advertised Device callbacks for finding Raspberry Pi
class MyRPiAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        Serial.print("BLE Advertised Device found: ");
        Serial.println(advertisedDevice.toString().c_str());

        // Check if this is our Raspberry Pi 4
        if (advertisedDevice.haveServiceUUID() && 
            advertisedDevice.isAdvertisingService(BLEUUID(RPI_SERVICE_UUID))) {
            
            BLEDevice::getScan()->stop();
            pRPiServerAddress = new BLEAddress(advertisedDevice.getAddress());
            doRPiConnect = true;
            doRPiScan = false;
            Serial.println("Found Raspberry Pi 4!");
        }
    }
};


void setup() {
  // Initialize Serial for debugging
  Serial.begin(115200);
  delay(1000); // Give serial time to initialize
  
  Serial.println("\n\nRight Split Keyboard - ESP32 (RPi Slave - Wireless)");
  Serial.println("Main: 5 rows × 6 columns");
  Serial.println("Communication: BLE to RPi 4 (wireless)");
  
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
  
  // Create BLE client for Raspberry Pi communication
  pRPiClient = BLEDevice::createClient();
  pRPiClient->setClientCallbacks(new MyRPiClientCallback());
  
  // Start scanning for Raspberry Pi 4
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyRPiAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(5, false);
  
  Serial.println("Scanning for Raspberry Pi 4...");
}

void loop() {
  // Handle Raspberry Pi BLE connection
  if (doRPiConnect == true) {
    if (connectToRPi()) {
      Serial.println("Connected to Raspberry Pi 4!");
    } else {
      Serial.println("Failed to connect to Raspberry Pi 4");
    }
    doRPiConnect = false;
  }
  
  // If disconnected from RPi, restart scanning
  if (!rpiConnected && doRPiScan) {
    BLEDevice::getScan()->start(0);
    doRPiScan = false;
  }
  
  // Always scan the right side matrix
  scanMainMatrix();
  
  // Always check for key state changes (for logging/debugging)
  checkMainKeyChanges();
  
  // Update right side modifier states
  updateRightModifierStates();
  
  // Prepare and send key data to Raspberry Pi (only if connected)
  if (rpiConnected) {
    sendKeyDataToRPi();
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

// Connect to the Raspberry Pi 4 BLE server
bool connectToRPi() {
    if (pRPiServerAddress == nullptr) {
      Serial.println("No RPi server address available");
      return false;
    }
    
    Serial.print("Forming a connection to ");
    Serial.println(pRPiServerAddress->toString().c_str());
    
    pRPiClient->connect(*pRPiServerAddress);
    Serial.println(" - Connected to RPi server");

    // Obtain a reference to the service we are after in the remote BLE server.
    BLERemoteService* pRemoteService = pRPiClient->getService(RPI_SERVICE_UUID);
    if (pRemoteService == nullptr) {
      Serial.print("Failed to find RPi service UUID: ");
      Serial.println(RPI_SERVICE_UUID);
      pRPiClient->disconnect();
      return false;
    }
    Serial.println(" - Found RPi service");

    // Obtain a reference to the characteristic in the service of the remote BLE server.
    pRPiCharacteristic = pRemoteService->getCharacteristic(RPI_CHARACTERISTIC_UUID);
    if (pRPiCharacteristic == nullptr) {
      Serial.print("Failed to find RPi characteristic UUID: ");
      Serial.println(RPI_CHARACTERISTIC_UUID);
      pRPiClient->disconnect();
      return false;
    }
    Serial.println(" - Found RPi characteristic");

    return true;
}

// Prepare and send key data to Raspberry Pi via BLE
void sendKeyDataToRPi() {
  // Update the key data structure
  // Copy right side key states
  for (int row = 0; row < MAIN_ROWS; row++) {
    for (int col = 0; col < MAIN_COLS; col++) {
      keyDataForRPi.keys[row][col] = mainKeyStates[row][col];
    }
  }
  
  // Copy right side modifier states
  keyDataForRPi.mods[0] = rightCtrlPressed;
  keyDataForRPi.mods[1] = rightShiftPressed;
  keyDataForRPi.mods[2] = rightAltPressed;
  keyDataForRPi.mods[3] = rightSuperPressed;
  keyDataForRPi.mods[4] = rightFnPressed;
  
  // Update timestamp
  keyDataForRPi.timestamp = millis();
  
  // Send data to RPi as JSON string via BLE
  if (pRPiCharacteristic != nullptr && rpiConnected) {
    String jsonData = serializeKeyDataToJSON();
    pRPiCharacteristic->writeValue(jsonData.c_str(), false);
  }
}

// Serialize key data to JSON format for RPi
String serializeKeyDataToJSON() {
  String json = "{";
  
  // Keys matrix
  json += "\"keys\":[";
  for (int row = 0; row < MAIN_ROWS; row++) {
    json += "[";
    for (int col = 0; col < MAIN_COLS; col++) {
      json += keyDataForRPi.keys[row][col] ? "1" : "0";
      if (col < MAIN_COLS - 1) json += ",";
    }
    json += "]";
    if (row < MAIN_ROWS - 1) json += ",";
  }
  json += "],";
  
  // Modifiers
  json += "\"mods\":[";
  for (int i = 0; i < 5; i++) {
    json += keyDataForRPi.mods[i] ? "1" : "0";
    if (i < 4) json += ",";
  }
  json += "],";
  
  // Timestamp
  json += "\"ts\":";
  json += String(keyDataForRPi.timestamp);
  
  json += "}";
  return json;
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
// Note: This function is kept for potential future use, but currently
// we send raw key states to RPi which handles HID conversion
uint8_t asciiToHIDKeycode(char key) {
  // Handle special keys - HID keycode values
  // These checks must come BEFORE ASCII range checks since the constants
  // overlap with ASCII values
  if (key == KEY_DELETE) return 0x2A; // Delete key sends backspace (HID 0x2A)
  if (key == KEY_HOME) return 0x83; // Home key sends Windows/GUI/Super key (HID 0x83)
  if (key == KEY_SPACE) {
    // KEY_SPACE is 0xF2, but we need to send the actual space HID code
    // Space ' ' (0x20) maps to 0x2C in HID
    // Return 0 to indicate we should handle it as ASCII space instead
    return 0; // Will be handled as ASCII space character
  }
  if (key == KEY_BACKTICK) return 0x35; // Backtick/Grave (0x35 is correct)
  if (key == KEY_TILDE) return 0x35; // Tilde (same as backtick with shift)
  
  // Handle other special keys
  if (key == 0x29) return 0x29; // ESC (HID 0x29)
  if (key == 0x2B) return 0x2B; // TAB (HID 0x2B)
  
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

