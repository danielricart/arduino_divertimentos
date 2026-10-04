/*
  ESP32-C3 BLE MIDI controller
  - Modulino Knob  -> MIDI Control Change (CC 1, channel 1 by default)
  - Modulino Buttons (3 buttons, labelled A, B, C):
      short press A/B/C    -> MIDI notes 60/62/64 on while held, off on release
      button C held 5 s    -> disconnect, clear stored bonds, restart advertising
                              (use this to pair with a different computer)

  Uses the standard BLE MIDI GATT service, so Windows 10/11 and macOS
  need no extra drivers.

  Libraries (Library Manager):
    - BLE-MIDI           (lathoub)   -> pulls in MIDI Library (FortySevenEffects)
    - NimBLE-Arduino     (h2zero) version 1.4.3 (BLE-MIDI is not compatible with 2.x)
    - Modulino           (Arduino)   -> Arduino_Modulino
*/

#include <Arduino_Modulino.h>
#include <BLEMIDI_Transport.h>
#include <hardware/BLEMIDI_ESP32_NimBLE.h>

// ---------- Configuration ----------
// Defaults from the Arduino_Modulino library. The Knob here uses 0x74 (0x76 is the alternate pinstrap).
// Pass 0xFF to let the library auto-discover the module instead.
const uint8_t KNOB_ADDRESS    = 0x74;
const uint8_t BUTTONS_ADDRESS = 0x7C;

const char*   DEVICE_NAME     = "ESP32-C3 MIDI Knob";
const uint8_t MIDI_CHANNEL    = 1;
const uint8_t KNOB_CC         = 1;     // CC number sent by the knob
const uint8_t BUTTON_NOTES[3] = {60, 62, 64};   // buttons A, B, C
const uint8_t BUTTON_VELOCITY = 100;

const int     KNOB_STEP       = 2;     // CC change per encoder detent
const uint8_t  REPAIR_BUTTON  = 2;     // 0 = A, 1 = B, 2 = C
const uint32_t LONG_PRESS_MS  = 5000;  // hold the re-pair button this long to re-pair
const uint32_t POLL_MS        = 10;
// -----------------------------------

BLEMIDI_CREATE_INSTANCE(DEVICE_NAME, MIDI)

ModulinoKnob    knob(KNOB_ADDRESS);
ModulinoButtons buttons(BUTTONS_ADDRESS);

bool connected = false;
int  lastCC    = -1;

const uint8_t NUM_BUTTONS = 3;
bool     buttonDown[NUM_BUTTONS]  = {false, false, false};
uint32_t buttonSince[NUM_BUTTONS] = {0, 0, 0};
bool     longHandled = false;

void sendKnobValue(int cc) {
  if (cc != lastCC && connected) {
    MIDI.sendControlChange(KNOB_CC, cc, MIDI_CHANNEL);
    lastCC = cc;
  }
}

void releaseAllNotes() {
  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    if (buttonDown[i] && connected) MIDI.sendNoteOff(BUTTON_NOTES[i], 0, MIDI_CHANNEL);
  }
}

void clearPairing() {
  releaseAllNotes();         // avoid a stuck note on the host when the link drops
  Serial.println("Clearing bonds and restarting advertising");
  NimBLEServer* server = NimBLEDevice::getServer();
  if (server) {
    for (uint16_t handle : server->getPeerDevices()) {
      server->disconnect(handle);
    }
  }
  NimBLEDevice::deleteAllBonds();
  NimBLEDevice::startAdvertising();
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

  // Buttons A/B/C: each sends its own note while held; holding the re-pair button re-pairs
  if (buttons.update()) {
    for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
      bool down = buttons.isPressed((int)i);
      if (down && !buttonDown[i]) {
        buttonDown[i] = true;
        if (i == REPAIR_BUTTON) {
          buttonSince[i] = millis();
          longHandled = false;
        }
        if (connected) MIDI.sendNoteOn(BUTTON_NOTES[i], BUTTON_VELOCITY, MIDI_CHANNEL);
      } else if (!down && buttonDown[i]) {
        buttonDown[i] = false;
        if (connected) MIDI.sendNoteOff(BUTTON_NOTES[i], 0, MIDI_CHANNEL);
      }
    }
  }
  if (buttonDown[REPAIR_BUTTON] && !longHandled &&
      millis() - buttonSince[REPAIR_BUTTON] >= LONG_PRESS_MS) {
    longHandled = true;
    clearPairing();
  }
}
