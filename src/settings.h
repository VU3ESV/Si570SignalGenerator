// Everything kept in NVS flash, one namespace per group so each can be reset alone:
//
//   "siggen"  operating state (power-on frequency and RF of each output, Si5351 drive
//             levels, selected output, step, reference, EXT calibration). Written only on
//             an explicit save or after a calibration, never while tuning.
//   "hw"      Si570 and Si5351 parts / limits / reference frequency, editable from the web page.
//             Defaults come from include/config.h.
//   "net"     Wi-Fi SSID / password and hostname.
//   "auth"    admin password for the web page and OTA (empty = open).
#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <ui_model.h>

#include "config.h"

// Output 0 is the Si570; outputs 1-3 are the Si5351's CLK0-CLK2.
constexpr uint8_t kSi570Output = 0;

struct StoredSettings {
  OutputState outputs[kMaxOutputs] = {{cfg::kDefaultHz, true}, {cfg::kDefaultHz, true},
                                      {cfg::kDefaultHz, true}, {cfg::kDefaultHz, true}};
  uint8_t driveMa[kMaxOutputs] = {0, cfg::kSi5351DefaultDriveMa, cfg::kSi5351DefaultDriveMa,
                                  cfg::kSi5351DefaultDriveMa};  // Si5351 outputs only
  uint8_t selected = kSi570Output;
  uint8_t stepIndex = cfg::kDefaultStepIndex;
  RefMode ref = RefMode::Internal;
  uint32_t extXtalHz = 0;  // crystal frequency measured against the external reference, 0 = none
};

struct HardwareConfig {
  uint8_t si570Address = cfg::kSi570Address;
  uint32_t si570StartupHz = cfg::kSi570StartupHz;
  uint8_t si570RegBase = cfg::kSi570RegBase;
  uint32_t minHz = cfg::kMinHz;
  uint32_t maxHz = cfg::kMaxHz;
  bool oeActiveHigh = cfg::kSi570OeActiveHigh;
  uint32_t refHz = cfg::kRefHz;
  // Si570 crystal correction used with the internal reference, ppb vs the value derived
  // from the factory start-up frequency (+ = crystal runs fast). Set from a measurement.
  int32_t si570CorrPpb = 0;

  bool s5Enabled = cfg::kSi5351Enabled;
  uint8_t s5Address = cfg::kSi5351Address;
  uint32_t s5XtalHz = cfg::kSi5351XtalHz;
  int32_t s5CorrPpb = 0;  // crystal correction, parts per billion (+ = crystal runs fast)
  uint32_t s5MinHz = cfg::kSi5351MinHz;
  uint32_t s5MaxHz = cfg::kSi5351MaxHz;

  // True if a change needs the Si570 re-initialised (and the EXT calibration dropped).
  bool sameChip(const HardwareConfig& o) const {
    return si570Address == o.si570Address && si570StartupHz == o.si570StartupHz && si570RegBase == o.si570RegBase;
  }
  // True if a change needs the Si5351 re-initialised.
  bool sameSi5351(const HardwareConfig& o) const {
    return s5Enabled == o.s5Enabled && s5Address == o.s5Address && s5XtalHz == o.s5XtalHz;
  }
};

struct NetConfig {
  char ssid[33] = "";
  char pass[65] = "";
  char hostname[33] = "";
};

StoredSettings loadSettings();
void saveSettings(const StoredSettings& s);
void saveExtXtal(uint32_t hz);  // 0 forgets it
void clearSettings();

HardwareConfig loadHardwareConfig();
void saveHardwareConfig(const HardwareConfig& hw);
const char* validateHardwareConfig(const HardwareConfig& hw);  // nullptr if OK, else the reason

NetConfig loadNetConfig();
void saveNetConfig(const NetConfig& net);
bool validHostname(const char* name);

constexpr const char* kAdminUser = "admin";
constexpr size_t kAdminPassMax = 64;
String loadAdminPassword();
void saveAdminPassword(const String& pass);
void clearAdminPassword();

// Wipes all four namespaces.
void factoryReset();
