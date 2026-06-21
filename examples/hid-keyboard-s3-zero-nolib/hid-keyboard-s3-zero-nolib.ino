// ESP32-S3-Zero HID Keyboard + Vendor Protocol (single interface)
// Press BOOT (GPIO0) to type default markup text.
// RGB LED (GPIO21): idle color, green=typing.
// Servo MG90S on GPIO1 (PWM 50Hz via LEDC).
//
// Commands (Report ID 0x07, host→device):
//   led <R> <G> <B>
//   type|default "<markup>"
//   boot  — simulate BOOT press
//   status — get device status
//   flash — reboot into firmware update
//   delay <base> [rand]  — typing speed (ms), default 60+random(60)
//   raw <hex> — keyboard LED byte
//
// Markup in type/default:
//   <enter> <up> <down> <left> <right> <tab>
//   <wN>  — wait N ms
//   <sN>  — servo angle 0-180
//   <ctrl>X <shift>X <option>X <command>X  (X=single char)
//   <<  — literal <

#include <Arduino.h>
#include <string.h>
#include <stdlib.h>
#include "USB.h"
#include "esp32-hal-rmt.h"
#include "USBHID.h"
#include "esp_system.h"

// Keyboard layout (extern from USBHIDKeyboard lib)
#define KEY_RETURN   0xB0
extern const uint8_t KeyboardLayout_pt_BR[];

// HID constants
#define RID_KBD       1
#define RID_VENDOR_OUT 7
#define RID_VENDOR_IN  8

// Keyboard LED event data (from USBHIDKeyboard.h)
typedef union {
  struct {
    uint8_t numlock:1, capslock:1, scrolllock:1, compose:1, kana:1, rsv:3;
  };
  uint8_t leds;
} keyboard_led_data_t;

// Pins
#define LED_PIN      21
#define BTN_PIN       0
#define SERVO_PIN     1

// HID keycodes (USB HID Usage Table, Keyboard/Keypad page)
#define HID_RETURN   0x28
#define HID_TAB      0x2B
#define HID_UP       0x52
#define HID_DOWN     0x51
#define HID_LEFT     0x50
#define HID_RIGHT    0x4F

// Modifier HIDs (0xE0-0xE7 → modifier bit 0-7)
#define MOD_LCTRL    0xE0
#define MOD_LSHIFT   0xE1
#define MOD_LALT     0xE2
#define MOD_LGUI     0xE3

// Markup flags from USBHIDKeyboard
#define SHIFT        0x80
#define ALT_GR       0x40
#define ISO_REPL     0xFE
#define ISO_KEY      0x64

// ESP32-S3: RTC register to force download mode
#define RTC_OPTION1  0x600080FC
#define RTC_FORCE_DFU (1 << 2)

// ── State ──
static bool capslockOn  = false;
static uint8_t curR = 0, curG = 0, curB = 8;
static char defaultText[63] = "hello world!";
static volatile bool isTyping = false;
static uint8_t dlyBase = 60, dlyRand = 60;

typedef struct { uint8_t modifiers, rsv, keys[6]; } KeyRep;
static KeyRep keyRep;

// ── Helpers ──
static int charDelay() {
  return dlyBase + (dlyRand ? random(dlyRand + 1) : 0);
}

// ── RGB LED ──
static void setLed(uint8_t r, uint8_t g, uint8_t b) {
  static int initPin = -1;
  static bool ready = false;
  if (initPin != LED_PIN) {
    ready = rmtInit(LED_PIN, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 10000000UL);
    if (ready) { rmtSetEOT(LED_PIN, LOW); initPin = LED_PIN; }
  }
  if (!ready) return;
  rmt_data_t sym[24];
  const rmt_data_t v[2] = {{8,1,5,0},{4,1,9,0}};
  uint8_t idx = 0;
  for (uint8_t ch : {g,r,b})
    for (uint8_t m = 0x80; m; m >>= 1) sym[idx++] = v[!(ch & m)];
  rmtWrite(LED_PIN, sym, RMT_SYMBOLS_OF(sym), RMT_WAIT_FOR_EVER);
}

static void idleLed() {
  setLed(capslockOn ? 16 : curR, capslockOn ? 0 : curG, capslockOn ? 16 : curB);
}

// ── Servo ──
static void setServo(int angle) {
  if (angle < 0) angle = 0;
  if (angle > 180) angle = 180;
  // MG90S: 0°→0.5ms, 180°→2.4ms, period=20ms, 14-bit PWM
  int duty = 410 + (1556 * angle / 180);
  ledcWrite(SERVO_PIN, duty);
}

// ── HID Report Descriptor (single interface) ──
static const uint8_t rptDesc[] = {
  0x05, 0x01,         // USAGE_PAGE (Generic Desktop)
  0x09, 0x06,         // USAGE (Keyboard)
  0xA1, 0x01,         // COLLECTION (Application)

  // Keyboard Input, Report ID 1
  0x85, RID_KBD,
  0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7,
  0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, // mods
  0x95, 0x01, 0x75, 0x08, 0x81, 0x01,                         // reserved
  0x05, 0x07, 0x19, 0x00, 0x29, 0xFF,
  0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x06, 0x81, 0x00, // keys

  // Keyboard Output – LEDs, Report ID 1
  0x05, 0x08, 0x19, 0x01, 0x29, 0x05,
  0x75, 0x01, 0x95, 0x05, 0x91, 0x02,
  0x95, 0x03, 0x91, 0x01,

  // Vendor Output, Report ID 7
  0x06, 0x00, 0xFF, 0x09, 0x01,
  0x85, RID_VENDOR_OUT,
  0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x3F, 0x91, 0x02,

  // Vendor Input, Report ID 8
  0x09, 0x02,
  0x85, RID_VENDOR_IN,
  0x75, 0x08, 0x95, 0x08, 0x81, 0x02,

  0xC0
};

// ── MyHID class ──
class MyHID : public USBHIDDevice {
  USBHID hid;
  volatile bool cmdPending;
  uint8_t cmdData[63], cmdLen;
public:
  MyHID() : hid(HID_ITF_PROTOCOL_NONE), cmdPending(false), cmdLen(0) {
    memset(cmdData, 0, sizeof(cmdData));
    memset(&keyRep, 0, sizeof(keyRep));
    hid.addDevice(this, sizeof(rptDesc));
  }
  void begin() { hid.begin(); }

  // USB callbacks
  uint16_t _onGetDescriptor(uint8_t *dst) override {
    memcpy(dst, rptDesc, sizeof(rptDesc)); return sizeof(rptDesc);
  }
  void _onOutput(uint8_t rid, const uint8_t *buf, uint16_t len) override {
    if (rid == RID_KBD && len >= 1) {
      keyboard_led_data_t p; p.leds = buf[0];
      capslockOn = p.capslock; idleLed();
    } else if (rid == RID_VENDOR_OUT && !cmdPending) {
      cmdLen = (len > 63) ? 63 : (uint8_t)len;
      memcpy(cmdData, buf, cmdLen); cmdPending = true;
    }
  }

  // Command queue
  bool hasCmd() const { return cmdPending; }
  void takeCmd(uint8_t *b, uint8_t *l) { *l = cmdLen; memcpy(b, cmdData, cmdLen); cmdPending = false; }

  // Keyboard reports
  void sendRep() { hid.SendReport(RID_KBD, &keyRep, sizeof(keyRep)); }
  void pressRaw(uint8_t k) {
    if (k >= 0xE0 && k < 0xE8)      keyRep.modifiers |= (1 << (k - 0xE0));
    else if (k && k < 0xA5) {
      for (int i = 0; i < 6; i++) if (!keyRep.keys[i]) { keyRep.keys[i] = k; break; }
    }
    sendRep();
  }
  void releaseRaw(uint8_t k) {
    if (k >= 0xE0 && k < 0xE8)      keyRep.modifiers &= ~(1 << (k - 0xE0));
    else if (k) for (int i = 0; i < 6; i++) if (keyRep.keys[i] == k) keyRep.keys[i] = 0;
    sendRep();
  }
  void releaseAll() { memset(&keyRep, 0, sizeof(keyRep)); sendRep(); }

  // Map ASCII → HID via layout
  void pressKey(uint8_t a, const uint8_t *lay) {
    uint8_t k;
    if (a >= 0x88)      k = a - 0x88;
    else if (a >= 0x80) { keyRep.modifiers |= (1 << (a - 0x80)); k = 0; }
    else {
      k = lay[a]; if (!k) return;
      if (k & SHIFT) { keyRep.modifiers |= 0x02; k &= ~SHIFT; }
      if (k & ALT_GR){ keyRep.modifiers |= 0x40; k &= ~ALT_GR; }
      if (k == ISO_REPL) k = ISO_KEY;
    }
    if (k) pressRaw(k); else sendRep();
  }
  void releaseKey(uint8_t a, const uint8_t *lay) {
    uint8_t k;
    if (a >= 0x88)      k = a - 0x88;
    else if (a >= 0x80) { keyRep.modifiers &= ~(1 << (a - 0x80)); k = 0; }
    else {
      k = lay[a]; if (!k) return;
      if (k & SHIFT) { keyRep.modifiers &= ~0x02; k &= ~SHIFT; }
      if (k & ALT_GR){ keyRep.modifiers &= ~0x40; k &= ~ALT_GR; }
      if (k == ISO_REPL) k = ISO_KEY;
    }
    if (k) releaseRaw(k); else sendRep();
  }

  void typeChar(uint8_t a, const uint8_t *lay) {
    pressKey(a, lay); delay(charDelay()); releaseKey(a, lay); delay(charDelay()/2);
  }
  void typeRaw(uint8_t h) {
    pressRaw(h); delay(charDelay()); releaseRaw(h); delay(charDelay()/2);
  }
  void typeWithMod(uint8_t mod, uint8_t a, const uint8_t *lay) {
    pressRaw(mod); delay(10); typeChar(a, lay); delay(10); releaseRaw(mod);
  }

  // Vendor input
  void sendStatus() {
    uint8_t st[8] = {
      isTyping ? 1 : 0, capslockOn ? 1 : 0, curR, curG, curB,
      (uint8_t)strlen(defaultText), dlyBase, dlyRand
    };
    hid.SendReport(RID_VENDOR_IN, st, sizeof(st));
  }
};
MyHID HID;

// ── Markup parser ──
static void flashErr() { setLed(64,0,0); delay(100); idleLed(); }

static void typeMarkup(const char *s) {
  isTyping = true;
  setLed(0, 32, 0);
  for (int i = 0; s[i]; i++) {
    if (s[i] != '<') { HID.typeChar((uint8_t)s[i], KeyboardLayout_pt_BR); continue; }
    // << → literal <
    if (s[i+1] == '<') { HID.typeChar('<', KeyboardLayout_pt_BR); i++; continue; }
    // Find end of tag
    const char *end = strchr(s + i, '>');
    if (!end) { HID.typeChar('<', KeyboardLayout_pt_BR); continue; }
    int tLen = (int)(end - (s + i) - 1);
    if (tLen <= 0) { i = (int)(end - s); continue; }
    char tag[32]; int cp = (tLen > 31) ? 31 : tLen;
    memcpy(tag, s + i + 1, cp); tag[cp] = 0;

    // Modifier tags (consume next character)
    uint8_t mod = 0;
    if      (!strcmp(tag, "ctrl"))    mod = MOD_LCTRL;
    else if (!strcmp(tag, "shift"))   mod = MOD_LSHIFT;
    else if (!strcmp(tag, "option"))  mod = MOD_LALT;
    else if (!strcmp(tag, "command")) mod = MOD_LGUI;
    if (mod) {
      i = (int)(end - s) + 1;
      if (s[i]) HID.typeWithMod(mod, (uint8_t)s[i], KeyboardLayout_pt_BR);
      continue;
    }
    // Non-modifier tags
    if      (!strcmp(tag, "enter"))  HID.typeRaw(HID_RETURN);
    else if (!strcmp(tag, "up"))     HID.typeRaw(HID_UP);
    else if (!strcmp(tag, "down"))   HID.typeRaw(HID_DOWN);
    else if (!strcmp(tag, "left"))   HID.typeRaw(HID_LEFT);
    else if (!strcmp(tag, "right"))  HID.typeRaw(HID_RIGHT);
    else if (!strcmp(tag, "tab"))    HID.typeRaw(HID_TAB);
    else if (tag[0] == 'w' || tag[0] == 'W') { int ms = atoi(tag+1); if (ms>0) delay(ms); }
    else if (tag[0] == 's' || tag[0] == 'S') { int a = atoi(tag+1); if (a>=0 && a<=180) setServo(a); }
    else flashErr();
    i = (int)(end - s);
  }
  isTyping = false;
}

// ── Commands ──
static void execCmd(const uint8_t *buf, uint8_t len) {
  if (len < 1) return;
  switch (buf[0]) {
    case 0x01: // SET_LED
      if (len >= 4) { curR = buf[1]; curG = buf[2]; curB = buf[3]; idleLed(); }
      break;
    case 0x02: // TYPE
      if (len >= 2) { typeMarkup((const char*)(buf+1)); idleLed(); }
      break;
    case 0x05: // SET_DEFAULT
      if (len >= 2) { strncpy(defaultText, (const char*)(buf+1), 62); defaultText[62] = 0; }
      break;
    case 0x06: // PRESS_BOOT
      setLed(64,0,0); delay(30); typeMarkup(defaultText); idleLed();
      break;
    case 0x07: // GET_STATUS
      HID.sendStatus();
      break;
    case 0x08: // ENTER_FLASH
      *(volatile uint32_t*)RTC_OPTION1 |= RTC_FORCE_DFU;
      delay(10); esp_restart();
      break;
    case 0x09: // SET_DELAY
      dlyBase = buf[1];
      dlyRand = (len >= 3) ? buf[2] : 0;
      break;
  }
}

// ── Init & Loop ──
void setup() {
  randomSeed(micros());
  ledcAttach(SERVO_PIN, 50, 14);
  USB.manufacturerName("Keychron"); USB.productName("Keychron S3");
  USB.VID(0x303A); USB.PID(0x1001);
  USB.begin(); HID.begin();
  pinMode(BTN_PIN, INPUT_PULLUP);
  setLed(0,0,0); delay(100); idleLed();
}

void loop() {
  uint8_t buf[63], len;
  if (HID.hasCmd()) { HID.takeCmd(buf, &len); execCmd(buf, len); }
  static bool lastBtn = HIGH;
  static unsigned long lastPress = 0;
  bool btn = digitalRead(BTN_PIN);
  if (lastBtn == HIGH && btn == LOW && millis() - lastPress > 250) {
    lastPress = millis();
    setLed(64,0,0); delay(30); typeMarkup(defaultText); idleLed();
  }
  lastBtn = btn;
}
