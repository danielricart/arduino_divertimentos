# midi-experiments

## ble_midi_knob

ESP32-C3 acting as a standard BLE MIDI device (no drivers needed on Windows 10/11 or macOS).

- Modulino Knob -> CC 1, channel 1 (value 0-127, clamped)
- Modulino Buttons, button A -> note 60 on/off; hold 3 s to disconnect, clear bonds and re-advertise

### Setup
1. Install the esp32 board core (Espressif) and select your ESP32-C3 board.
2. Library Manager: `BLE-MIDI` (lathoub), `NimBLE-Arduino` (h2zero), `Modulino` (Arduino).
3. Set `KNOB_ADDRESS` / `BUTTONS_ADDRESS` in the sketch. The modules must already be configured to
   those I2C addresses (use the address-change example shipped with the Modulino library).
4. Upload, then pair:
   - macOS: Audio MIDI Setup -> Window -> Show MIDI Studio -> Bluetooth icon -> Connect.
   - Windows: use a BLE MIDI-aware app (e.g. a DAW or MIDI-OX alternative such as Bluetooth LE MIDI
     support in recent Windows MIDI Services); some apps need the device added under Bluetooth settings first.

### Status
Not compiled or tested on hardware (no toolchain in the authoring environment). Library API names
(`setHandleConnected`, `ModulinoKnob(address)`, `getPeerDevices`) should be verified against the versions you install.
