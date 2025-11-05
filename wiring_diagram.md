# Split Keyboard Wiring Diagram

## Overview
This document shows the detailed wiring connections for both sides of the split keyboard with thumb clusters and I2C communication.

## Left Side (Arduino Uno) - Main: 4×6 + Thumb: 1×5

### Pin Connections
```
Main Matrix:
- Row Pins (Outputs): 2, 3, 4, 5
- Column Pins (Inputs): 6, 7, 8, 9, 10, 11

Thumb Cluster:
- Row Pin (Output): 12
- Column Pins (Inputs): 6, 7, 8, 9, 10 (shared with main matrix)

I2C Communication:
- SDA: A4
- SCL: A5
```

### Matrix Layout
```
        Col1  Col2  Col3  Col4  Col5  Col6
        Pin6  Pin7  Pin8  Pin9  Pin10 Pin11
        ┌─────┬─────┬─────┬─────┬─────┬─────┐
Row1    │  1  │  2  │  3  │  4  │  5  │  6  │
Pin2    │     │     │     │     │     │     │
        ├─────┼─────┼─────┼─────┼─────┼─────┤
Row2    │  q  │  w  │  e  │  r  │  t  │  y  │
Pin3    │     │     │     │     │     │     │
        ├─────┼─────┼─────┼─────┼─────┼─────┤
Row3    │  a  │  s  │  d  │  f  │  g  │  h  │
Pin4    │     │     │     │     │     │     │
        ├─────┼─────┼─────┼─────┼─────┼─────┤
Row4    │  z  │  x  │  c  │  v  │  b  │  n  │
Pin5    │     │     │     │     │     │     │
        └─────┴─────┴─────┴─────┴─────┴─────┘
        │     │     │     │     │     │     │
        │  T  │  T  │  T  │  T  │  T  │     │ ← Thumb cluster (Pin12)
        │  H  │  H  │  H  │  H  │  H  │     │
        │  U  │  U  │  U  │  U  │  U  │     │
        │  M  │  M  │  M  │  M  │  M  │     │
        │  B  │  B  │  B  │  B  │  B  │     │
```

### Individual Switch Connections
```
Main Matrix:
Switch 1 (Key '1'):  Row1 (Pin2) ←→ Col1 (Pin6)
Switch 2 (Key '2'):  Row1 (Pin2) ←→ Col2 (Pin7)
Switch 3 (Key '3'):  Row1 (Pin2) ←→ Col3 (Pin8)
Switch 4 (Key '4'):  Row1 (Pin2) ←→ Col4 (Pin9)
Switch 5 (Key '5'):  Row1 (Pin2) ←→ Col5 (Pin10)
Switch 6 (Key '6'):  Row1 (Pin2) ←→ Col6 (Pin11)

Switch 7 (Key 'q'):  Row2 (Pin3) ←→ Col1 (Pin6)
Switch 8 (Key 'w'):  Row2 (Pin3) ←→ Col2 (Pin7)
Switch 9 (Key 'e'):  Row2 (Pin3) ←→ Col3 (Pin8)
Switch 10 (Key 'r'): Row2 (Pin3) ←→ Col4 (Pin9)
Switch 11 (Key 't'): Row2 (Pin3) ←→ Col5 (Pin10)
Switch 12 (Key 'y'): Row2 (Pin3) ←→ Col6 (Pin11)

Switch 13 (Key 'a'): Row3 (Pin4) ←→ Col1 (Pin6)
Switch 14 (Key 's'): Row3 (Pin4) ←→ Col2 (Pin7)
Switch 15 (Key 'd'): Row3 (Pin4) ←→ Col3 (Pin8)
Switch 16 (Key 'f'): Row3 (Pin4) ←→ Col4 (Pin9)
Switch 17 (Key 'g'): Row3 (Pin4) ←→ Col5 (Pin10)
Switch 18 (Key 'h'): Row3 (Pin4) ←→ Col6 (Pin11)

Switch 19 (Key 'z'): Row4 (Pin5) ←→ Col1 (Pin6)
Switch 20 (Key 'x'): Row4 (Pin5) ←→ Col2 (Pin7)
Switch 21 (Key 'c'): Row4 (Pin5) ←→ Col3 (Pin8)
Switch 22 (Key 'v'): Row4 (Pin5) ←→ Col4 (Pin9)
Switch 23 (Key 'b'): Row4 (Pin5) ←→ Col5 (Pin10)
Switch 24 (Key 'n'): Row4 (Pin5) ←→ Col6 (Pin11)

Thumb Cluster:
Switch 25 (Thumb1): ThumbRow (Pin12) ←→ Col1 (Pin6)
Switch 26 (Thumb2): ThumbRow (Pin12) ←→ Col2 (Pin7)
Switch 27 (Thumb3): ThumbRow (Pin12) ←→ Col3 (Pin8)
Switch 28 (Thumb4): ThumbRow (Pin12) ←→ Col4 (Pin9)
Switch 29 (Thumb5): ThumbRow (Pin12) ←→ Col5 (Pin10)
```

## Right Side (ESP32) - Main: 4×6 + Thumb: 1×5

### Pin Connections
```
Main Matrix:
- Row Pins (Outputs): 2, 4, 5, 18
- Column Pins (Inputs): 21, 22, 23, 25, 26, 27

Thumb Cluster:
- Row Pin (Output): 32
- Column Pins (Inputs): 21, 22, 23, 25, 26 (shared with main matrix)

I2C Communication:
- SDA: 33 (separate pin to avoid conflicts)
- SCL: 34 (separate pin to avoid conflicts)
```

### Matrix Layout
```
        Col1  Col2  Col3  Col4  Col5  Col6
        Pin21 Pin22 Pin23 Pin25 Pin26 Pin27
        ┌─────┬─────┬─────┬─────┬─────┬─────┐
Row1    │  7  │  8  │  9  │  0  │  -  │  =  │
Pin2    │     │     │     │     │     │     │
        ├─────┼─────┼─────┼─────┼─────┼─────┤
Row2    │  u  │  i  │  o  │  p  │  [  │  ]  │
Pin4    │     │     │     │     │     │     │
        ├─────┼─────┼─────┼─────┼─────┼─────┤
Row3    │  j  │  k  │  l  │  ;  │  '  │  \  │
Pin5    │     │     │     │     │     │     │
        ├─────┼─────┼─────┼─────┼─────┼─────┤
Row4    │  m  │  ,  │  .  │  /  │ SP  │ SP  │
Pin18   │     │     │     │     │     │     │
        └─────┴─────┴─────┴─────┴─────┴─────┘
        │     │     │     │     │     │     │
        │  T  │  T  │  T  │  T  │  T  │     │ ← Thumb cluster (Pin32)
        │  H  │  H  │  H  │  H  │  H  │     │
        │  U  │  U  │  U  │  U  │  U  │     │
        │  M  │  M  │  M  │  M  │  M  │     │
        │  B  │  B  │  B  │  B  │  B  │     │
```

### Individual Switch Connections
```
Main Matrix:
Switch 1 (Key '7'):   Row1 (Pin2)  ←→ Col1 (Pin21)
Switch 2 (Key '8'):   Row1 (Pin2)  ←→ Col2 (Pin22)
Switch 3 (Key '9'):   Row1 (Pin2)  ←→ Col3 (Pin23)
Switch 4 (Key '0'):   Row1 (Pin2)  ←→ Col4 (Pin25)
Switch 5 (Key '-'):   Row1 (Pin2)  ←→ Col5 (Pin26)
Switch 6 (Key '='):   Row1 (Pin2)  ←→ Col6 (Pin27)

Switch 7 (Key 'u'):   Row2 (Pin4)  ←→ Col1 (Pin21)
Switch 8 (Key 'i'):   Row2 (Pin4)  ←→ Col2 (Pin22)
Switch 9 (Key 'o'):   Row2 (Pin4)  ←→ Col3 (Pin23)
Switch 10 (Key 'p'):  Row2 (Pin4)  ←→ Col4 (Pin25)
Switch 11 (Key '['):  Row2 (Pin4)  ←→ Col5 (Pin26)
Switch 12 (Key ']'):  Row2 (Pin4)  ←→ Col6 (Pin27)

Switch 13 (Key 'j'):  Row3 (Pin5)  ←→ Col1 (Pin21)
Switch 14 (Key 'k'):  Row3 (Pin5)  ←→ Col2 (Pin22)
Switch 15 (Key 'l'):  Row3 (Pin5)  ←→ Col3 (Pin23)
Switch 16 (Key ';'):  Row3 (Pin5)  ←→ Col4 (Pin25)
Switch 17 (Key "'"):  Row3 (Pin5)  ←→ Col5 (Pin26)
Switch 18 (Key '\'):  Row3 (Pin5)  ←→ Col6 (Pin27)

Switch 19 (Key 'm'):  Row4 (Pin18) ←→ Col1 (Pin21)
Switch 20 (Key ','):  Row4 (Pin18) ←→ Col2 (Pin22)
Switch 21 (Key '.'):  Row4 (Pin18) ←→ Col3 (Pin23)
Switch 22 (Key '/'):  Row4 (Pin18) ←→ Col4 (Pin25)
Switch 23 (Space):   Row4 (Pin18) ←→ Col5 (Pin26)
Switch 24 (Space):   Row4 (Pin18) ←→ Col6 (Pin27)

Thumb Cluster:
Switch 25 (Thumb1): ThumbRow (Pin32) ←→ Col1 (Pin21)
Switch 26 (Thumb2): ThumbRow (Pin32) ←→ Col2 (Pin22)
Switch 27 (Thumb3): ThumbRow (Pin32) ←→ Col3 (Pin23)
Switch 28 (Thumb4): ThumbRow (Pin32) ←→ Col4 (Pin25)
Switch 29 (Thumb5): ThumbRow (Pin32) ←→ Col5 (Pin26)
```

## I2C Connection Between Sides

### Physical Connection
```
Left Side (Arduino Uno)    Right Side (ESP32)
SDA (A4) ──────────────── SDA (33)
SCL (A5) ──────────────── SCL (34)
GND ───────────────────── GND
```

### Communication Details
- **I2C Address:** Left side (Arduino) = 0x08
- **Master:** Right side (ESP32) requests data
- **Slave:** Left side (Arduino) responds with key data
- **Data Structure:** 29 boolean values + timestamp
- **Update Rate:** Continuous scanning and transmission

## Wiring Tips

### 1. Matrix Scanning with Thumb Clusters
- **Main Matrix:** Rows are set HIGH by default, scanned one at a time
- **Thumb Cluster:** Scanned separately after main matrix
- **Shared Columns:** Thumb cluster uses first 5 columns of main matrix
- **Timing:** Small delays between row changes to prevent interference

### 2. Preventing Ghosting
- Add diodes between each switch and column pin
- Diode cathode (banded end) connects to column pin
- Diode anode connects to switch
- **Important:** Diodes are especially critical with shared columns

### 3. I2C Wiring
- Keep I2C wires as short as possible
- Use twisted pair if available
- Ensure both boards share common ground
- Avoid running I2C wires near high-frequency signals

### 4. Thumb Cluster Considerations
- **Shared Columns:** Thumb cluster shares columns 1-5 with main matrix
- **Scanning Order:** Scan main matrix first, then thumb cluster
- **Pin Conflicts:** Ensure thumb cluster row pin doesn't conflict with other pins
- **Wiring:** Thumb cluster can be wired separately and connected to shared columns

### 5. Power Distribution
- **Independent Power:** Each side can be powered independently
- **Common Ground:** Essential for I2C communication
- **Voltage Levels:** Arduino Uno (5V), ESP32 (3.3V) - I2C is 3.3V compatible
- **Current:** Ensure sufficient current for all switches and controllers

### 6. Testing and Debugging
- **Individual Testing:** Test each matrix section separately
- **I2C Testing:** Use I2C scanner to verify communication
- **Serial Output:** Monitor both sides for debugging
- **Continuity:** Check all connections with multimeter

### 7. Switch Types and Compatibility
- Compatible with Cherry MX, Gateron, Kailh, and other mechanical switches
- Each switch has two terminals that connect row to column
- No additional resistors needed (built into the code)
- **Hot-swappable:** Can use hot-swappable sockets for easy switch replacement

### 8. PCB vs Hand-wiring
- **PCB Recommended:** Better for complex layouts with thumb clusters
- **Hand-wiring:** Possible but requires careful planning
- **Routing:** Consider thumb cluster placement and wire management
- **Layers:** Multiple PCB layers may be needed for clean routing
