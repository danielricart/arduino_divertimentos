# midi-experiments

## ble_midi_knob

ESP32-C3 acting as a standard BLE MIDI device (no drivers needed on Windows 10/11 or macOS).

Each board advertises as `ESP32-C3 MIDI Knob XXXX`, where `XXXX` are the last 4 hex digits of its
factory Bluetooth MAC, so several boards can be told apart. Serial prints the full name and MAC at start-up.

- Modulino Knob -> CC 1, channel 1 (value 0-127, clamped)
- Modulino Buttons (3 buttons A, B, C) -> notes 60, 62, 64, on while held and off on release
- Modulino Pixels (8 RGB LEDs) controlled from the computer on channel 1: CC 20-27 = brightness of LED 1-8, CC 28 = master brightness, CC 29/30/31 = red/green/blue. LEDs start off until a brightness is sent. The Knob stays on CC 1.
- Modulino Movement: gyroscope rotation speed -> CC 102/103/104 (roll/pitch/yaw), 64 when still. Tune range, dead zone and smoothing with the `GYRO_*` constants in the sketch.
- Hold button C for 5 s to disconnect from the computer. Pairing is kept, so the same computer can reconnect and another computer can connect while the device advertises

### MIDI map (channel 1)

"Out" = sent by the device to the computer. "In" = sent by the computer to the device.

| Function | Direction | MIDI message | Range |
|----------|-----------|--------------|-------|
| Knob rotation | Out | CC 1 | 0-127 |
| Knob press | - | not mapped | - |
| Button A | Out | Note 60 (C4), velocity 100 | on while held / off on release |
| Button B | Out | Note 62 (D4), velocity 100 | on while held / off on release |
| Button C | Out | Note 64 (E4), velocity 100 | on while held / off on release |
| Button C held 5 s | - | none (disconnects from the computer, keeps pairing) | - |
| Gyro roll speed | Out | CC 102 | 0-127, 64 when still |
| Gyro pitch speed | Out | CC 103 | 0-127, 64 when still |
| Gyro yaw speed | Out | CC 104 | 0-127, 64 when still |
| Brightness - LED0 | In | CC 20 | 0-127 |
| Brightness - LED1 | In | CC 21 | 0-127 |
| Brightness - LED2 | In | CC 22 | 0-127 |
| Brightness - LED3 | In | CC 23 | 0-127 |
| Brightness - LED4 | In | CC 24 | 0-127 |
| Brightness - LED5 | In | CC 25 | 0-127 |
| Brightness - LED6 | In | CC 26 | 0-127 |
| Brightness - LED7 | In | CC 27 | 0-127 |
| Master brightness (all LEDs) | In | CC 28 | 0-127 (multiplies each LED's value) |
| Colour red (all LEDs) | In | CC 29 | 0-127 |
| Colour green (all LEDs) | In | CC 30 | 0-127 |
| Colour blue (all LEDs) | In | CC 31 | 0-127 |

At start-up all LEDs are off, master brightness is 127 and the colour is white (127/127/127),
so sending CC 20 = 127 alone lights LED0 white.

### Wiring (ESP32-C3 Super Mini, 3.3 V only)

The Modulinos run at **3.3 V** (supply range 2.0-3.6 V) with 3.3 V I2C logic. Never power them from 5 V.
The Super Mini's default I2C pins are GPIO8 (SDA) and GPIO9 (SCL), which is what the sketch uses.

Qwiic cable colours are black, red, blue, yellow. Each module has two Qwiic connectors, so chain the Buttons, Pixels and Movement from the Knob (or join all of them to the same four wires):

| Qwiic wire | Signal | Super Mini pin |
|------------|--------|----------------|
| Black      | GND    | `G` (GND)      |
| Red        | 3.3 V  | `3V3`          |
| Blue       | SDA    | `8` (GPIO8)    |
| Yellow     | SCL    | `9` (GPIO9)    |

Notes:
- GPIO8 also drives the on-board LED, which will flicker with I2C traffic. That is harmless.
- GPIO8 and GPIO9 are boot-strapping pins (GPIO9 is the BOOT button). If the board will not boot or enter
  download mode with the modules attached, disconnect them and retry.

### Libraries

| Library | Source | Install |
|---------|--------|---------|
| esp32 board core (Espressif) | Boards Manager | Install `esp32` by Espressif Systems, latest 3.x |
| BLE-MIDI (lathoub) | **GitHub master** | `git clone https://github.com/lathoub/Arduino-BLE-MIDI.git ~/Documents/Arduino/libraries/BLE-MIDI` |
| MIDI Library (FortySevenEffects) | Library Manager | Search `MIDI Library` |
| Arduino_Modulino (Arduino) | Library Manager | Search `Arduino_Modulino` (accept its dependencies) |

Not needed: **NimBLE-Arduino**. BLE uses the core's built-in BLE library.

Why GitHub for BLE-MIDI: the Library Manager release (2.2, 2022) does not compile on esp32 core 3.x
(`conversion from 'String' to non-scalar type 'std::string'`). Master has the fix.
Without git: download https://github.com/lathoub/Arduino-BLE-MIDI/archive/refs/heads/master.zip,
unzip, and rename the folder to `~/Documents/Arduino/libraries/BLE-MIDI`.
To update later: `git -C ~/Documents/Arduino/libraries/BLE-MIDI pull`.

### Clean up an older setup
1. Close the Arduino IDE.
2. Delete `~/Documents/Arduino/libraries/BLE-MIDI` (the Library Manager 2.2 copy).
3. Delete `~/Documents/Arduino/libraries/NimBLE-Arduino`.
4. Install BLE-MIDI from GitHub as above, then reopen the IDE.
5. If the IDE offers to install NimBLE-Arduino or "update" BLE-MIDI, decline.

### Setup
1. Board: **ESP32C3 SuperMini** (`nologo_esp32c3_super_mini`). Enable **USB CDC On Boot** to see Serial output.
2. I2C addresses in the sketch: Knob `0x3A` (`0x3B` with the alternate pinstrap), Buttons `0x3E`, Pixels `0x36`, Movement `0x6A` (`0x6B` on some modules).
   Use `0xFF` to let the library find a module, or run the Modulino `AddressChanger` example to list them.
3. Upload, then pair:
   - macOS: Audio MIDI Setup -> Window -> Show MIDI Studio -> Bluetooth icon -> Connect.
     The device does not appear in System Settings -> Bluetooth.
   - Windows: use a BLE MIDI-aware app; some need the device added under Bluetooth settings first.
4. If the Mac cannot reconnect after a board was reflashed or ran an older build that cleared its pairing,
   make the Mac forget it (System Settings -> Bluetooth -> (i) next to the device -> Forget This Device),
   then connect again from Audio MIDI Setup.

### Status
CI compiles the sketch on every push.
Verified on an ESP32-C3 Super Mini with macOS: BLE pairing via Audio MIDI Setup, knob CC 1, button notes,
and LED0 via CC 20. Not yet verified: the 5 s disconnect hold on C, the other LED CCs and the colour CCs.
