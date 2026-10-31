// Everything kept in NVS flash, one namespace per group so each can be reset alone:
//
//   "siggen"  operating state (power-on frequency, step, reference, RF, EXT calibration).
//             Written only on an explicit save or after a calibration, never while tuning.
//   "hw"      Si570 part / limits / reference frequency, editable from the web page.
//             Defaults come from include/config.h.
//   "net"     Wi-Fi SSID / password and hostname.
//   "auth"    admin password for the web page and OTA (empty = open).
#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <ui_model.h>

#include "config.h"

struct StoredSettings {
  uint32_t freqHz = cfg::kDefaultHz;
  uint8_t stepIndex = cfg::kDefaultStepIndex;
  RefMode ref = RefMode::Internal;
  bool rfOn = true;
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

  // True if a change needs the Si570 re-initialised (and the EXT calibration dropped).
  bool sameChip(const HardwareConfig& o) const {
    return si570Address == o.si570Address && si570StartupHz == o.si570StartupHz && si570RegBase == o.si570RegBase;
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
