// Build-time configuration.
//
// Pins and loop constants are fixed here. The Si570 part settings, frequency limits,
// OE polarity and reference frequency below are only DEFAULTS: they can be changed
// from the web page (Settings > Si570 hardware) without rebuilding.
#pragma once

#include <stdint.h>

namespace cfg {

// ---------------------------------------------------------------- firmware / network
// Bump on every release; shown on the web page, in /status and in mDNS.
constexpr const char* kFirmwareVersion = "0.2.0";
extern const char kFirmwareBuild[];  // defined once in main.cpp so every file reports the same stamp
constexpr const char* kDefaultHostname = "Si570-SigGen";  // http://Si570-SigGen.local/
constexpr const char* kApSsidPrefix = "Si570-Setup-";     // set-up hotspot when no Wi-Fi is configured
constexpr uint32_t kWifiConnectTimeoutMs = 20000;
constexpr uint16_t kHttpPort = 80;
constexpr uint16_t kWsPort = 81;                          // live status feed

// ---------------------------------------------------------------- Si570 part (defaults)
// These MUST match your exact part number. Look it up at
// https://www.skyworksinc.com/en/Application-Pages/Timing-Lookup-Customize
// A wrong start-up frequency makes every output frequency wrong by the same ratio;
// the firmware detects that and logs the start-up frequency it thinks you have.
constexpr uint8_t kSi570Address = 0x55;
constexpr uint32_t kSi570StartupHz = 10000000;  // factory power-on output, e.g. 570CAC000107DG
constexpr uint8_t kSi570RegBase = 7;  // 2nd option letter A/B (±50/±20 ppm): 7, C (±7 ppm): 13

// Output range offered by the UI. CMOS parts: 10-160 MHz. LVDS/LVPECL/CML parts go to
// 280/945/1417.5 MHz depending on speed grade (945-970 and 1134-1212.5 MHz can't be made).
constexpr uint32_t kMinHz = 10000000;
constexpr uint32_t kMaxHz = 160000000;

// ---------------------------------------------------------------- pins (30-pin ESP32-WROOM-32 DevKit)
// Every pin below is on the 30-pin header. Avoid GPIO12 for new wiring: pulled high at
// power-up it switches the flash supply to 1.8 V and the board will not boot.
constexpr int kPinSda = 21;
constexpr int kPinScl = 22;

// Set kPinEncA to -1 to build without an encoder (web / serial control only). With the
// pins configured but nothing connected, the internal pull-ups keep the inputs idle.
constexpr int kPinEncA = 32;   // encoder CLK
constexpr int kPinEncB = 33;   // encoder DT
constexpr int kPinEncSw = 25;  // encoder push switch (to GND)

// Si570 OE pin for RF on/off. -1 if OE is not wired (the RF mode is then hidden).
// Polarity is part-number dependent; most parts are active high.
constexpr int kPinSi570Oe = 4;
constexpr bool kSi570OeActiveHigh = true;  // default; editable from the web page

// External reference. Both inputs need 3.3 V logic levels (see README for the
// conditioning circuit). Set kPinRefIn to -1 to build without external reference support.
constexpr int kPinRefIn = 34;       // 10 MHz reference, squared up
constexpr int kPinSi570Sense = 35;  // buffered sample of the Si570 output
constexpr int kPinCountGate = 27;   // internal counter gate - leave this pin unconnected

// ---------------------------------------------------------------- encoder
constexpr uint8_t kEncTransitionsPerDetent = 4;  // 4 for KY-040 style, 2 for half-step encoders
constexpr bool kEncReverse = false;              // flip if clockwise tunes down
constexpr uint32_t kButtonDebounceMs = 30;
constexpr uint32_t kButtonLongMs = 800;

// ---------------------------------------------------------------- display
enum class DisplayType : uint8_t { Auto, Lcd, Oled, None };
constexpr DisplayType kDisplay = DisplayType::Auto;  // Auto: OLED at 0x3C/0x3D, else LCD backpack
constexpr bool kOledIsSh1106 = false;                 // most 0.96" modules are SSD1306; 1.3" are often SH1106

// ---------------------------------------------------------------- external reference loop
constexpr uint32_t kRefHz = 10000000;  // default; editable from the web page
constexpr uint32_t kRefGateMs = 1000;           // 1 s gate = 0.1 ppm count resolution
constexpr uint32_t kTrackMaxHz = 40000000;      // ESP32 pulse counter limit; above this: HOLD
constexpr uint32_t kCalibrationHz = 10000000;   // used by "calibrate now" when output > kTrackMaxHz
constexpr double kLockPpm = 0.5;

// ---------------------------------------------------------------- defaults / UI
constexpr uint32_t kDefaultHz = 10000000;
constexpr uint8_t kDefaultStepIndex = 3;  // 1 kHz
constexpr uint32_t kIdleTimeoutMs = 10000;

}  // namespace cfg
