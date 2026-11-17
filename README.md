# Split Keyboard Project

A wireless split keyboard. Each half is an ESP32, and everything is Bluetooth
(BLE) — there are no wires between the halves or to the computer.

```
 Right ESP32 ──BLE──▶ Left ESP32 ──BLE keyboard──▶ Computer
 (sends which keys     (owns the keymap for both
  are pressed)          halves, sends the keys)
```

- **Left half** (`left_esp32/`) is the keyboard the computer pairs with. It
  scans its own keys, receives the right half's key positions, applies the
  keymap and sends one combined HID report.
- **Right half** (`right_esp32/`) just scans its keys and sends their
  positions to the left half (5 bytes per update).

The keymap for **both** halves lives in `left_esp32/left_esp32.ino`, so
layout changes only need the left half re-uploaded.

## Layout

Each half is a 5 × 6 matrix: four main rows plus a thumb row. Each thumb row
has five physical keys.

```
Left                                     Right
┌──────┬────┬────┬────┬────┬────┐        ┌────┬────┬────┬─────┬───────┬──────┐
│ Esc  │ 1  │ 2  │ 3  │ 4  │ 5  │        │ 6  │ 7  │ 8  │ 9   │ 0     │ -    │
├──────┼────┼────┼────┼────┼────┤        ├────┼────┼────┼─────┼───────┼──────┤
│ Tab  │ q  │ w  │ e  │ r  │ t  │        │ y  │ u  │ i  │ o   │ p     │ =    │
├──────┼────┼────┼────┼────┼────┤        ├────┼────┼────┼─────┼───────┼──────┤
│ Ctrl │ a  │ s  │ d  │ f  │ g  │        │ h  │ j  │ k  │ l   │ ;     │ '    │
├──────┼────┼────┼────┼────┼────┤        ├────┼────┼────┼─────┼───────┼──────┤
│Shift │ z  │ x  │ c  │ v  │ b  │        │ n  │ m  │ ,  │ .   │ /     │ \    │
└──────┴────┴────┴────┴────┴────┘        └────┴────┴────┴─────┴───────┴──────┘
Thumb: Enter Super  Alt  ]    [          Thumb:   #    `   Bksp  Fn     Space
       (cols 0-4; col 5 has no key)              (cols 1-5; col 0 has no key)
```

### Shift

Shift works like a normal US keyboard (`1` → `!`, `]` → `}`, `'` → `"`, …),
with two custom thumb keys:

| Key | Normal | With Shift |
|-----|--------|------------|
| Right thumb col 1 | `#` | `~` |
| Right thumb col 2 | `` ` `` | `@` |

### Fn layer

Hold **Fn** (right thumb col 4) for the function keys. Keys without an Fn
mapping keep doing what they normally do.

| Key | With Fn |
|-----|---------|
| `1` – `5` (left) | F1 – F5 |
| `6` – `0` (right) | F6 – F10 |
| `-` | F11 |
| `=` | F12 |
| `w` / `a` / `s` / `d` | Up / Left / Down / Right |
| `q` / `e` | Page Up / Page Down |

Fn + Shift + arrows selects text like Shift + arrows on a normal keyboard.

### Changing the layout

Edit the `keymap` table in `left_esp32/left_esp32.ino`. It has three
layers per half: `BASE`, `SHIFTED` (only for keys whose shifted character
isn't the usual one) and `FN`. Entries are either a character (`'a'`, `'#'`,
`' '`), a named key (`K_ENTER`, `K_BSPC`, `K_LCTRL`, `K_FN`, `K_F(1)`, …), or
`NONE` / `SAME`.

## Special combos

| Combo | What it does |
|-------|--------------|
| Hold **Esc + Shift** (left) for 2 s | Restart the left ESP32 (the right half reconnects by itself) |
| Hold **`-` + `\`** (right) for 2 s | Restart the right ESP32 |
| **Fn + Shift + 1 / 2 / 3** | Switch to host device 1 / 2 / 3. Each device is a separate Bluetooth keyboard, so pair it once from each host. |
| Hold **Fn + Shift + 1 / 2 / 3** for 3 s | Pair a new host on that device: forgets its old host, gives it a fresh Bluetooth address and restarts. Pair "Split Keyboard" from the new host, and remove the old entry from the old host. |
| **Fn + Space** | Turn auto period / auto "I" on or off (remembered across restarts) |

## Sleep

Each half goes to sleep after 30 minutes with no key presses. Press any key
on that half to wake it; it restarts and reconnects in a second or two.

## Pins

| | Rows (outputs, top → bottom, thumb last) | Columns (inputs, left → right) |
|---|---|---|
| Left ESP32  | 14, 27, 26, 23, 22 | 13, 32, 33, 25, 19, 21 |
| Right ESP32 | 19, 21, 22, 23, 32 | 33, 25, 26, 27, 18, 4 |

Rows are driven LOW one at a time; columns use internal pull-ups, so a
pressed key reads LOW. The thumb row shares the column pins with the main
matrix.

## Building and uploading

1. Install the Arduino IDE and the **esp32 by Espressif Systems** board
   package (Boards Manager).
2. Board: **ESP32 Dev Module**.
3. Open and upload `left_esp32/left_esp32.ino` to the left half and
   `right_esp32/right_esp32.ino` to the right half.

No libraries need to be installed: the left sketch bundles a patched copy of
T-vK's ESP32-BLE-Keyboard (`BleKeyboard.h/.cpp`) in its folder. Don't install
the stock library — the sketch relies on the patch (the `onStarted` hook and
connection-id callbacks).

## Pairing

1. Power both halves. The right half finds the left half on its own.
2. On the computer, pair with **Split Keyboard**.
3. To replace the host on a device slot, hold **Fn + Shift + 1/2/3** for 3 s
   (see Special combos).

## Auto text

Like a phone keyboard (Fn + Space turns all of it on/off):

- Two spaces after a word become `. `
- A lone `i` becomes `I` when you finish the word (space, `,` `.` `?` `!`
  `;` `:` or `'`, so `i'm` → `I'm`)

The keyboard can't see the screen, so it only acts on what it just typed;
after 10 s idle, Tab, Esc or a Ctrl/Alt/Super shortcut it does nothing until
you type more.

## Debugging

Both sketches log to the serial monitor at **115200 baud**: key presses and
releases, connections, and (on the left) every HID report sent to the
computer.

## Repository layout

| Path | What |
|------|------|
| `left_esp32/` | Left half firmware + bundled BLE keyboard library |
| `right_esp32/` | Right half firmware |
| `wiring_diagram.md` | Old wiring notes (still describes the Arduino/I2C version) |
| `old/` | Earlier attempts (Raspberry Pi left half, original BLE library) and case models |
