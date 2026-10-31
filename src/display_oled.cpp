// 128x64 OLED layout
//
//   "EXT LOCK +0.02ppm"          reference status (6x10 font)
//          "14.250 000"          frequency (10x20 font), digit being tuned underlined/boxed
//   "Step 1kHz         MHz"      or a message
//   [TUNE] STEP  REF  RF:ON      modes, current one inverted
#include <U8g2lib.h>
#include <stdio.h>
#include <string.h>

#include <freq_format.h>
#include <type_traits>

#include "config.h"
#include "display.h"

namespace {

using OledDriver = std::conditional_t<cfg::kOledIsSh1106, U8G2_SH1106_128X64_NONAME_F_HW_I2C,
                                      U8G2_SSD1306_128X64_NONAME_F_HW_I2C>;

constexpr int kWidth = 128;
constexpr int kBigGlyph = 10;  // u8g2_font_10x20 is monospaced
constexpr int kSmallGlyph = 6;
constexpr int kFreqBaseline = 32;

class OledDisplay : public Display {
 public:
  explicit OledDisplay(uint8_t address) : g_(U8G2_R0) { g_.setI2CAddress(address << 1); }

  bool begin() {
    g_.setBusClock(400000);
    return g_.begin();
  }

  const char* name() const override { return cfg::kOledIsSh1106 ? "SH1106 OLED" : "SSD1306 OLED"; }

  void showSplash(const char* line1, const char* line2) override {
    g_.clearBuffer();
    g_.setFont(u8g2_font_10x20_mf);
    g_.drawStr(0, 28, line1);
    g_.setFont(u8g2_font_6x10_mf);
    g_.drawStr(0, 50, line2);
    g_.sendBuffer();
  }

  void render(const DisplayModel& m) override {
    char text[24];
    g_.clearBuffer();

    g_.setFont(u8g2_font_6x10_mf);
    if (!m.hasRef)
      snprintf(text, sizeof text, "Si570 signal gen");
    else if (m.ref == RefMode::Internal)
      snprintf(text, sizeof text, "REF INT");
    else if (m.refStatus == RefStatus::Tracking || m.refStatus == RefStatus::Locked)
      snprintf(text, sizeof text, "EXT %s %+.2fppm", refStatusLabel(m.refStatus), m.refErrorCentiPpm / 100.0);
    else
      snprintf(text, sizeof text, "EXT %s", refStatusLabel(m.refStatus));
    g_.drawStr(0, 8, text);

    char freq[16];
    const size_t len = formatFrequency(freq, sizeof freq, m.freqHz);
    const int x0 = kWidth - kBigGlyph * (int)len;
    g_.setFont(u8g2_font_10x20_mf);
    g_.drawStr(x0, kFreqBaseline, freq);
    const int idx = digitIndexForDecade(freq, len, decadeOf(m.stepHz));
    if (idx >= 0) {
      const int x = x0 + idx * kBigGlyph;
      if (m.mode == Mode::Step)
        g_.drawFrame(x - 1, kFreqBaseline - 17, kBigGlyph + 2, 21);
      else if (m.mode == Mode::Tune)
        g_.drawHLine(x, kFreqBaseline + 2, kBigGlyph - 1);
    }

    g_.setFont(u8g2_font_6x10_mf);
    if (m.message[0] != '\0') {
      g_.drawStr(0, 46, m.message);
    } else {
      snprintf(text, sizeof text, "Step %s", stepLabel(m.stepHz));
      g_.drawStr(0, 46, text);
      g_.drawStr(kWidth - 3 * kSmallGlyph, 46, "MHz");
    }

    static const Mode kModes[] = {Mode::Tune, Mode::Step, Mode::Ref, Mode::Rf};
    int x = 0;
    for (Mode mode : kModes) {
      if ((mode == Mode::Ref && !m.hasRef) || (mode == Mode::Rf && !m.hasRf)) continue;
      const char* label = mode == Mode::Rf ? (m.rfOn ? "RF:ON" : "RF:OFF") : UiModel::modeName(mode);
      const int w = (int)strlen(label) * kSmallGlyph + 4;
      if (mode == m.mode) {
        g_.drawBox(x, 54, w, 10);
        g_.setDrawColor(0);
      }
      g_.drawStr(x + 2, 63, label);
      g_.setDrawColor(1);
      x += w + 2;
    }

    g_.sendBuffer();
  }

 private:
  OledDriver g_;
};

}  // namespace

std::unique_ptr<Display> makeOledDisplay(uint8_t address) {
  std::unique_ptr<OledDisplay> d(new OledDisplay(address));
  if (!d->begin()) return nullptr;
  return d;
}
