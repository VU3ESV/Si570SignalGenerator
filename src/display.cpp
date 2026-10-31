#include "display.h"

#include <Wire.h>
#include <string.h>

#include "config.h"

bool DisplayModel::operator==(const DisplayModel& o) const {
  return output == o.output && outputCount == o.outputCount && driveMa == o.driveMa && freqHz == o.freqHz && stepHz == o.stepHz && mode == o.mode && hasRef == o.hasRef && ref == o.ref &&
         refStatus == o.refStatus && refErrorCentiPpm == o.refErrorCentiPpm && hasRf == o.hasRf &&
         rfOn == o.rfOn && strcmp(message, o.message) == 0;
}

const char* outputTag(uint8_t output) {
  static const char* const kTags[] = {"570", "CK0", "CK1", "CK2"};
  return output < sizeof kTags / sizeof kTags[0] ? kTags[output] : "?";
}

namespace {

class NullDisplay : public Display {
 public:
  const char* name() const override { return "none (serial only)"; }
  void showSplash(const char*, const char*) override {}
  void render(const DisplayModel&) override {}
};

bool probe(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

}  // namespace

std::unique_ptr<Display> createDisplay() {
  using cfg::DisplayType;
  const bool autoDetect = cfg::kDisplay == DisplayType::Auto;

  if (autoDetect || cfg::kDisplay == DisplayType::Oled) {
    static const uint8_t kOledAddresses[] = {0x3C, 0x3D};
    for (uint8_t a : kOledAddresses) {
      if (!probe(a)) continue;
      if (auto d = makeOledDisplay(a)) return d;
    }
  }
  if (autoDetect || cfg::kDisplay == DisplayType::Lcd) {
    // PCF8574 (0x20-0x27) and PCF8574A (0x38-0x3F) backpacks, most common first.
    static const uint8_t kCandidates[] = {0x27, 0x3F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26,
                                          0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E};
    for (uint8_t a : kCandidates) {
      if (autoDetect && (a == 0x3C || a == 0x3D)) continue;  // OLED addresses
      if (!probe(a)) continue;
      if (auto d = makeLcdDisplay(a)) return d;
    }
  }
  return std::unique_ptr<Display>(new NullDisplay());
}
