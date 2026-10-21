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

  - Modulino Movement, gyroscope rotation speed -> CC 102/103/104 (roll/pitch/yaw),
    64 when still, lower/higher when rotating one way or the other.
  - Modulino Movement, tap (sensor's built-in tap engine) -> note 36 (kick) on channel 10,
    velocity estimated from how hard the tap was. Two quick taps give two notes.

  Uses the standard BLE MIDI GATT service, so Windows 10/11 and macOS
  need no extra drivers.

  Libraries: see midi-experiments/README.md. BLE-MIDI must come from its GitHub
  master; the 2.2 release in Library Manager does not compile on esp32 core 3.x.
  BLE uses the esp32 core's built-in BLE library (no NimBLE-Arduino).
*/

#include <Arduino_Modulino.h>
#include <BLEMIDI_Transport.h>
#include <hardware/BLEMIDI_ESP32.h>
#include <esp_mac.h>


// ---------- Configuration ----------
// 7-bit I2C addresses as used on the bus (the library's internal "pinstrap" table stores
// them doubled: 0x74/0x76, 0x7C, 0x6C). Knob is 0x3B with the alternate pinstrap.
// Pass 0xFF to let the library auto-discover a module instead.
const uint8_t KNOB_ADDRESS    = 0x3A;
const uint8_t BUTTONS_ADDRESS = 0x3E;
const uint8_t PIXELS_ADDRESS  = 0x36;
// The Movement module is a plain LSM6DSOX (no Modulino MCU), so this is already the bus address.
// Some modules use 0x6B instead (the library lists both as fixed addresses).
const uint8_t MOVEMENT_ADDRESS = 0x6A;

const char*   DEVICE_NAME     = "ESP32-C3 MIDI Knob";  // the last 4 hex digits of the Bluetooth MAC are appended
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

// Gyroscope -> CC: rotation speed in degrees per second, centred at 64 when still
const uint8_t GYRO_CC[3]          = {102, 103, 104};  // roll, pitch, yaw
const float   GYRO_FULL_SCALE_DPS = 250.0;  // speed that reaches 0 or 127
const float   GYRO_DEADZONE_DPS   = 5.0;    // below this the axis counts as still (sensor noise)
const float   GYRO_SMOOTHING      = 0.3;    // 0..1, weight of each new reading (lower = smoother)
const uint32_t GYRO_SEND_MS       = 20;     // at most 50 CC updates per second per axis

// Tap -> drum note, using the LSM6DSOX's built-in single-tap detection
const uint8_t  TAP_NOTE         = 36;   // General MIDI kick drum
const uint8_t  TAP_CHANNEL      = 10;   // General MIDI drum channel
const uint8_t  TAP_THRESHOLD    = 4;    // 1..31, x 125 mg at the +-4 g range (4 = 0.5 g); lower = more sensitive
const uint8_t  TAP_AXES         = 0x02; // TAP_CFG0 enable bits: 0x02 = Z, 0x04 = Y, 0x08 = X (Z = tapping the table or the board's top)
const uint32_t TAP_NOTE_MS      = 50;   // note length sent to the computer
const float    TAP_VEL_MIN_G    = 0.1;  // peak acceleration change mapped to the lowest velocity
const float    TAP_VEL_MAX_G    = 2.0;  // ... and to velocity 127
const uint8_t  TAP_VEL_FLOOR    = 20;   // velocity used for the lightest taps

const int     KNOB_STEP       = 2;     // CC change per encoder detent
const uint8_t  DISCONNECT_BUTTON = 2;  // 0 = A, 1 = B, 2 = C
const uint32_t LONG_PRESS_MS  = 5000;  // hold the disconnect button this long to disconnect
const uint32_t POLL_MS        = 10;
// -----------------------------------

BLEMIDI_CREATE_INSTANCE(DEVICE_NAME, MIDI)

ModulinoKnob    knob(KNOB_ADDRESS);
ModulinoButtons buttons(BUTTONS_ADDRESS);
ModulinoPixels  pixels(PIXELS_ADDRESS);
ModulinoMovement movement(MOVEMENT_ADDRESS);

// Pixels state, all 0-127 as received over MIDI. Colour starts white; LEDs start off.
uint8_t ledLevel[NUM_PIXELS] = {0, 0, 0, 0, 0, 0, 0, 0};
uint8_t masterLevel = 127;
uint8_t colourRGB[3] = {127, 127, 127};
bool    pixelsDirty = false;

bool connected = false;
int  lastCC    = -1;

bool  movementOk      = false;
float gyroFiltered[3] = {0, 0, 0};
int   lastGyroCC[3]   = {-1, -1, -1};

float    lastAccel[3]  = {0, 0, 0};
float    accelPeak     = 0;    // largest recent change in acceleration, decays every poll
bool     tapNoteOn     = false;
uint32_t tapNoteOffAt  = 0;

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

// ---- Raw LSM6DSOX register access (the Modulino library does not expose tap detection) ----
// Register addresses and bit layouts from ST's lsm6dsox_reg.h.
const uint8_t LSM6DSOX_TAP_SRC     = 0x1C;  // bit 6 TAP_IA, bit 5 SINGLE_TAP
const uint8_t LSM6DSOX_CTRL1_XL    = 0x10;
const uint8_t LSM6DSOX_TAP_CFG0    = 0x56;
const uint8_t LSM6DSOX_TAP_CFG1    = 0x57;
const uint8_t LSM6DSOX_TAP_CFG2    = 0x58;
const uint8_t LSM6DSOX_TAP_THS_6D  = 0x59;
const uint8_t LSM6DSOX_INT_DUR2    = 0x5A;
const uint8_t LSM6DSOX_WAKE_UP_THS = 0x5B;
const uint8_t LSM6DSOX_MD1_CFG     = 0x5E;

void imuWrite(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(MOVEMENT_ADDRESS);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

int imuRead(uint8_t reg) {
  Wire.beginTransmission(MOVEMENT_ADDRESS);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return -1;
  if (Wire.requestFrom(MOVEMENT_ADDRESS, (uint8_t)1) != 1) return -1;
  return Wire.read();
}

// Called after movement.begin(), which sets the accelerometer to 104 Hz, +-4 g (0x4A).
void setupTapDetection() {
  imuWrite(LSM6DSOX_CTRL1_XL, 0x6A);          // 417 Hz for the tap engine; keep +-4 g so the library's scaling stays right
  imuWrite(LSM6DSOX_TAP_CFG0, 0x40 | TAP_AXES | 0x01);  // clear on read, chosen axes, latched
  imuWrite(LSM6DSOX_TAP_CFG1, TAP_THRESHOLD & 0x1F);    // X threshold, default axis priority
  imuWrite(LSM6DSOX_TAP_CFG2, 0x80 | (TAP_THRESHOLD & 0x1F));  // enable basic interrupts, Y threshold
  imuWrite(LSM6DSOX_TAP_THS_6D, TAP_THRESHOLD & 0x1F);  // Z threshold
  imuWrite(LSM6DSOX_INT_DUR2, (3 << 2) | 3);  // quiet 29 ms, shock 57 ms at 417 Hz (ST's example values)
  imuWrite(LSM6DSOX_WAKE_UP_THS, 0x00);       // single tap only: each tap is reported on its own
  imuWrite(LSM6DSOX_MD1_CFG, 0x40);           // route single tap to INT1 (not wired; needed for the event logic)
}

// Tracks how sharply the acceleration changes, as a rough measure of tap strength.
void trackAccelPeak() {
  float a[3] = {movement.getX(), movement.getY(), movement.getZ()};
  float change = sqrtf(sq(a[0] - lastAccel[0]) + sq(a[1] - lastAccel[1]) + sq(a[2] - lastAccel[2]));
  for (uint8_t i = 0; i < 3; i++) lastAccel[i] = a[i];
  accelPeak = max(accelPeak * 0.5f, change);
}

uint8_t tapVelocity() {
  if (accelPeak <= TAP_VEL_MIN_G) return TAP_VEL_FLOOR;
  float t = (accelPeak - TAP_VEL_MIN_G) / (TAP_VEL_MAX_G - TAP_VEL_MIN_G);
  return constrain((int)(TAP_VEL_FLOOR + t * (127 - TAP_VEL_FLOOR)), TAP_VEL_FLOOR, 127);
}

void updateTap() {
  if (tapNoteOn && (int32_t)(millis() - tapNoteOffAt) >= 0) {
    if (connected) MIDI.sendNoteOff(TAP_NOTE, 0, TAP_CHANNEL);
    tapNoteOn = false;
  }
  int src = imuRead(LSM6DSOX_TAP_SRC);
  if (src < 0 || !(src & 0x20)) return;   // SINGLE_TAP
  if (!connected) return;
  if (tapNoteOn) MIDI.sendNoteOff(TAP_NOTE, 0, TAP_CHANNEL);  // retrigger
  MIDI.sendNoteOn(TAP_NOTE, tapVelocity(), TAP_CHANNEL);
  tapNoteOn = true;
  tapNoteOffAt = millis() + TAP_NOTE_MS;
}

// Maps a rotation speed onto 0..127 with 64 meaning "still".
int gyroToCC(float dps) {
  if (fabsf(dps) < GYRO_DEADZONE_DPS) dps = 0;
  int cc = 64 + (int)lroundf(dps / GYRO_FULL_SCALE_DPS * 63.0f);
  return constrain(cc, 0, 127);
}

// Uses the readings taken by movement.update() in loop().
void updateGyro() {
  float rates[3] = {movement.getRoll(), movement.getPitch(), movement.getYaw()};
  for (uint8_t i = 0; i < 3; i++) {
    gyroFiltered[i] += GYRO_SMOOTHING * (rates[i] - gyroFiltered[i]);
    int cc = gyroToCC(gyroFiltered[i]);
    if (cc != lastGyroCC[i] && connected) {
      MIDI.sendControlChange(GYRO_CC[i], cc, MIDI_CHANNEL);
      lastGyroCC[i] = cc;
    }
  }
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
  movementOk = movement.begin();
  if (movementOk) setupTapDetection();
  Serial.printf("Knob at 0x%02X, buttons at 0x%02X\n", knob.getAddress(), buttons.getAddress());
  Serial.printf("Movement at 0x%02X: %s\n", MOVEMENT_ADDRESS, movementOk ? "found" : "NOT found");

  BLEMIDI.setHandleConnected([]() {
    connected = true;
    lastCC = -1;             // force a resend of the current values
    for (uint8_t i = 0; i < 3; i++) lastGyroCC[i] = -1;
    Serial.println("BLE MIDI connected");
  });
  BLEMIDI.setHandleDisconnected([]() {
    connected = false;
    Serial.println("BLE MIDI disconnected");
    // The core BLE transport does not re-advertise by itself after a disconnect
    BLEDevice::startAdvertising();
  });

  MIDI.setHandleControlChange(onControlChange);
  // Make each board distinguishable: append the last two bytes of its factory Bluetooth MAC.
  // The name must fit BLE-MIDI's 24-byte buffer (23 characters).
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_BT);
  static char bleName[24];
  snprintf(bleName, sizeof(bleName), "%s %02X%02X", DEVICE_NAME, mac[4], mac[5]);
  BLEMIDI.setName(bleName);
  Serial.printf("Bluetooth name \"%s\", MAC %02X:%02X:%02X:%02X:%02X:%02X\n",
                bleName, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

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

  if (movementOk && movement.update()) {
    trackAccelPeak();
    updateTap();
    static uint32_t lastGyroSend = 0;
    if (millis() - lastGyroSend >= GYRO_SEND_MS) {
      lastGyroSend = millis();
      updateGyro();
    }
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
