#include "settings.h"

#include <Preferences.h>
#include <ctype.h>
#include <nvs.h>

namespace {

constexpr const char* kStateNs = "siggen";
constexpr const char* kHardwareNs = "hw";
constexpr const char* kNetNs = "net";
constexpr const char* kAuthNs = "auth";

// Preferences::begin(ns, true) logs an error when the namespace was never written,
// which is the normal first-boot case. Check quietly first.
bool hasNamespace(const char* ns) {
  nvs_handle_t h;
  if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) return false;
  nvs_close(h);
  return true;
}

void clearNamespace(const char* ns) {
  Preferences p;
  if (!p.begin(ns, false)) return;
  p.clear();
  p.end();
}

}  // namespace

// ---------------------------------------------------------------- operating state

StoredSettings loadSettings() {
  StoredSettings s;
  Preferences p;
  if (!hasNamespace(kStateNs) || !p.begin(kStateNs, true)) return s;  // nothing saved yet
  s.freqHz = p.getUInt("freq", s.freqHz);
  s.stepIndex = p.getUChar("step", s.stepIndex);
  s.ref = p.getUChar("ref", 0) == 1 ? RefMode::External : RefMode::Internal;
  s.rfOn = p.getBool("rf", s.rfOn);
  s.extXtalHz = p.getUInt("xtal", 0);
  p.end();
  return s;
}

void saveSettings(const StoredSettings& s) {
  Preferences p;
  if (!p.begin(kStateNs, false)) return;
  p.putUInt("freq", s.freqHz);
  p.putUChar("step", s.stepIndex);
  p.putUChar("ref", s.ref == RefMode::External ? 1 : 0);
  p.putBool("rf", s.rfOn);
  if (s.extXtalHz != 0) p.putUInt("xtal", s.extXtalHz);
  p.end();
}

void saveExtXtal(uint32_t hz) {
  Preferences p;
  if (!p.begin(kStateNs, false)) return;
  p.putUInt("xtal", hz);
  p.end();
}

void clearSettings() { clearNamespace(kStateNs); }

// ---------------------------------------------------------------- hardware

HardwareConfig loadHardwareConfig() {
  HardwareConfig hw;
  Preferences p;
  if (!hasNamespace(kHardwareNs) || !p.begin(kHardwareNs, true)) return hw;
  HardwareConfig stored;
  stored.si570Address = p.getUChar("addr", hw.si570Address);
  stored.si570StartupHz = p.getUInt("startup", hw.si570StartupHz);
  stored.si570RegBase = p.getUChar("regbase", hw.si570RegBase);
  stored.minHz = p.getUInt("min", hw.minHz);
  stored.maxHz = p.getUInt("max", hw.maxHz);
  stored.oeActiveHigh = p.getBool("oehigh", hw.oeActiveHigh);
  stored.refHz = p.getUInt("ref", hw.refHz);
  p.end();
  // Never boot into a configuration the web page would have refused.
  return validateHardwareConfig(stored) == nullptr ? stored : hw;
}

void saveHardwareConfig(const HardwareConfig& hw) {
  Preferences p;
  if (!p.begin(kHardwareNs, false)) return;
  p.putUChar("addr", hw.si570Address);
  p.putUInt("startup", hw.si570StartupHz);
  p.putUChar("regbase", hw.si570RegBase);
  p.putUInt("min", hw.minHz);
  p.putUInt("max", hw.maxHz);
  p.putBool("oehigh", hw.oeActiveHigh);
  p.putUInt("ref", hw.refHz);
  p.end();
}

const char* validateHardwareConfig(const HardwareConfig& hw) {
  if (hw.si570Address < 0x08 || hw.si570Address > 0x77) return "I2C address must be 0x08-0x77";
  if (hw.si570StartupHz < 3500000 || hw.si570StartupHz > 1417500000) return "start-up frequency must be 3.5-1417.5 MHz";
  if (hw.si570RegBase != 7 && hw.si570RegBase != 13) return "register bank must be 7 or 13";
  if (hw.minHz < 3500000) return "minimum frequency must be at least 3.5 MHz";
  if (hw.maxHz > 1417500000) return "maximum frequency must be at most 1417.5 MHz";
  if (hw.minHz >= hw.maxHz) return "minimum must be below maximum";
  if (hw.refHz < 1000000 || hw.refHz > cfg::kTrackMaxHz) return "reference must be 1-40 MHz";
  return nullptr;
}

// ---------------------------------------------------------------- network

NetConfig loadNetConfig() {
  NetConfig n;
  strncpy(n.hostname, cfg::kDefaultHostname, sizeof(n.hostname) - 1);
  Preferences p;
  if (!hasNamespace(kNetNs) || !p.begin(kNetNs, true)) return n;
  p.getString("ssid", n.ssid, sizeof(n.ssid));
  p.getString("pass", n.pass, sizeof(n.pass));
  String host = p.getString("host", "");
  if (validHostname(host.c_str())) strncpy(n.hostname, host.c_str(), sizeof(n.hostname) - 1);
  p.end();
  return n;
}

void saveNetConfig(const NetConfig& n) {
  Preferences p;
  if (!p.begin(kNetNs, false)) return;
  p.putString("ssid", n.ssid);
  p.putString("pass", n.pass);
  p.putString("host", n.hostname);
  p.end();
}

// DNS label rules: 1-31 letters, digits or hyphens, not starting or ending with a hyphen.
bool validHostname(const char* name) {
  const size_t len = strlen(name);
  if (len == 0 || len > 31 || name[0] == '-' || name[len - 1] == '-') return false;
  for (size_t i = 0; i < len; ++i)
    if (!isalnum((unsigned char)name[i]) && name[i] != '-') return false;
  return true;
}

// ---------------------------------------------------------------- admin password

String loadAdminPassword() {
  Preferences p;
  if (!hasNamespace(kAuthNs) || !p.begin(kAuthNs, true)) return String();
  String v = p.getString("pass", "");
  p.end();
  return v;
}

void saveAdminPassword(const String& pass) {
  Preferences p;
  if (!p.begin(kAuthNs, false)) return;
  p.putString("pass", pass);
  p.end();
}

void clearAdminPassword() { clearNamespace(kAuthNs); }

void factoryReset() {
  clearNamespace(kStateNs);
  clearNamespace(kHardwareNs);
  clearNamespace(kNetNs);
  clearNamespace(kAuthNs);
}
