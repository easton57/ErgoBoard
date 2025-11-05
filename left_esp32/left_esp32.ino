/*
 * Split Keyboard - Left Side (ESP32)
 * Main cluster: 4 rows × 6 columns
 * 
 * Pin Configuration:
 * Main Matrix:
 *   Rows (Outputs): 2, 3, 4, 5
 *   Columns (Inputs): 6, 7, 8, 9, 10, 11
 * 
 * I2C Communication:
 *   SDA: 21 (dedicated I2C data line)
 *   SCL: 22 (dedicated I2C clock line)
 */

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
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

// Left side modifier states
bool ctrlPressed = false;
bool shiftPressed = false;
bool altPressed = false;
bool superPressed = false;
bool fnPressed = false;

// Right side key states (received via I2C)
bool rightMainKeyStates[4][6] = {false};
bool rightModifiers[5] = {false}; // ctrl, shift, alt, super, fn

// Debounce delay
const unsigned long DEBOUNCE_DELAY = 5;

// BLE Client to connect to right side ESP32
BLEClient* pClient = nullptr;
BLECharacteristic* pCharacteristic = nullptr;
bool doConnect = false;
bool connected = false;
bool doScan = false;

// Service and characteristic UUIDs for communication
#define SERVICE_UUID        "12345678-1234-1234-1234-123456789abc"
#define CHARACTERISTIC_UUID "87654321-4321-4321-4321-cba987654321"

// Remote device address (will be set when scanning)
BLEAddress* pServerAddress = nullptr;

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

// Key data structure to send via bluetooth
struct KeyData {
  bool mainKeys[MAIN_ROWS][MAIN_COLS];
  bool modifiers[5];  // ctrl, shift, alt, super, fn
  unsigned long timestamp;
};

KeyData keyData;

// BLE Client callbacks
class MyClientCallback : public BLEClientCallbacks {
    void onConnect(BLEClient* pclient) {
        Serial.println("Connected to right side ESP32");
        connected = true;
    }

    void onDisconnect(BLEClient* pclient) {
        connected = false;
        Serial.println("Disconnected from right side ESP32");
        doScan = true;
    }
};

// BLE Advertised Device callbacks
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        Serial.print("BLE Advertised Device found: ");
        Serial.println(advertisedDevice.toString().c_str());

        // Check if this is our right side ESP32
        if (advertisedDevice.haveServiceUUID() && 
            advertisedDevice.isAdvertisingService(BLEUUID(SERVICE_UUID))) {
            
            BLEDevice::getScan()->stop();
            pServerAddress = new BLEAddress(advertisedDevice.getAddress());
            doConnect = true;
            doScan = true;
            Serial.println("Found right side ESP32!");
        }
    }
};

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
  Wire.begin();
  
  // Initialize BLE
  BLEDevice::init("Left Split Keyboard");
  
  // Create BLE client
  pClient = BLEDevice::createClient();
  pClient->setClientCallbacks(new MyClientCallback());
  
  // Start scanning for right side ESP32
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(5, false);
  
  // Debug output
  Serial.begin(115200);
  Serial.println("Left Split Keyboard - ESP32");
  Serial.println("Main: 5 rows × 6 columns");
  Serial.println("Scanning for right side ESP32...");
}

void loop() {
  // Handle BLE connection
  if (doConnect == true) {
    if (connectToServer()) {
      Serial.println("Connected to right side ESP32!");
    } else {
      Serial.println("Failed to connect to right side ESP32");
    }
    doConnect = false;
  }

  // If we are connected to a peer BLE Server, update the characteristic each time we are reached
  // with the current time since boot.
  if (connected) {
    // Scan the main matrix
    scanMainMatrix();
    
    // Check for key state changes
    checkMainKeyChanges();
    
    // Update modifier states
    updateModifierStates();
    
    // Update key data structure
    updateKeyData();
    
    // Send key data to right side
    sendKeyDataToRight();
  } else if (doScan) {
    BLEDevice::getScan()->start(0);  // this is just example to start scan after disconnect, most of the time you don't need this
    doScan = false;
  }
  
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

// Connect to the right side ESP32 BLE server
bool connectToServer() {
    Serial.print("Forming a connection to ");
    Serial.println(pServerAddress->toString().c_str());
    
    pClient->connect(*pServerAddress);
    Serial.println(" - Connected to server");

    // Obtain a reference to the service we are after in the remote BLE server.
    BLERemoteService* pRemoteService = pClient->getService(SERVICE_UUID);
    if (pRemoteService == nullptr) {
      Serial.print("Failed to find our service UUID: ");
      Serial.println(SERVICE_UUID);
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found our service");

    // Obtain a reference to the characteristic in the service of the remote BLE server.
    pCharacteristic = pRemoteService->getCharacteristic(CHARACTERISTIC_UUID);
    if (pCharacteristic == nullptr) {
      Serial.print("Failed to find our characteristic UUID: ");
      Serial.println(CHARACTERISTIC_UUID);
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found our characteristic");

    return true;
}

// Send key data to right side ESP32
void sendKeyDataToRight() {
  if (pCharacteristic != nullptr && connected) {
    // Send the key data structure
    pCharacteristic->writeValue((uint8_t*)&keyData, sizeof(keyData), false);
  }
}

