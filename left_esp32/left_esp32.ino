/*
 * Split Keyboard - Left Side (ESP32)
 * Master / Relay: BLE Keyboard to host system

 * Architecture:
 *   - Left side: ESP32 (this device) - BLE keyboard to host
 *   - Right side: ESP32 - BLE client, sends its raw key positions here
 *   - This device owns the keymap for BOTH halves, combines them and sends
 *     one HID report to the host
 *
 * Pin Configuration:
 *   Rows (Outputs, top to bottom): 14, 27, 26, 23 (main), 22 (thumb cluster)
 *   Columns (Inputs, left to right): 13, 32, 33, 25, 19, 21
 *
 * BLE Communication:
 *   - BLE Keyboard: connects to the host as KEYBOARD_NAME
 *   - BLE Server: receives right side key data from the right ESP32
 *
 * Key combos:
 *   - Hold Esc + Shift (left outer column, top + bottom) for 2 s: restart this
 *     ESP32 (right side reconnects on its own)
 *   - Fn + Space: turn auto period / lone "i" -> "I" on or off
 *   - Fn + Shift + 1/2/3: switch to host device 1/2/3 (see switchDevice)
 *   - Hold Fn + Shift + 1/2/3 for 3 s: forget that device's host and pair a
 *     new one (see pairNewHost)
 *
 * Sleep: after SLEEP_TIMEOUT_MS with no key pressed on either half, this
 *   side sleeps. Press any LEFT key to wake it (it restarts and the host
 *   reconnects). The right side sleeps on its own timer.
 */

#include "BleKeyboard.h"  // bundled in this sketch folder (patched T-vK ESP32-BLE-Keyboard 0.3.2)
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "esp_sleep.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "driver/gpio.h"
#include <Preferences.h>
#if __has_include("esp_mac.h")
#include "esp_mac.h"  // esp_base_mac_addr_set (ESP32 Arduino core 3.x)
#endif

// Name the host sees, and the name the right side scans for.
// BleKeyboard truncates names to 15 chars, and the advertising packet only
// has room for a short name, so keep this short. Must match right_esp32.ino.
#define KEYBOARD_NAME "Split Keyboard"

// Service and characteristic UUIDs for right side communication
#define SERVICE_UUID        "12345678-1234-1234-1234-123456789abc"
#define CHARACTERISTIC_UUID "87654321-4321-4321-4321-cba987654321"

// Matrix dimensions (same on both halves; row 4 is the thumb cluster)
const int MAIN_ROWS = 5;
const int MAIN_COLS = 6;

// Rows (top to bottom): 14, 27, 26, 23 (main), 22 (thumb cluster)
const int mainRowPins[MAIN_ROWS] = {14, 27, 26, 23, 22};
// Columns (left to right)
const int mainColPins[MAIN_COLS] = {13, 32, 33, 25, 19, 21};

// ---- Key codes ----
// Each keymap entry is a uint16_t:
//   'a', '1', '#', ' ' ...  a printable character; sent as the US-layout key
//                           (plus Shift if needed) that types it
//   K_xxx                   a named key (below)
//   NONE                    no key at this position
//   SAME                    (Shift / Fn layers) do what the base layer does
#define HID(code) ((uint16_t)(0x100 | (code)))  // raw HID usage code
constexpr uint16_t NONE     = 0;
constexpr uint16_t SAME     = 0;
constexpr uint16_t K_ENTER  = HID(0x28);
constexpr uint16_t K_ESC    = HID(0x29);
constexpr uint16_t K_BSPC   = HID(0x2A);
constexpr uint16_t K_TAB    = HID(0x2B);
constexpr uint16_t K_RIGHT  = HID(0x4F);
constexpr uint16_t K_LEFT   = HID(0x50);
constexpr uint16_t K_DOWN   = HID(0x51);
constexpr uint16_t K_UP     = HID(0x52);
constexpr uint16_t K_PGUP   = HID(0x4B);
constexpr uint16_t K_PGDN   = HID(0x4E);
constexpr uint16_t K_LCTRL  = HID(0xE0);  // modifiers: HID 0xE0-0xE7
constexpr uint16_t K_LSHIFT = HID(0xE1);
constexpr uint16_t K_LALT   = HID(0xE2);
constexpr uint16_t K_LGUI   = HID(0xE3);  // Super / Windows / Command
constexpr uint16_t K_FN     = 0x200;      // Fn layer key, never sent to the host
constexpr uint16_t K_AUTOTEXT = 0x201;    // toggles auto period + lone i, never sent
#define K_F(n) HID(0x3A + (n) - 1)        // F1..F12

enum Side  { LEFT, RIGHT, SIDES };
enum Layer { BASE, SHIFTED, FN, LAYERS };

// ---- Keymap ----
// SHIFTED only lists keys whose shifted character isn't the usual US one;
// everything else gets Shift from the host like a normal keyboard.
// Thumb row: left cols 0-4 are real keys (col 5 has none);
//            right cols 1-5 are real keys (col 0 has none).
const uint16_t keymap[SIDES][LAYERS][MAIN_ROWS][MAIN_COLS] = {
  {  // LEFT
    {  // BASE
      {K_ESC,    '1',    '2',  '3',  '4',  '5'},
      {K_TAB,    'q',    'w',  'e',  'r',  't'},
      {K_LCTRL,  'a',    's',  'd',  'f',  'g'},
      {K_LSHIFT, 'z',    'x',  'c',  'v',  'b'},
      {K_ENTER,  K_LGUI, K_LALT, ']',  '[',  NONE},
    },
    {},  // SHIFTED: all SAME
    {  // FN
      {SAME, K_F(1), K_F(2), K_F(3), K_F(4), K_F(5)},
      {SAME, K_PGUP, K_UP,   K_PGDN, SAME,   SAME},  // Fn + Q W E
      {SAME, K_LEFT, K_DOWN, K_RIGHT, SAME,  SAME},  // Fn + A S D
    },
  },
  {  // RIGHT
    {  // BASE
      {'6',  '7', '8', '9',    '0',    '-'},
      {'y',  'u', 'i', 'o',    'p',    '='},
      {'h',  'j', 'k', 'l',    ';',    '\''},
      {'n',  'm', ',', '.',    '/',    '\\'},
      {NONE, '#', '`', K_BSPC, K_FN,   ' '},
    },
    {  // SHIFTED
      {}, {}, {}, {},
      {SAME, '~', '@', SAME, SAME, SAME},
    },
    {  // FN
      {K_F(6), K_F(7), K_F(8), K_F(9), K_F(10), K_F(11)},
      {SAME,   SAME,   SAME,   SAME,   SAME,    K_F(12)},
      {},
      {},
      {SAME,   SAME,   SAME,   SAME,   SAME,    K_AUTOTEXT},  // Fn + Space
    },
  },
};

// ---- State ----
// Debounced key states for both halves. The left half is scanned here, the
// right half arrives over BLE.
bool keyDown[SIDES][MAIN_ROWS][MAIN_COLS] = {};
bool lastLeftKeyDown[MAIN_ROWS][MAIN_COLS] = {};  // for debounce + change log
uint8_t rightModifierBits = 0;  // from the right side's packet, see KeyPacket

KeyReport lastReport = {};  // last HID report sent to the host

Preferences prefs;  // settings kept across restarts

// Which of the 3 host devices we're on (0-2). Each one uses its own
// Bluetooth address, so every host pairs with "its own" keyboard.
const int DEVICE_SLOTS = 3;
uint8_t deviceSlot = 0;
int switchKeySide = -1, switchKeyRow = 0, switchKeyCol = 0;  // hidden from the host while held
// Fn + Shift + number: tap to switch, hold PAIR_HOLD_MS to pair a new host
const unsigned long PAIR_HOLD_MS = 3000;
int pendingSlot = -1;  // slot whose number key is down (tap or hold not decided yet)
unsigned long pendingSlotSince = 0;

// Which bond belongs to which slot, so pairing a new host on one slot can
// forget just that slot's old host. Filled in after each host pairs.
const int MAX_BONDS = 16;
esp_bd_addr_t bootBonds[MAX_BONDS];  // bonds that existed when we started
int bootBondCount = 0;
volatile bool hostAuthDone = false;  // set from the BLE stack, handled in loop()
esp_bd_addr_t hostAuthAddr;
esp_bd_addr_t rightAddr;
bool rightAddrKnown = false;

// Auto text (see "Auto period + lone i" below)
const unsigned long TEXT_MEMORY_MS = 10000;
const int TEXT_HISTORY_LEN = 16;
char textHistory[TEXT_HISTORY_LEN];
int textHistoryLen = 0;
unsigned long lastTextTime = 0;
bool autoTextEnabled = true;
bool prevKeyDown[SIDES][MAIN_ROWS][MAIN_COLS] = {};  // for spotting new presses
int iSide = -1, iRow = 0, iCol = 0;           // key that last typed 'i'
int hiddenSide = -1, hiddenRow = 0, hiddenCol = 0;  // 'i' key hidden after being retyped as 'I'

// Debounce delay
const unsigned long DEBOUNCE_DELAY = 5;

// BLE: the right side writes key data to this characteristic
BLECharacteristic* pDataCharacteristic = nullptr;
bool rightConnected = false;
bool oldRightConnected = false;

// Restart combo: hold these two left keys together for RESTART_HOLD_MS
const int RESTART_KEY_A_ROW = 0, RESTART_KEY_A_COL = 0;  // Esc
const int RESTART_KEY_B_ROW = 3, RESTART_KEY_B_COL = 0;  // Shift
const unsigned long RESTART_HOLD_MS = 2000;
bool restartComboActive = false;   // combo keys are hidden from the host
unsigned long restartComboSince = 0;

// Sleep after this long with no key pressed on either half
const unsigned long SLEEP_TIMEOUT_MS = 30UL * 60UL * 1000UL;  // 30 minutes
unsigned long lastActivityTime = 0;

// What actually goes over BLE: 5 bytes, so it fits in one packet even at the
// default 23-byte MTU (20 bytes of payload).
//   keys:      bit (row * MAIN_COLS + col) set = key pressed (30 bits)
//   modifiers: bit 0 ctrl, 1 shift, 2 alt, 3 super, 4 fn
//              (bits 0-3 line up with the HID modifier byte)
// Must match the packet in the other side's sketch.
struct __attribute__((packed)) KeyPacket {
  uint32_t keys;
  uint8_t modifiers;
};

String keyName(uint16_t k);

// Host and right side both connect to the same GATT server, so we tell them
// apart by connection id: the right side is whichever connection writes
// key data to our characteristic (it writes continuously while connected).
volatile int rightConnId = -1;

// Clear right side key states so keys don't get stuck
void clearRightSideState() {
  memset(keyDown[RIGHT], 0, sizeof(keyDown[RIGHT]));
  rightModifierBits = 0;
}

// BLE Characteristic callbacks for receiving data from right side
class RightSideCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic, esp_ble_gatts_cb_param_t *param) override {
    if (rightConnId != param->write.conn_id) {
      rightConnId = param->write.conn_id;
      rightConnected = true;
      memcpy(rightAddr, param->write.bda, sizeof(esp_bd_addr_t));
      rightAddrKnown = true;
    }
    String rxValue = pCharacteristic->getValue();

    if (rxValue.length() != sizeof(KeyPacket)) {
      static unsigned long lastBadLenLog = 0;
      if (millis() - lastBadLenLog > 2000) {
        Serial.printf("Right side sent %u bytes, expected %u - is the right sketch up to date?\n",
                      (unsigned)rxValue.length(), (unsigned)sizeof(KeyPacket));
        lastBadLenLog = millis();
      }
      return;
    }

    KeyPacket packet;
    memcpy(&packet, rxValue.c_str(), sizeof(KeyPacket));
    for (int row = 0; row < MAIN_ROWS; row++) {
      for (int col = 0; col < MAIN_COLS; col++) {
        bool down = (packet.keys >> (row * MAIN_COLS + col)) & 1;
        if (down != keyDown[RIGHT][row][col]) {
          Serial.printf("Right %s: row %d, col %d -> %s\n", down ? "pressed" : "released",
                        row, col, keyName(keymap[RIGHT][BASE][row][col]).c_str());
        }
        keyDown[RIGHT][row][col] = down;
      }
    }
    rightModifierBits = packet.modifiers;
  }
};

// BleKeyboard owns the one GATT server and its connection callbacks.
// Replacing those callbacks broke the keyboard's own connect/disconnect
// handling, and every new connection stops advertising, so once the right
// side connected the host could never see us.
class SplitKeyboard : public BleKeyboard {
  public:
    using BleKeyboard::BleKeyboard;

  protected:
    // Called by begin() after the HID services start, before advertising is
    // configured - add the right side's service to the same server here.
    void onStarted(BLEServer *pServer) override {
      BLEService *pCommService = pServer->createService(SERVICE_UUID);
      pDataCharacteristic = pCommService->createCharacteristic(
                             CHARACTERISTIC_UUID,
                             BLECharacteristic::PROPERTY_READ |
                             BLECharacteristic::PROPERTY_WRITE |
                             BLECharacteristic::PROPERTY_WRITE_NR  // right side writes without response
                           );
      pDataCharacteristic->setCallbacks(new RightSideCallbacks());
      pCommService->start();
      // Not added to the advertisement: a 128-bit UUID would push the name
      // out of the packet. The right side finds us by name instead.
    }

    // Handled in the overloads below (which carry the connection id).
    void onConnect(BLEServer *pServer) override {}
    void onDisconnect(BLEServer *pServer) override {}

    void onConnect(BLEServer *pServer, esp_ble_gatts_cb_param_t *param) override {
      Serial.printf("\n=== BLE connection (conn_id %d) ===\n", param->connect.conn_id);
      // We can't tell host from right side yet; enabling HID notifications
      // is harmless for the right side.
      BleKeyboard::onConnect(pServer);
      // Count doesn't include this connection yet. Keep advertising until
      // both the host and the right side are connected.
      if (pServer->getConnectedCount() == 0) {
        BLEDevice::startAdvertising();
      }
    }

    void onDisconnect(BLEServer *pServer, esp_ble_gatts_cb_param_t *param) override {
      if (param->disconnect.conn_id == rightConnId) {
        Serial.println("\n=== Right side ESP32 disconnected ===");
        rightConnId = -1;
        rightConnected = false;
        clearRightSideState();
        BLEDevice::startAdvertising();
      } else {
        Serial.println("\n=== Host disconnected ===");
        BleKeyboard::onDisconnect(pServer);  // also restarts advertising
      }
    }
};

// BLE keyboard instance (connects to the host system)
SplitKeyboard bleKeyboard(KEYBOARD_NAME, "Split Keyboard", 100);

void setup() {
  Serial.begin(115200);
  delay(1000); // Give serial time to initialize
  Serial.println("\n\nLeft Split Keyboard - ESP32 (Relay/Master)");

  for (int i = 0; i < MAIN_ROWS; i++) {
    pinMode(mainRowPins[i], OUTPUT);
    digitalWrite(mainRowPins[i], HIGH);
  }
  for (int i = 0; i < MAIN_COLS; i++) {
    pinMode(mainColPins[i], INPUT_PULLUP);
  }

  // Also sets up the right side service (SplitKeyboard::onStarted) and
  // starts advertising.
  prefs.begin("keyboard", false);
  autoTextEnabled = prefs.getBool("autoText", true);
  Serial.printf("Auto text: %s (Fn + Space to toggle)\n", autoTextEnabled ? "ON" : "OFF");

  // Must happen before Bluetooth starts. Slot 0 keeps the factory address,
  // so a host paired before this feature existed is still device 1.
  deviceSlot = prefs.getUChar("slot", 0);
  if (deviceSlot >= DEVICE_SLOTS) deviceSlot = 0;
  uint8_t mac[6];
  esp_efuse_mac_get_default(mac);
  mac[5] += deviceSlot * 4;  // the chip derives up to 4 addresses from the base
  // Each "pair a new host" gives the slot a fresh address, so the old host
  // can't reconnect and the new one sees a brand new keyboard.
  uint8_t gen = prefs.getUChar(slotKey("gen", deviceSlot).c_str(), 0);
  if (gen) {
    mac[0] ^= 0x02;  // locally administered, never clashes with the factory address
    mac[4] ^= gen;
  }
  esp_base_mac_addr_set(mac);
  Serial.printf("Device %d of %d (Fn + Shift + 1/2/3 to switch, hold 3 s to pair a new host)\n",
                deviceSlot + 1, DEVICE_SLOTS);

  BLEDevice::setCustomGapHandler(onGapEvent);
  Serial.println("Starting BLE Keyboard...");
  bleKeyboard.begin();
  bootBondCount = getBonds(bootBonds);
  Serial.println("Scan for '" KEYBOARD_NAME "' and connect");
}

void loop() {
  // Advertising is restarted from SplitKeyboard::onDisconnect
  if (rightConnected != oldRightConnected) {
    if (rightConnected) Serial.println("Right side ESP32 connected");
    oldRightConnected = rightConnected;
  }

  static bool wasConnected = false;
  bool isConnected = bleKeyboard.isConnected();
  if (isConnected != wasConnected) {
    Serial.println(isConnected ? "\n=== Host CONNECTED ===" : "\n=== Host DISCONNECTED ===");
    wasConnected = isConnected;
  }

  scanMatrix();
  logLeftKeyChanges();
  checkRestartCombo();  // before sendKeys so the combo keys get hidden
  checkSleep();
  sendKeys();
  rememberHost();

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
      if (isPressed != lastLeftKeyDown[row][col]) {
        delay(DEBOUNCE_DELAY);
        bool secondRead = (digitalRead(mainColPins[col]) == LOW);
        keyDown[LEFT][row][col] = (isPressed == secondRead) ? isPressed : lastLeftKeyDown[row][col];
      } else {
        keyDown[LEFT][row][col] = isPressed;
      }
    }

    digitalWrite(mainRowPins[row], HIGH);
    delayMicroseconds(5);
  }
}

void logLeftKeyChanges() {
  for (int row = 0; row < MAIN_ROWS; row++) {
    for (int col = 0; col < MAIN_COLS; col++) {
      bool down = keyDown[LEFT][row][col];
      if (down != lastLeftKeyDown[row][col]) {
        Serial.printf("Left %s: row %d, col %d -> %s\n", down ? "pressed" : "released",
                      row, col, keyName(keymap[LEFT][BASE][row][col]).c_str());
        lastLeftKeyDown[row][col] = down;
      }
    }
  }
}

// Restart combo: hold Esc (top-left) and Shift (bottom-left of the main
// block) together for RESTART_HOLD_MS - mirrors the right side's combo.
// Once both are down they're hidden from the host until both are released.
// Restarting the left side drops the right side's BLE link; the right side
// rescans and reconnects on its own.
void checkRestartCombo() {
  bool a = keyDown[LEFT][RESTART_KEY_A_ROW][RESTART_KEY_A_COL];
  bool b = keyDown[LEFT][RESTART_KEY_B_ROW][RESTART_KEY_B_COL];

  if (a && b) {
    if (!restartComboActive) {
      Serial.println("Restart combo held - keep holding to restart...");
    }
    restartComboActive = true;
    if (restartComboSince == 0) restartComboSince = millis() | 1;
    if (millis() - restartComboSince >= RESTART_HOLD_MS) {
      Serial.println("\n=== Restart combo (Esc+Shift) - restarting ESP32 ===");
      bleKeyboard.releaseAll();  // don't leave anything held on the host
      delay(100);                // let the release report go out
      Serial.flush();
      ESP.restart();
    }
  } else {
    restartComboSince = 0;
    if (!a && !b) restartComboActive = false;
  }
}

bool isRestartComboKey(int side, int row, int col) {
  return side == LEFT &&
         ((row == RESTART_KEY_A_ROW && col == RESTART_KEY_A_COL) ||
          (row == RESTART_KEY_B_ROW && col == RESTART_KEY_B_COL));
}

// Any key held on either half counts as activity
void checkSleep() {
  for (int side = 0; side < SIDES; side++) {
    for (int row = 0; row < MAIN_ROWS; row++) {
      for (int col = 0; col < MAIN_COLS; col++) {
        if (keyDown[side][row][col]) {
          lastActivityTime = millis();
          return;
        }
      }
    }
  }
  if (millis() - lastActivityTime >= SLEEP_TIMEOUT_MS) {
    bleKeyboard.releaseAll();
    delay(100);
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

// ---- Switching host devices ----
// Fn + Shift + 1/2/3. The Bluetooth address can only change before Bluetooth
// starts, so save the slot and restart. The new slot's host reconnects (or,
// the first time, pair it from that host), and the right side rescans by
// name and reconnects on its own.
void switchDevice(int slot) {
  if (slot == deviceSlot) {
    Serial.printf("Already on device %d\n", slot + 1);
    return;
  }
  Serial.printf("\n=== Switching to device %d - restarting ===\n", slot + 1);
  bleKeyboard.releaseAll();  // don't leave anything held on the old host
  delay(100);
  prefs.putUChar("slot", slot);
  Serial.flush();
  ESP.restart();
}

// ---- Pairing a new host ----
// Hold Fn + Shift + 1/2/3 for PAIR_HOLD_MS. Forgets the host that slot was
// paired with, gives the slot a new Bluetooth address and restarts. The
// keyboard then shows up as a new "Split Keyboard" for any host to pair.
// The old host keeps a dead "Split Keyboard" entry; remove it there.
void pairNewHost(int slot) {
  Serial.printf("\n=== Pairing a new host on device %d - restarting ===\n", slot + 1);
  bleKeyboard.releaseAll();  // don't leave anything held on the current host
  delay(100);

  uint8_t old[6];
  String hostKey = slotKey("host", slot);
  if (prefs.getBytes(hostKey.c_str(), old, sizeof(old)) == sizeof(old)) {
    esp_ble_remove_bond_device(old);
    prefs.remove(hostKey.c_str());
    Serial.printf("Forgot old host %02X:%02X:%02X:%02X:%02X:%02X\n",
                  old[0], old[1], old[2], old[3], old[4], old[5]);
  }
  String genKey = slotKey("gen", slot);
  uint8_t gen = prefs.getUChar(genKey.c_str(), 0) + 1;
  if (gen == 0) gen = 1;  // 0 means the original address
  prefs.putUChar(genKey.c_str(), gen);
  prefs.putUChar("slot", slot);

  delay(300);  // let the bond removal finish
  Serial.flush();
  ESP.restart();
}

String slotKey(const char* name, int slot) { return String(name) + slot; }

// Up to MAX_BONDS bonded addresses into out, returns how many
int getBonds(esp_bd_addr_t* out) {
  int n = esp_ble_get_bond_device_num();
  if (n <= 0) return 0;
  if (n > MAX_BONDS) n = MAX_BONDS;
  esp_ble_bond_dev_t list[MAX_BONDS];
  if (esp_ble_get_bond_device_list(&n, list) != ESP_OK) return 0;
  for (int i = 0; i < n; i++) memcpy(out[i], list[i].bd_addr, sizeof(esp_bd_addr_t));
  return n;
}

bool addrIn(const uint8_t* addr, esp_bd_addr_t* list, int n) {
  for (int i = 0; i < n; i++) {
    if (memcmp(addr, list[i], sizeof(esp_bd_addr_t)) == 0) return true;
  }
  return false;
}

// Runs on the BLE stack's task: just note it, rememberHost() does the work
void onGapEvent(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param) {
  if (event == ESP_GAP_BLE_AUTH_CMPL_EVT && param->ble_security.auth_cmpl.success) {
    memcpy(hostAuthAddr, param->ble_security.auth_cmpl.bd_addr, sizeof(esp_bd_addr_t));
    hostAuthDone = true;
  }
}

// After a host pairs (or reconnects), save which bond is this slot's host.
// A bond that's new since startup is the one just made; otherwise it's a
// host that was already bonded.
void rememberHost() {
  if (!hostAuthDone) return;
  hostAuthDone = false;
  if (rightAddrKnown && memcmp(hostAuthAddr, rightAddr, sizeof(esp_bd_addr_t)) == 0) return;

  esp_bd_addr_t bonds[MAX_BONDS];
  int n = getBonds(bonds);
  const uint8_t* host = nullptr;
  for (int i = 0; i < n && !host; i++) {
    bool isRight = rightAddrKnown && memcmp(bonds[i], rightAddr, sizeof(esp_bd_addr_t)) == 0;
    if (!isRight && !addrIn(bonds[i], bootBonds, bootBondCount)) host = bonds[i];
  }
  if (!host && addrIn(hostAuthAddr, bonds, n)) host = hostAuthAddr;
  if (!host) return;

  prefs.putBytes(slotKey("host", deviceSlot).c_str(), host, sizeof(esp_bd_addr_t));
  Serial.printf("Device %d host is %02X:%02X:%02X:%02X:%02X:%02X\n", deviceSlot + 1,
                host[0], host[1], host[2], host[3], host[4], host[5]);
}

// ---- Building the HID report ----

const uint16_t WITH_SHIFT = 0x100;  // flag in asciiToHid's result

// US-layout HID usage code for a printable character, with WITH_SHIFT set
// if Shift is needed to type it. 0 if unknown.
uint16_t asciiToHid(char c) {
  if (c >= 'a' && c <= 'z') return 0x04 + (c - 'a');
  if (c >= 'A' && c <= 'Z') return WITH_SHIFT | (0x04 + (c - 'A'));
  if (c >= '1' && c <= '9') return 0x1E + (c - '1');
  if (c == '0') return 0x27;
  if (c == ' ') return 0x2C;

  static const char shiftedDigits[] = "!@#$%^&*()";  // Shift + 1..0
  for (int i = 0; i < 10; i++) {
    if (c == shiftedDigits[i]) return WITH_SHIFT | (0x1E + i);
  }

  static const char    plain[]   = "-=[]\\;'`,./";
  static const char    shifted[] = "_+{}|:\"~<>?";
  static const uint8_t codes[]   = {0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38};
  for (int i = 0; i < 11; i++) {
    if (c == plain[i])   return codes[i];
    if (c == shifted[i]) return WITH_SHIFT | codes[i];
  }
  return 0;
}

// True if any held key on either half is `code` on the base layer
bool baseKeyHeld(uint16_t code) {
  for (int side = 0; side < SIDES; side++) {
    for (int row = 0; row < MAIN_ROWS; row++) {
      for (int col = 0; col < MAIN_COLS; col++) {
        if (keyDown[side][row][col] && keymap[side][BASE][row][col] == code &&
            !(restartComboActive && isRestartComboKey(side, row, col))) {
          return true;
        }
      }
    }
  }
  return false;
}

// ---- Auto period + lone i ----
// Like a phone keyboard: two spaces after a word become ". ", and a lone
// "i" becomes "I". (No sentence-start caps: it got in the way in terminals.)
// The keyboard can't see the screen, so it keeps its own short memory of
// what was just typed. That memory is wiped after TEXT_MEMORY_MS with no key
// presses (you've probably clicked somewhere else), and by Tab, Esc, F-keys, arrows
// and Ctrl/Alt/Super shortcuts. With an empty memory it does nothing.
// Fn + Space turns the whole thing on/off (remembered across restarts).
// (state is up in the State section)

void forgetText() { textHistoryLen = 0; }

void rememberChar(char c) {
  if (textHistoryLen == TEXT_HISTORY_LEN) {
    memmove(textHistory, textHistory + 1, TEXT_HISTORY_LEN - 1);
    textHistoryLen--;
  }
  textHistory[textHistoryLen++] = c;
}

// The US-layout character Shift + c types
char shiftedChar(char c) {
  if (c >= 'a' && c <= 'z') return c - 'a' + 'A';
  static const char from[] = "1234567890-=[]\\;',./`";
  static const char to[]   = "!@#$%^&*()_+{}|:\"<>?~";
  const char* p = strchr(from, c);
  return (c && p) ? to[p - from] : c;
}

// A word (letter, digit, or closing bracket/quote) followed by one space
bool afterWordAndSpace() {
  if (textHistoryLen < 2 || textHistory[textHistoryLen - 1] != ' ') return false;
  char c = textHistory[textHistoryLen - 2];
  return isalnum((unsigned char)c) || c == ')' || c == '"' || c == '\'';
}

// Press and release one key on top of whatever is already held (minus Shift,
// so '.' doesn't come out as '>'), with extra modifiers if given
void tapKey(uint8_t hid, uint8_t mods = 0) {
  KeyReport r = lastReport;
  r.modifiers &= ~0x22;
  for (int i = 0; i < 6; i++) {
    if (r.keys[i] == hid) r.keys[i] = 0;  // still held: release so the tap is a new press
  }
  KeyReport down = r;
  down.modifiers |= mods;
  for (int i = 0; i < 6; i++) {
    if (down.keys[i] == 0) { down.keys[i] = hid; break; }
  }
  bleKeyboard.sendReport(&down);
  delay(10);
  bleKeyboard.sendReport(&r);
  delay(10);
  lastReport = r;
}

// The last thing typed is a lowercase "i" on its own (after a space, Enter,
// '(' or '"'), not the end of a longer word
bool loneLowerI() {
  if (textHistoryLen < 2 || textHistory[textHistoryLen - 1] != 'i') return false;
  char before = textHistory[textHistoryLen - 2];
  return before == ' ' || before == '\n' || before == '(' || before == '"';
}

// Backspace over the "i" and type "I". If the i key is still down (fast
// typing), it's released by the tap, so hide it until it's let go or the
// host would see it pressed again.
void capitalizeLoneI() {
  tapKey(0x2A);        // Backspace
  tapKey(0x0C, 0x02);  // Shift + i
  textHistory[textHistoryLen - 1] = 'I';
  if (iSide >= 0 && keyDown[iSide][iRow][iCol]) {
    hiddenSide = iSide; hiddenRow = iRow; hiddenCol = iCol;
  }
  Serial.println("Auto I");
}

// Called once for each new key press, before the report goes out.
// k is the keymap entry after layers; typed is the character it types (0 if none).
void onKeyPress(int side, int row, int col, uint16_t k, char typed, bool chordHeld) {
  if (k == K_AUTOTEXT) {
    autoTextEnabled = !autoTextEnabled;
    prefs.putBool("autoText", autoTextEnabled);
    forgetText();
    Serial.printf("Auto text: %s\n", autoTextEnabled ? "ON" : "OFF");
    return;
  }
  if (!autoTextEnabled || k == NONE || k == K_FN) return;
  if ((k & 0x100) && (k & 0xFF) >= 0xE0 && (k & 0xFF) <= 0xE7) return;  // modifier alone

  if (millis() - lastTextTime > TEXT_MEMORY_MS) forgetText();
  lastTextTime = millis();

  if (chordHeld) { forgetText(); return; }  // Ctrl/Alt/Super shortcut
  if (k == K_ENTER) { forgetText(); rememberChar('\n'); return; }
  if (k == K_BSPC) { if (textHistoryLen > 0) textHistoryLen--; return; }
  if (!typed) { forgetText(); return; }  // Tab, Esc, F-keys, arrows...

  // Word ended right after a lone "i": " i " / " i," / " i'm" -> "I"
  if (strchr(" ,.?!;:'", typed) && loneLowerI()) capitalizeLoneI();

  if (typed == ' ' && afterWordAndSpace()) {
    // The held Space goes out in the normal report right after this
    tapKey(0x2A);  // Backspace over the first space
    tapKey(0x37);  // '.'
    textHistoryLen--;
    rememberChar('.');
    rememberChar(' ');
    Serial.println("Auto period");
    return;
  }

  if (typed == 'i') { iSide = side; iRow = row; iCol = col; }
  rememberChar(typed);
}

// Build one HID report from both halves and send it if it changed.
// The whole report is sent at once, so Shift/Ctrl/etc. are always exactly
// what's physically held (plus Shift for characters that need it).
void sendKeys() {
  bool fnHeld    = baseKeyHeld(K_FN)     || (rightModifierBits & 0x10);
  bool shiftHeld = baseKeyHeld(K_LSHIFT) || (rightModifierBits & 0x02);
  int layer = fnHeld ? FN : (shiftHeld ? SHIFTED : BASE);

  bool chordHeld = baseKeyHeld(K_LCTRL) || baseKeyHeld(K_LALT) || baseKeyHeld(K_LGUI) ||
                   (rightModifierBits & 0x0D);  // ctrl, alt, super

  // New presses: device switching, auto text
  for (int side = 0; side < SIDES; side++) {
    for (int row = 0; row < MAIN_ROWS; row++) {
      for (int col = 0; col < MAIN_COLS; col++) {
        bool down = keyDown[side][row][col] &&
                    !(restartComboActive && isRestartComboKey(side, row, col));
        if (down && !prevKeyDown[side][row][col]) {
          uint16_t base = keymap[side][BASE][row][col];
          if (fnHeld && shiftHeld && base >= '1' && base < '1' + DEVICE_SLOTS) {
            // Tap or hold is decided below
            pendingSlot = base - '1';
            pendingSlotSince = millis();
            switchKeySide = side; switchKeyRow = row; switchKeyCol = col;
            prevKeyDown[side][row][col] = true;
            continue;
          }
          uint16_t entry = keymap[side][layer][row][col];
          uint16_t k = (entry == SAME) ? keymap[side][BASE][row][col] : entry;
          char typed = 0;
          if (k != NONE && k < 0x80) typed = (entry == SAME && shiftHeld) ? shiftedChar(k) : (char)k;
          onKeyPress(side, row, col, k, typed, chordHeld);
        }
        prevKeyDown[side][row][col] = down;
      }
    }
  }

  if (switchKeySide >= 0) {
    bool held = keyDown[switchKeySide][switchKeyRow][switchKeyCol];
    if (held && pendingSlot >= 0 && millis() - pendingSlotSince >= PAIR_HOLD_MS) {
      pairNewHost(pendingSlot);  // restarts
    }
    if (!held) {
      switchKeySide = -1;
      if (pendingSlot >= 0) {
        int slot = pendingSlot;
        pendingSlot = -1;
        switchDevice(slot);  // restarts unless already on that slot
      }
    }
  }
  if (hiddenSide >= 0 && !keyDown[hiddenSide][hiddenRow][hiddenCol]) hiddenSide = -1;

  KeyReport report = {};
  report.modifiers = rightModifierBits & 0x0F;  // ctrl/shift/alt/super bits match HID
  bool addShift = false;
  int keyCount = 0;

  for (int side = 0; side < SIDES; side++) {
    for (int row = 0; row < MAIN_ROWS; row++) {
      for (int col = 0; col < MAIN_COLS; col++) {
        if (!keyDown[side][row][col]) continue;
        if (restartComboActive && isRestartComboKey(side, row, col)) continue;
        if (side == switchKeySide && row == switchKeyRow && col == switchKeyCol) continue;
        if (side == hiddenSide && row == hiddenRow && col == hiddenCol) continue;

        uint16_t k = keymap[side][layer][row][col];
        if (k == SAME) k = keymap[side][BASE][row][col];
        if (k == NONE || k == K_FN || k == K_AUTOTEXT) continue;

        uint8_t hid;
        if (k & 0x100) {               // named key / raw HID code
          hid = k & 0xFF;
          if (hid >= 0xE0 && hid <= 0xE7) {  // modifier
            report.modifiers |= 1 << (hid - 0xE0);
            continue;
          }
        } else {                       // printable character
          uint16_t h = asciiToHid((char)k);
          if (h & WITH_SHIFT) addShift = true;
          hid = h & 0xFF;
        }

        if (hid != 0 && keyCount < 6) report.keys[keyCount++] = hid;
      }
    }
  }
  if (addShift) report.modifiers |= 0x02;

  if (memcmp(&report, &lastReport, sizeof(KeyReport)) != 0) {
    bleKeyboard.sendReport(&report);
    lastReport = report;
    Serial.printf("Sent: mod=0x%02X keys=[%02X %02X %02X %02X %02X %02X]%s\n",
                  report.modifiers, report.keys[0], report.keys[1], report.keys[2],
                  report.keys[3], report.keys[4], report.keys[5], fnHeld ? " (Fn)" : "");
  }
}

// Readable name for a keymap entry (for the serial log)
String keyName(uint16_t k) {
  if (k == NONE) return "(none)";
  if (k == ' ')  return "SPACE";
  if (k < 0x80)  return String((char)k);
  switch (k) {
    case K_ENTER:  return "ENTER";
    case K_ESC:    return "ESC";
    case K_BSPC:   return "BACKSPACE";
    case K_TAB:    return "TAB";
    case K_UP:     return "UP";
    case K_DOWN:   return "DOWN";
    case K_LEFT:   return "LEFT";
    case K_RIGHT:  return "RIGHT";
    case K_PGUP:   return "PGUP";
    case K_PGDN:   return "PGDN";
    case K_LCTRL:  return "CTRL";
    case K_LSHIFT: return "SHIFT";
    case K_LALT:   return "ALT";
    case K_LGUI:   return "SUPER";
    case K_FN:     return "FN";
    case K_AUTOTEXT: return "AUTOTEXT";
  }
  if (k >= K_F(1) && k <= K_F(12)) return "F" + String(k - K_F(1) + 1);
  return "0x" + String(k, HEX);
}
