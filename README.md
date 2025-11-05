# Split Keyboard Project

A split keyboard design with Arduino Uno (left side) and ESP32 (right side) controllers, featuring thumb clusters and I2C communication.

## Layout

```
Left Side (Arduino Uno) - Main: 4×6 + Thumb: 1×5
┌─────────────────────────────────┐
│esc 1  2  3  4  5              │
│tab q  w  e  r  t              │
│super a s  d  f  g              │
│shift z x  c  v  b              │
│  [thumb cluster]               │ ← 5 keys (left 5 columns only)
└─────────────────────────────────┘

Right Side (ESP32) - Main: 4×6 + Thumb: 1×5  
┌─────────────────────────────────┐
│ 7  8  9  0  -  =              │
│ u  i  o  p  [  ]              │
│ j  k  l  ;  '  \              │
│ m  ,  .  /  SP SP             │
│  [thumb cluster]               │ ← 5 keys (left 5 columns only)
└─────────────────────────────────┘
```

## Hardware Requirements

### Left Side (Arduino Uno)
- Arduino Uno or compatible board
- 29 mechanical switches (24 main + 5 thumb)
- 4×6 + 1×5 PCB or hand-wired matrix
- USB cable for connection

### Right Side (ESP32)
- ESP32 development board
- 29 mechanical switches (24 main + 5 thumb)
- 4×6 + 1×5 PCB or hand-wired matrix
- Micro USB cable for programming

### Communication
- I2C connection between sides (SDA/SCL wires)
- 3.3V power sharing (optional)

## Pin Configuration

### Arduino Uno (Left Side)
- **Main Matrix Row Pins (Outputs):** 2, 3, 4, 5
- **Main Matrix Column Pins (Inputs):** 6, 7, 8, 9, 10, 11
- **Thumb Cluster Row Pin (Output):** 12
- **Thumb Cluster Column Pins (Inputs):** 6, 7, 8, 9, 10 (shared with main matrix)
- **I2C Communication:** SDA: A4, SCL: A5

### ESP32 (Right Side)
- **Main Matrix Row Pins (Outputs):** 2, 4, 5, 18
- **Main Matrix Column Pins (Inputs):** 21, 22, 23, 25, 26, 27
- **Thumb Cluster Row Pin (Output):** 32
- **Thumb Cluster Column Pins (Inputs):** 21, 22, 23, 25, 26 (shared with main matrix)
- **I2C Communication:** SDA: 33, SCL: 34 (separate pins to avoid conflicts)

## Modifier Key Handling

### Why Modifier Keys Need Special Treatment
Modifier keys like Shift, Ctrl, Alt, Super (Windows/Command), and Fn work differently from regular character keys:

1. **State-based operation:** They remain "pressed" until released
2. **Key combinations:** They modify the behavior of other keys
3. **System integration:** Operating systems expect proper modifier keycodes
4. **Timing:** They must be sent before the keys they modify

### Current Modifier Key Layout

#### Left Side Modifiers
- **Main Matrix:**
  - `esc` (top-left): Escape key
  - `tab` (second row, left): Tab key  
  - `super` (third row, left): Windows/Command key
  - `shift` (fourth row, left): Left Shift key

- **Thumb Cluster:**
  - `ctrl` (leftmost): Left Control key
  - `alt` (second): Left Alt key
  - `fn` (third): Function key (for layer switching)
  - `]` (fourth): Right bracket
  - `[` (fifth): Left bracket

#### Right Side Modifiers
- **Main Matrix:** Standard character keys only
- **Thumb Cluster:** Currently mapped to space keys (customizable)

### How Modifier Handling Works

#### 1. Separate Modifier Tracking
```cpp
// Modifier states are tracked separately
bool ctrlPressed = false;
bool shiftPressed = false;
bool altPressed = false;
bool superPressed = false;
bool fnPressed = false;
```

#### 2. I2C Data Structure
```cpp
struct KeyData {
  bool mainKeys[4][6];      // Regular key states
  bool thumbKeys[5];        // Thumb key states
  bool modifiers[5];        // Modifier states: [ctrl, shift, alt, super, fn]
  unsigned long timestamp;  // For timing validation
};
```

#### 3. Processing Order
1. **Modifiers first:** Send modifier key states before regular keys
2. **Regular keys:** Send character keys with modifiers applied
3. **Combinations:** Handle key combinations properly

#### 4. Example Key Combination
When pressing `Ctrl + C`:
1. Left thumb cluster `ctrl` key is pressed
2. Arduino sends modifier state via I2C
3. ESP32 receives modifier state and sends `KEY_LEFT_CTRL` to system
4. Left main matrix `c` key is pressed
5. ESP32 sends `c` key with `Ctrl` modifier already active
6. System receives `Ctrl + C` combination

### Customizing Modifier Keys

#### Changing Modifier Assignments
```cpp
// In left_arduino_uno.ino
const char thumbKeyMap[THUMB_COLS] = {'ctrl', 'alt', 'fn', ']', '['};

// Change to:
const char thumbKeyMap[THUMB_COLS] = {'shift', 'ctrl', 'alt', 'fn', 'super'};
```

#### Adding New Modifiers
```cpp
// Add new modifier definitions
#define MODIFIER_HYPER    6
#define MODIFIER_MEH      7

// Update modifier arrays and handling
```

#### Layer Switching with Fn Key
```cpp
// Example: Fn key for layer switching
if (leftModifiers[4]) { // Fn key is pressed
  // Switch to function layer
  // Map keys differently
} else {
  // Normal layer
}
```

## Wiring Diagram

### Matrix Wiring with Thumb Clusters
```
Main Matrix (4×6):
Row 1 ──┐
Row 2 ──┼──┐
Row 3 ──┼──┼──┐
Row 4 ──┘  │  │
            │  │
Col1 Col2 Col3 Col4 Col5 Col6

Thumb Cluster (1×5):
Thumb Row ──────────────┐
                        │
                    Col1 Col2 Col3 Col4 Col5
                    (shared with main matrix)
```

### I2C Connection
```
Left Side (Arduino Uno)    Right Side (ESP32)
SDA (A4) ──────────────── SDA (33)
SCL (A5) ──────────────── SCL (34)
GND ───────────────────── GND
```

### Switch Connection
- **Main Matrix:** Each switch connects one row pin to one column pin
- **Thumb Cluster:** Each switch connects the thumb row pin to one of the first 5 column pins
- **Shared Columns:** The thumb cluster shares columns 1-5 with the main matrix

## Software Setup

### Arduino IDE Setup

1. **Install Arduino IDE** from [arduino.cc](https://www.arduino.cc/en/software)
2. **Install ESP32 Board Support:**
   - Go to File → Preferences
   - Add to Additional Board Manager URLs: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
   - Go to Tools → Board → Boards Manager
   - Search for "ESP32" and install "ESP32 by Espressif Systems"

### Required Libraries

#### For Arduino Uno (left_arduino_uno.ino)
- **Wire.h** - Built-in library for I2C communication
- **Note:** This sketch acts as an I2C slave device

#### For ESP32 (right_esp32.ino)
- **BleKeyboard** - Install via Library Manager
  - Go to Tools → Manage Libraries
  - Search for "BleKeyboard" by T-vK
  - Install the library
- **Wire.h** - Built-in library for I2C communication

## Installation Steps

### Left Side (Arduino)
1. Connect your Arduino board via USB
2. Open `left_arduino_uno.ino` in Arduino IDE
3. Select the correct board (Arduino Uno)
4. Select the correct port
5. Click Upload

### Right Side (ESP32)
1. Connect your ESP32 via USB
2. Open `right_esp32.ino` in Arduino IDE
3. Select "ESP32 Dev Module" or your specific ESP32 board
4. Select the correct port
5. Click Upload

### I2C Connection
1. Connect SDA and SCL between the two boards
2. Ensure both boards share a common ground
3. Power both boards independently

## Usage

### Left Side
- Acts as an I2C slave device
- Scans main matrix (4×6) and thumb cluster (1×5)
- Tracks modifier key states separately
- Sends key data to ESP32 via I2C
- No direct USB output

### Right Side
- Acts as an I2C master device
- Receives key data from Arduino via I2C
- Scans its own main matrix (4×6) and thumb cluster (1×5)
- Combines all key data and sends via Bluetooth
- Appears as "Split Keyboard" in Bluetooth devices

### Communication Flow
```
Left Side Keys → I2C → Right Side → Bluetooth → Computer
Right Side Keys ──────┘
```

## Customization

### Key Mapping
Edit the `mainKeyMap` and `thumbKeyMap` arrays in both sketches:

```cpp
// Example: Change thumb cluster keys
const char thumbKeyMap[THUMB_COLS] = {'ctrl', 'alt', 'fn', ']', '['}; // Current mapping
// Could be: {'shift', 'ctrl', 'alt', 'fn', 'super'} for different layout
```

### Modifier Key Functions
The current implementation provides:
- **Left thumb cluster:** Ctrl, Alt, Fn, ], [
- **Left main matrix:** Esc, Tab, Super, Shift (as modifiers)
- **Right side:** Standard character keys

### Thumb Cluster Functions
The thumb clusters can be used for:
- Modifier keys (Shift, Ctrl, Alt, Super)
- Layer switching (Fn key)
- Special functions
- Gaming macros
- Custom key combinations

### Pin Assignment
You can change pin assignments by modifying the pin arrays:

```cpp
// Example: Use different pins for thumb cluster
const int thumbRowPin = 13;  // Changed from 12
```

## Troubleshooting

### Common Issues

#### I2C Communication Problems
- **Problem:** Keys not registering from left side
- **Solution:** 
  - Check I2C connections (SDA, SCL, GND)
  - Verify both boards are powered
  - Check Serial Monitor for I2C errors

#### Modifier Keys Not Working
- **Problem:** Shift, Ctrl, Alt not functioning properly
- **Solution:**
  - Check modifier key wiring
  - Verify modifier state tracking in code
  - Check I2C data transmission

#### Thumb Cluster Not Working
- **Problem:** Thumb keys not responding
- **Solution:**
  - Check thumb cluster row pin connection
  - Verify column pin sharing with main matrix
  - Test continuity between switches

#### Left Side Not Detected
- **Problem:** ESP32 can't communicate with Arduino
- **Solution:**
  - Check I2C address (0x08)
  - Verify Wire.begin() calls
  - Check for pin conflicts

### Debug Mode
Both sketches include Serial output for debugging:
- **Arduino:** Open Serial Monitor at 9600 baud
- **ESP32:** Open Serial Monitor at 115200 baud

## Advanced Features

### Thumb Cluster Modifiers
The current implementation maps left thumb cluster to modifiers:

```cpp
// Left thumb cluster as modifiers
if (leftModifiers[0]) bleKeyboard.press(KEY_LEFT_CTRL);
if (leftModifiers[1]) bleKeyboard.press(KEY_LEFT_SHIFT);
if (leftModifiers[2]) bleKeyboard.press(KEY_LEFT_ALT);
```

### Layer Support
You can implement multiple layers using the Fn key:

```cpp
// Example: Layer switching with Fn key
bool layer1 = false;
if (leftModifiers[4]) {  // Fn key is pressed
  layer1 = true;
} else {
  layer1 = false;
}
```

### Custom Key Combinations
Map specific key combinations to thumb cluster keys:

```cpp
// Example: Thumb cluster for common shortcuts
if (leftThumbKeyStates[3]) {  // Right bracket key
  bleKeyboard.press(KEY_LEFT_CTRL);
  bleKeyboard.press('c');
  bleKeyboard.releaseAll();
}
```

### Modifier Key Combinations
Handle complex modifier combinations:

```cpp
// Example: Ctrl+Alt+Del equivalent
if (leftModifiers[0] && leftModifiers[2]) {  // Ctrl + Alt
  if (someKeyPressed) {
    bleKeyboard.press(KEY_DELETE);
    bleKeyboard.release(KEY_DELETE);
  }
}
```

## Contributing

Feel free to modify and improve these sketches for your specific needs!

## License

This project is open source. Use it for any purpose you like.
