/*
  ESP32-C3 BLE MIDI controller
  - Modulino Knob  -> MIDI Control Change (CC 1, channel 1 by default)
  - Modulino Buttons:
      button A short press -> MIDI note 60 on/off
      button A long press  -> disconnect, clear stored bonds, restart advertising
                              (use this to pair with a different computer)

  Uses the standard BLE MIDI GATT service, so Windows 10/11 and macOS
  need no extra drivers.

  Libraries: see midi-experiments/README.md. BLE-MIDI must come from its GitHub
  master; the 2.2 release in Library Manager does not compile on esp32 core 3.x.
  BLE uses the esp32 core's built-in BLE library (no NimBLE-Arduino).
*/

#include <Arduino_Modulino.h>
#include <BLEMIDI_Transport.h>
#include <hardware/BLEMIDI_ESP32.h>

// Stack-specific headers, only needed to clear stored bonds
#if defined(CONFIG_NIMBLE_ENABLED)
#include <host/ble_hs.h>
#elif defined(CONFIG_BLUEDROID_ENABLED)
#include <esp_gap_ble_api.h>
#endif

// ---------- Configuration ----------
// Defaults from the Arduino_Modulino library. The Knob here uses 0x74 (0x76 is the alternate pinstrap).
// Pass 0xFF to let the library auto-discover the module instead.
const uint8_t KNOB_ADDRESS    = 0x74;
const uint8_t BUTTONS_ADDRESS = 0x7C;

const char*   DEVICE_NAME     = "ESP32-C3 MIDI Knob";
const uint8_t MIDI_CHANNEL    = 1;
const uint8_t KNOB_CC         = 1;     // CC number sent by the knob
const uint8_t BUTTON_NOTE     = 60;
const uint8_t BUTTON_VELOCITY = 100;

const int     KNOB_STEP       = 2;     // CC change per encoder detent
const uint32_t LONG_PRESS_MS  = 3000;  // hold button A this long to re-pair
const uint32_t POLL_MS        = 10;
// -----------------------------------

BLEMIDI_CREATE_INSTANCE(DEVICE_NAME, MIDI)

ModulinoKnob    knob(KNOB_ADDRESS);
ModulinoButtons buttons(BUTTONS_ADDRESS);

bool connected = false;
int  lastCC    = -1;

bool     buttonDown  = false;
bool     longHandled = false;
uint32_t buttonSince = 0;

void sendKnobValue(int cc) {
  if (cc != lastCC && connected) {
    MIDI.sendControlChange(KNOB_CC, cc, MIDI_CHANNEL);
    lastCC = cc;
  }
}

void deleteAllBonds() {
#if defined(CONFIG_NIMBLE_ENABLED)
  ble_store_clear();
#elif defined(CONFIG_BLUEDROID_ENABLED)
  int count = esp_ble_get_bond_device_num();
  if (count > 0) {
    esp_ble_bond_dev_t* list = new esp_ble_bond_dev_t[count];
    esp_ble_get_bond_device_list(&count, list);
    for (int i = 0; i < count; i++) esp_ble_remove_bond_device(list[i].bd_addr);
    delete[] list;
  }
#endif
}

void clearPairing() {
  Serial.println("Clearing bonds and restarting advertising");
  BLEServer* server = BLEDevice::getServer();
  if (server) {
    for (auto& peer : server->getPeerDevices(false)) {
      server->disconnect(peer.first);
    }
  }
  deleteAllBonds();
  // Advertising restarts from the disconnected handler once the link is down
}

void setup() {
  Serial.begin(115200);

  Modulino.begin();          // starts Wire on the board's default I2C pins
  knob.begin();
  buttons.begin();
  knob.set(0);
  Serial.printf("Knob at 0x%02X, buttons at 0x%02X\n", knob.getAddress(), buttons.getAddress());

  BLEMIDI.setHandleConnected([]() {
    connected = true;
    lastCC = -1;             // force a resend of the current value
    Serial.println("BLE MIDI connected");
  });
  BLEMIDI.setHandleDisconnected([]() {
    connected = false;
    Serial.println("BLE MIDI disconnected");
    // The core BLE transport does not re-advertise by itself after a disconnect
    BLEDevice::startAdvertising();
  });

  MIDI.begin();
  Serial.println("Advertising as BLE MIDI device");
}

void loop() {
  MIDI.read();               // keeps the BLE MIDI transport serviced

  static uint32_t lastPoll = 0;
  if (millis() - lastPoll < POLL_MS) return;
  lastPoll = millis();

  // Knob: clamp the encoder count so it maps directly onto 0..127
  {
    int count = knob.get();
    int maxCount = 127 / KNOB_STEP;
    if (count < 0) { count = 0; knob.set(0); }
    if (count > maxCount) { count = maxCount; knob.set(maxCount); }
    sendKnobValue(min(count * KNOB_STEP, 127));
  }

  // Button A: short press = note, long press = re-pair
  if (buttons.update()) {
    bool down = buttons.isPressed('A');
    if (down && !buttonDown) {
      buttonDown = true;
      longHandled = false;
      buttonSince = millis();
      if (connected) MIDI.sendNoteOn(BUTTON_NOTE, BUTTON_VELOCITY, MIDI_CHANNEL);
    } else if (!down && buttonDown) {
      buttonDown = false;
      if (connected) MIDI.sendNoteOff(BUTTON_NOTE, 0, MIDI_CHANNEL);
    }
  }
  if (buttonDown && !longHandled && millis() - buttonSince >= LONG_PRESS_MS) {
    longHandled = true;
    clearPairing();
  }
}
