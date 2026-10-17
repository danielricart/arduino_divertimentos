/*
  ESP32-C3 BLE MIDI controller
  - Modulino Knob  -> MIDI Control Change (CC 1, channel 1 by default)
  - Modulino Buttons (3 buttons, labelled A, B, C):
      short press A/B/C    -> MIDI notes 60/62/64 on while held, off on release
      button C held 5 s    -> disconnect from the computer and advertise again
                              (pairing is kept, so the same or another computer can connect)

  - Modulino Pixels (8 RGB LEDs), controlled from the computer over MIDI (channel 1):
      CC 20-27  brightness of LED 1-8 (0-127)
      CC 28     master brightness (0-127), multiplied with each LED's value
      CC 29/30/31  red / green / blue of all LEDs (0-127)
    LEDs start off until the computer sends a brightness.

  Uses the standard BLE MIDI GATT service, so Windows 10/11 and macOS
  need no extra drivers.

  Libraries: see midi-experiments/README.md. BLE-MIDI must come from its GitHub
  master; the 2.2 release in Library Manager does not compile on esp32 core 3.x.
  BLE uses the esp32 core's built-in BLE library (no NimBLE-Arduino).
*/

#include <Arduino_Modulino.h>
#include <BLEMIDI_Transport.h>
#include <hardware/BLEMIDI_ESP32.h>


// ---------- Configuration ----------
// 7-bit I2C addresses as used on the bus (the library's internal "pinstrap" table stores
// them doubled: 0x74/0x76, 0x7C, 0x6C). Knob is 0x3B with the alternate pinstrap.
// Pass 0xFF to let the library auto-discover a module instead.
const uint8_t KNOB_ADDRESS    = 0x3A;
const uint8_t BUTTONS_ADDRESS = 0x3E;
const uint8_t PIXELS_ADDRESS  = 0x36;

const char*   DEVICE_NAME     = "ESP32-C3 MIDI Knob";
const uint8_t MIDI_CHANNEL    = 1;
const uint8_t KNOB_CC         = 1;     // CC number sent by the knob
const uint8_t BUTTON_NOTES[3] = {60, 62, 64};   // buttons A, B, C
const uint8_t BUTTON_VELOCITY = 100;

// CC numbers received from the computer to control the Pixels
const uint8_t PIXEL_CC_FIRST  = 20;    // CC 20..27 = LED 1..8 brightness
const uint8_t PIXEL_CC_MASTER = 28;
const uint8_t PIXEL_CC_RED    = 29;
const uint8_t PIXEL_CC_GREEN  = 30;
const uint8_t PIXEL_CC_BLUE   = 31;
const uint8_t NUM_PIXELS      = 8;

const int     KNOB_STEP       = 2;     // CC change per encoder detent
const uint8_t  DISCONNECT_BUTTON = 2;  // 0 = A, 1 = B, 2 = C
const uint32_t LONG_PRESS_MS  = 5000;  // hold the disconnect button this long to disconnect
const uint32_t POLL_MS        = 10;
// -----------------------------------

BLEMIDI_CREATE_INSTANCE(DEVICE_NAME, MIDI)

ModulinoKnob    knob(KNOB_ADDRESS);
ModulinoButtons buttons(BUTTONS_ADDRESS);
ModulinoPixels  pixels(PIXELS_ADDRESS);

// Pixels state, all 0-127 as received over MIDI. Colour starts white; LEDs start off.
uint8_t ledLevel[NUM_PIXELS] = {0, 0, 0, 0, 0, 0, 0, 0};
uint8_t masterLevel = 127;
uint8_t colourRGB[3] = {127, 127, 127};
bool    pixelsDirty = false;

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

// Handlers run from MIDI.read() in loop(); they only record state, the I2C write happens in refreshPixels().
void onControlChange(byte channel, byte number, byte value) {
  if (channel != MIDI_CHANNEL) return;
  if (number >= PIXEL_CC_FIRST && number < PIXEL_CC_FIRST + NUM_PIXELS) {
    ledLevel[number - PIXEL_CC_FIRST] = value;
  } else if (number == PIXEL_CC_MASTER) {
    masterLevel = value;
  } else if (number == PIXEL_CC_RED) {
    colourRGB[0] = value;
  } else if (number == PIXEL_CC_GREEN) {
    colourRGB[1] = value;
  } else if (number == PIXEL_CC_BLUE) {
    colourRGB[2] = value;
  } else {
    return;
  }
  pixelsDirty = true;
}

void refreshPixels() {
  uint8_t r = colourRGB[0] * 2;   // 0-127 -> 0-254
  uint8_t g = colourRGB[1] * 2;
  uint8_t b = colourRGB[2] * 2;
  for (uint8_t i = 0; i < NUM_PIXELS; i++) {
    // Per-LED level scaled by master, then mapped onto the module's 0-100 brightness scale
    uint8_t brightness = (uint32_t)ledLevel[i] * masterLevel * 100 / (127 * 127);
    if (brightness == 0) {
      pixels.clear(i);
    } else {
      pixels.set(i, r, g, b, brightness);
    }
  }
  pixels.show();
  pixelsDirty = false;
}

void releaseAllNotes() {
  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    if (buttonDown[i] && connected) MIDI.sendNoteOff(BUTTON_NOTES[i], 0, MIDI_CHANNEL);
  }
}

// Drops the current connection but keeps the pairing keys: clearing them made the Mac,
// which still holds its copy, unable to reconnect. Advertising restarts from the
// disconnected handler, so this or another computer can connect again.
void disconnectHost() {
  releaseAllNotes();         // avoid a stuck note on the host when the link drops
  Serial.println("Disconnecting from the computer");
  BLEServer* server = BLEDevice::getServer();
  if (server) {
    for (auto& peer : server->getPeerDevices(false)) {
      server->disconnect(peer.first);
    }
  }
}

void setup() {
  Serial.begin(115200);

  Modulino.begin();          // starts Wire on the board's default I2C pins
  knob.begin();
  buttons.begin();
  pixels.begin();
  pixels.clear();
  pixels.show();
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

  MIDI.setHandleControlChange(onControlChange);
  MIDI.begin();
  Serial.println("Advertising as BLE MIDI device");
}

void loop() {
  MIDI.read();               // keeps the BLE MIDI transport serviced

  static uint32_t lastPoll = 0;
  if (millis() - lastPoll < POLL_MS) return;
  lastPoll = millis();

  if (pixelsDirty) refreshPixels();

  // Knob: clamp the encoder count so it maps directly onto 0..127
  {
    int count = knob.get();
    int maxCount = 127 / KNOB_STEP;
    if (count < 0) { count = 0; knob.set(0); }
    if (count > maxCount) { count = maxCount; knob.set(maxCount); }
    sendKnobValue(min(count * KNOB_STEP, 127));
  }

  // Buttons A/B/C: each sends its own note while held; holding the disconnect button disconnects
  if (buttons.update()) {
    for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
      bool down = buttons.isPressed((int)i);
      if (down && !buttonDown[i]) {
        buttonDown[i] = true;
        if (i == DISCONNECT_BUTTON) {
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
  if (buttonDown[DISCONNECT_BUTTON] && !longHandled &&
      millis() - buttonSince[DISCONNECT_BUTTON] >= LONG_PRESS_MS) {
    longHandled = true;
    disconnectHost();
  }
}
