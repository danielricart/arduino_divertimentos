# midi-experiments

## ble_midi_knob

ESP32-C3 acting as a standard BLE MIDI device (no drivers needed on Windows 10/11 or macOS).

- Modulino Knob -> CC 1, channel 1 (value 0-127, clamped)
- Modulino Buttons, button A -> note 60 on/off; hold 3 s to disconnect, clear bonds and re-advertise

### Wiring (ESP32-C3 Super Mini, 3.3 V only)

The Modulinos run at **3.3 V** (supply range 2.0-3.6 V) with 3.3 V I2C logic. Never power them from 5 V.
The Super Mini's default I2C pins are GPIO8 (SDA) and GPIO9 (SCL), which is what the sketch uses.

Qwiic cable colours are black, red, blue, yellow. Each module has two Qwiic connectors, so chain the second
module from the first, or join both to the same four wires:

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
2. Set `KNOB_ADDRESS` / `BUTTONS_ADDRESS` in the sketch.
3. Upload, then pair:
   - macOS: Audio MIDI Setup -> Window -> Show MIDI Studio -> Bluetooth icon -> Connect.
     The device does not appear in System Settings -> Bluetooth.
   - Windows: use a BLE MIDI-aware app; some need the device added under Bluetooth settings first.

### Status
CI compiles the sketch on every push. Not yet verified on hardware with the core BLE transport.
