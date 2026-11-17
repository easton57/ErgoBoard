/*
 * Split Keyboard - Right Side (ESP32)
 * BLE client to the left side, which sends everything on to the host.
 *
 * Architecture:
 *   - Left side: ESP32 - BLE keyboard to host + BLE server for this side
 *   - Right side: ESP32 (this device) - scans its keys and sends which
 *     positions are pressed to the left side
 *   - The keymap for BOTH halves lives in left_esp32.ino (keymap[RIGHT]),
 *     so layout changes don't need this side re-uploaded
 *
 * Pin Configuration (5 rows x 6 columns, row 4 is the thumb cluster):
 *   Rows (Outputs, top to bottom): 19, 21, 22, 23 (main), 32 (thumb cluster)
 *   Columns (Inputs, left to right): 33, 25, 26, 27, 18, 4
 *   Note: Pin 34 is INPUT-ONLY and cannot be used as a row pin
 *
 * Restart: hold - (top-right) and \ (bottom-right of the main block)
 *   together for RESTART_HOLD_MS. Works without the left side. Once both are
 *   down they stop being sent, so the host sees at most one - and one \.
 *
 * Sleep: after SLEEP_TIMEOUT_MS with no right side key pressed, this side
 *   sleeps. Press any right key to wake it (it restarts and reconnects to
 *   the left side in a second or two; that first key press is lost).
 */

#include <BLEDevice.h>
#include <BLEClient.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "esp_sleep.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "driver/gpio.h"

// Advertised name of the left side - must match KEYBOARD_NAME in left_esp32.ino
#define LEFT_DEVICE_NAME "Split Keyboard"
// Must match the UUIDs in left_esp32.ino
#define LEFT_SERVICE_UUID        "12345678-1234-1234-1234-123456789abc"
#define LEFT_CHARACTERISTIC_UUID "87654321-4321-4321-4321-cba987654321"

// Matrix dimensions
const int MAIN_ROWS = 5;
const int MAIN_COLS = 6;

// Row pins (top to bottom): 19, 21, 22, 23 (main), 32 (thumb cluster)
const int mainRowPins[MAIN_ROWS] = {19, 21, 22, 23, 32};
// Column pins (left to right)
const int mainColPins[MAIN_COLS] = {33, 25, 26, 27, 18, 4};

// Key labels for the serial log only. What each key actually does is set by
// keymap[RIGHT] in left_esp32.ino - keep these in step with it.
const char* const keyLabels[MAIN_ROWS][MAIN_COLS] = {
  {"6",      "7", "8", "9",         "0",     "-"},
  {"y",      "u", "i", "o",         "p",     "="},
  {"h",      "j", "k", "l",         ";",     "'"},
  {"n",      "m", ",", ".",         "/",     "\\"},
  {"(none)", "#", "`", "BACKSPACE", "SUPER", "SPACE"},
};

// Debounced key states
bool keyDown[MAIN_ROWS][MAIN_COLS] = {};
bool lastKeyDown[MAIN_ROWS][MAIN_COLS] = {};  // for debounce + change log

// Debounce delay
const unsigned long DEBOUNCE_DELAY = 5;

// BLE client state for the link to the left side
BLEClient* pLeftClient = nullptr;
BLERemoteCharacteristic* pLeftCharacteristic = nullptr;
BLEAddress* pLeftServerAddress = nullptr;
bool leftConnected = false;
bool doLeftConnect = false;
bool doLeftScan = false;

// Restart combo: hold these two keys together for RESTART_HOLD_MS
const int RESTART_KEY_A_ROW = 0, RESTART_KEY_A_COL = 5;  // -
const int RESTART_KEY_B_ROW = 3, RESTART_KEY_B_COL = 5;  // backslash
const unsigned long RESTART_HOLD_MS = 2000;
bool restartComboActive = false;   // combo keys are hidden from the left side
unsigned long restartComboSince = 0;

// Sleep after this long with no right side key pressed
const unsigned long SLEEP_TIMEOUT_MS = 30UL * 60UL * 1000UL;  // 30 minutes
unsigned long lastActivityTime = 0;

// What goes over BLE: 5 bytes, so it fits in one packet even at the
// default 23-byte MTU (20 bytes of payload).
//   keys:      bit (row * MAIN_COLS + col) set = key pressed (30 bits)
//   modifiers: bit 0 ctrl, 1 shift, 2 alt, 3 super, 4 fn
//              (always 0 here - this side has no modifier keys of its own;
//              Super on the thumb row is just a key position like any other)
// Must match the packet in left_esp32.ino.
struct __attribute__((packed)) KeyPacket {
  uint32_t keys;
  uint8_t modifiers;
};

void sendKeyDataToLeft();

class LeftClientCallbacks : public BLEClientCallbacks {
  void onConnect(BLEClient* pClient) override {
    Serial.println("\n=== Connected to Left Side ESP32 ===");
    leftConnected = true;
  }

  void onDisconnect(BLEClient* pClient) override {
    Serial.println("\n=== Disconnected from Left Side ESP32 ===");
    leftConnected = false;
    doLeftScan = true;
  }
};

class LeftAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    // The left side advertises as a HID keyboard (no room for our 128-bit
    // service UUID in the packet), so match it by name.
    if (advertisedDevice.haveName() &&
        advertisedDevice.getName() == LEFT_DEVICE_NAME) {
      BLEDevice::getScan()->stop();
      delete pLeftServerAddress;
      pLeftServerAddress = new BLEAddress(advertisedDevice.getAddress());
      doLeftConnect = true;
      doLeftScan = false;
      Serial.printf("Found Left Side ESP32 at %s\n", pLeftServerAddress->toString().c_str());
    }
  }
};

void setup() {
  Serial.begin(115200);
  delay(1000); // Give serial time to initialize
  Serial.println("\n\nRight Split Keyboard - ESP32 (Left Side Client)");

  for (int i = 0; i < MAIN_ROWS; i++) {
    pinMode(mainRowPins[i], OUTPUT);
    digitalWrite(mainRowPins[i], HIGH);
  }
  for (int i = 0; i < MAIN_COLS; i++) {
    pinMode(mainColPins[i], INPUT_PULLUP);
  }

  BLEDevice::init("Right Split Keyboard");

  pLeftClient = BLEDevice::createClient();
  pLeftClient->setClientCallbacks(new LeftClientCallbacks());

  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new LeftAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  // Scanning starts from loop() (non-blocking) so the matrix, restart combo
  // and sleep timer keep working while the left side isn't found yet.
  doLeftScan = true;
}

void loop() {
  if (doLeftConnect) {
    doLeftConnect = false;
    if (connectToLeft()) {
      Serial.println("Connected to Left Side ESP32!");
    } else {
      Serial.println("Failed to connect to Left Side ESP32 - scanning again");
      doLeftScan = true;
    }
  }

  if (!leftConnected && doLeftScan) {
    Serial.println("Scanning for Left Side ESP32...");
    // Non-blocking, runs until the left side is found (onResult stops it)
    BLEDevice::getScan()->start(0, nullptr, false);
    doLeftScan = false;
  }

  scanMatrix();
  logKeyChanges();
  checkRestartCombo();  // before sending, so the combo keys get hidden
  checkSleep();

  // Sent every loop, not just on changes: the left side tells us apart from
  // the host by which connection writes to it.
  sendKeyDataToLeft();

  // Small delay to allow BLE stack to process events
  delay(10);
}

void scanMatrix() {
  for (int row = 0; row < MAIN_ROWS; row++) {
    digitalWrite(mainRowPins[row], LOW);
    delayMicroseconds(10);  // let the line settle

    for (int col = 0; col < MAIN_COLS; col++) {
      // LOW = pressed (column has a pull-up)
      bool isPressed = (digitalRead(mainColPins[col]) == LOW);

      // On a change, only accept it if a second read agrees
      if (isPressed != lastKeyDown[row][col]) {
        delay(DEBOUNCE_DELAY);
        bool secondRead = (digitalRead(mainColPins[col]) == LOW);
        keyDown[row][col] = (isPressed == secondRead) ? isPressed : lastKeyDown[row][col];
      } else {
        keyDown[row][col] = isPressed;
      }
    }

    digitalWrite(mainRowPins[row], HIGH);
    delayMicroseconds(5);
  }
}

void logKeyChanges() {
  for (int row = 0; row < MAIN_ROWS; row++) {
    for (int col = 0; col < MAIN_COLS; col++) {
      bool down = keyDown[row][col];
      if (down != lastKeyDown[row][col]) {
        Serial.printf("Right %s: row %d, col %d -> %s\n", down ? "pressed" : "released",
                      row, col, keyLabels[row][col]);
        lastKeyDown[row][col] = down;
      }
    }
  }
}

// Restart combo, right side only - see RESTART_KEY_A/B above
void checkRestartCombo() {
  bool a = keyDown[RESTART_KEY_A_ROW][RESTART_KEY_A_COL];
  bool b = keyDown[RESTART_KEY_B_ROW][RESTART_KEY_B_COL];

  if (a && b) {
    if (!restartComboActive) {
      Serial.println("Restart combo held - keep holding to restart...");
    }
    // Stays active until BOTH keys are released, so a key still held
    // after letting go of the other doesn't reappear on the host
    restartComboActive = true;
    if (restartComboSince == 0) restartComboSince = millis() | 1;
    if (millis() - restartComboSince >= RESTART_HOLD_MS) {
      Serial.println("\n=== Restart combo - restarting ESP32 ===");
      sendKeyDataToLeft();  // combo keys already reported as released
      delay(100);
      Serial.flush();
      ESP.restart();
    }
  } else {
    restartComboSince = 0;
    if (!a && !b) restartComboActive = false;
  }
}

// Any right side key held counts as activity
void checkSleep() {
  for (int row = 0; row < MAIN_ROWS; row++) {
    for (int col = 0; col < MAIN_COLS; col++) {
      if (keyDown[row][col]) {
        lastActivityTime = millis();
        return;
      }
    }
  }
  if (millis() - lastActivityTime >= SLEEP_TIMEOUT_MS) {
    if (leftConnected) {
      pLeftClient->disconnect();  // let the left side clear our keys cleanly
      delay(100);
    }
    goToSleep();
  }
}

// ---- Sleep ----
// Light sleep with every row driven LOW: any key press pulls its column LOW
// and wakes the chip (works on every column pin, unlike deep sleep's ext1).
// Bluetooth must be off to sleep, so on wake we just restart and reconnect.
void goToSleep() {
  Serial.println("\n=== Idle - going to sleep (press any key to wake) ===");
  Serial.flush();

  esp_bluedroid_disable();
  esp_bt_controller_disable();

  for (int i = 0; i < MAIN_ROWS; i++) {
    digitalWrite(mainRowPins[i], LOW);
  }
  for (int i = 0; i < MAIN_COLS; i++) {
    gpio_wakeup_enable((gpio_num_t)mainColPins[i], GPIO_INTR_LOW_LEVEL);
  }
  esp_sleep_enable_gpio_wakeup();
  esp_light_sleep_start();

  // Woke up from a key press
  ESP.restart();
}

// Connect to the left side's BLE server and find its characteristic
bool connectToLeft() {
  if (pLeftServerAddress == nullptr) {
    Serial.println("No left side server address available");
    return false;
  }

  Serial.printf("Connecting to %s\n", pLeftServerAddress->toString().c_str());
  if (!pLeftClient->connect(*pLeftServerAddress)) {
    Serial.println(" - Connection failed");
    return false;
  }

  BLERemoteService* pRemoteService = pLeftClient->getService(LEFT_SERVICE_UUID);
  if (pRemoteService == nullptr) {
    Serial.println(" - Left side service not found (is the left sketch up to date?)");
    pLeftClient->disconnect();
    return false;
  }

  pLeftCharacteristic = pRemoteService->getCharacteristic(LEFT_CHARACTERISTIC_UUID);
  if (pLeftCharacteristic == nullptr) {
    Serial.println(" - Left side characteristic not found");
    pLeftClient->disconnect();
    return false;
  }

  return true;
}

// Send the pressed key positions to the left side
void sendKeyDataToLeft() {
  if (!leftConnected || pLeftCharacteristic == nullptr) return;

  KeyPacket packet = {0, 0};
  for (int row = 0; row < MAIN_ROWS; row++) {
    for (int col = 0; col < MAIN_COLS; col++) {
      if (!keyDown[row][col]) continue;
      // Hide the restart combo keys so they don't repeat on the host
      if (restartComboActive &&
          ((row == RESTART_KEY_A_ROW && col == RESTART_KEY_A_COL) ||
           (row == RESTART_KEY_B_ROW && col == RESTART_KEY_B_COL))) continue;
      packet.keys |= (1UL << (row * MAIN_COLS + col));
    }
  }
  pLeftCharacteristic->writeValue((uint8_t*)&packet, sizeof(KeyPacket), false);
}
