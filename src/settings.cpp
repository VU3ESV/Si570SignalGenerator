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

// Keys per output: the Si570 keeps the original "freq"/"rf" so older saves still load;
// the Si5351 outputs use f1..f3, r1..r3 and d1..d3 (drive mA).
namespace {
struct OutputKeys {
  char freq[6], rf[6], drive[6];
};
OutputKeys outputKeys(uint8_t i) {
  OutputKeys k;
  if (i == kSi570Output) {
    snprintf(k.freq, sizeof k.freq, "freq");
    snprintf(k.rf, sizeof k.rf, "rf");
  } else {
    snprintf(k.freq, sizeof k.freq, "f%u", i);
    snprintf(k.rf, sizeof k.rf, "r%u", i);
  }
  snprintf(k.drive, sizeof k.drive, "d%u", i);
  return k;
}
}  // namespace

StoredSettings loadSettings() {
  StoredSettings s;
  Preferences p;
  if (!hasNamespace(kStateNs) || !p.begin(kStateNs, true)) return s;  // nothing saved yet
  for (uint8_t i = 0; i < kMaxOutputs; ++i) {
    const OutputKeys k = outputKeys(i);
    s.outputs[i].freqHz = p.getUInt(k.freq, s.outputs[i].freqHz);
    s.outputs[i].rfOn = p.getBool(k.rf, s.outputs[i].rfOn);
    if (i != kSi570Output) s.driveMa[i] = p.getUChar(k.drive, s.driveMa[i]);
  }
  s.selected = p.getUChar("sel", s.selected);
  s.stepIndex = p.getUChar("step", s.stepIndex);
  s.ref = p.getUChar("ref", 0) == 1 ? RefMode::External : RefMode::Internal;
  s.extXtalHz = p.getUInt("xtal", 0);
  p.end();
  return s;
}

void saveSettings(const StoredSettings& s) {
  Preferences p;
  if (!p.begin(kStateNs, false)) return;
  for (uint8_t i = 0; i < kMaxOutputs; ++i) {
    const OutputKeys k = outputKeys(i);
    p.putUInt(k.freq, s.outputs[i].freqHz);
    p.putBool(k.rf, s.outputs[i].rfOn);
    if (i != kSi570Output) p.putUChar(k.drive, s.driveMa[i]);
  }
  p.putUChar("sel", s.selected);
  p.putUChar("step", s.stepIndex);
  p.putUChar("ref", s.ref == RefMode::External ? 1 : 0);
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
  stored.si570CorrPpb = p.getInt("corr", hw.si570CorrPpb);
  stored.s5Enabled = p.getBool("s5en", hw.s5Enabled);
  stored.s5Address = p.getUChar("s5addr", hw.s5Address);
  stored.s5XtalHz = p.getUInt("s5xtal", hw.s5XtalHz);
  stored.s5CorrPpb = p.getInt("s5corr", hw.s5CorrPpb);
  stored.s5MinHz = p.getUInt("s5min", hw.s5MinHz);
  stored.s5MaxHz = p.getUInt("s5max", hw.s5MaxHz);
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
  p.putInt("corr", hw.si570CorrPpb);
  p.putBool("s5en", hw.s5Enabled);
  p.putUChar("s5addr", hw.s5Address);
  p.putUInt("s5xtal", hw.s5XtalHz);
  p.putInt("s5corr", hw.s5CorrPpb);
  p.putUInt("s5min", hw.s5MinHz);
  p.putUInt("s5max", hw.s5MaxHz);
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
  if (hw.si570CorrPpb < -500000 || hw.si570CorrPpb > 500000) return "Si570 correction must be within +/-500 ppm";
  if (hw.s5Address < 0x08 || hw.s5Address > 0x77) return "Si5351 I2C address must be 0x08-0x77";
  if (hw.s5Enabled && hw.s5Address == hw.si570Address) return "Si5351 and Si570 need different I2C addresses";
  if (hw.s5XtalHz < 10000000 || hw.s5XtalHz > 40000000) return "Si5351 crystal must be 10-40 MHz (normally 25 or 27)";
  if (hw.s5CorrPpb < -500000 || hw.s5CorrPpb > 500000) return "Si5351 correction must be within +/-500 ppm";
  if (hw.s5MinHz < 4000) return "Si5351 minimum frequency must be at least 4 kHz";
  if (hw.s5MaxHz > 225000000) return "Si5351 maximum frequency must be at most 225 MHz";
  if (hw.s5MinHz >= hw.s5MaxHz) return "Si5351 minimum must be below maximum";
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
