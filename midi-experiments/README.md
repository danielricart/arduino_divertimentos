# midi-experiments

## ble_midi_knob

ESP32-C3 acting as a standard BLE MIDI device (no drivers needed on Windows 10/11 or macOS).

- Modulino Knob -> CC 1, channel 1 (value 0-127, clamped)
- Modulino Buttons (3 buttons A, B, C) -> notes 60, 62, 64, on while held and off on release
- Modulino Pixels (8 RGB LEDs) controlled from the computer on channel 1: CC 20-27 = brightness of LED 1-8, CC 28 = master brightness, CC 29/30/31 = red/green/blue. LEDs start off until a brightness is sent. The Knob stays on CC 1.
- Hold button C for 5 s to disconnect, clear bonds and re-advertise (pair with a different computer)

### Wiring (ESP32-C3 Super Mini, 3.3 V only)

The Modulinos run at **3.3 V** (supply range 2.0-3.6 V) with 3.3 V I2C logic. Never power them from 5 V.
The Super Mini's default I2C pins are GPIO8 (SDA) and GPIO9 (SCL), which is what the sketch uses.

Qwiic cable colours are black, red, blue, yellow. Each module has two Qwiic connectors, so chain the Buttons and Pixels from the Knob (or join all of them to the same four wires):

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

### Setup
1. Install the esp32 board core (Espressif) and select **ESP32C3 SuperMini** (`nologo_esp32c3_super_mini`).
2. Library Manager: `BLE-MIDI` (lathoub), `NimBLE-Arduino` (h2zero) **version 1.4.3** (latest 1.x; do not use 2.x, see below), `Arduino_Modulino` (Arduino).
3. Set `KNOB_ADDRESS` / `BUTTONS_ADDRESS` in the sketch. The modules must already be configured to
   those I2C addresses. Library defaults are Knob 0x74 (0x76 alternate) and Buttons 0x7C, Pixels 0x6C; use 0xFF to auto-discover.
4. Upload, then pair:
   - macOS: Audio MIDI Setup -> Window -> Show MIDI Studio -> Bluetooth icon -> Connect.
   - Windows: use a BLE MIDI-aware app (e.g. a DAW or MIDI-OX alternative such as Bluetooth LE MIDI
     support in recent Windows MIDI Services); some apps need the device added under Bluetooth settings first.

### Compatibility warning
BLE-MIDI 2.2 (lathoub) implements NimBLE server callbacks with the 1.x signatures (`onConnect(BLEServer*)`). NimBLE-Arduino 2.x changed them, so this project pins NimBLE-Arduino 1.4.3 (the latest 1.4.x) until BLE-MIDI is updated.

### Status
Not compiled or tested on hardware (no toolchain in the authoring environment). Library API names
(`setHandleConnected`, `ModulinoKnob(address)`, `getPeerDevices`) should be verified against the versions you install.
