// 16x2 character LCD layout
//
//   row 0   " 14.250 000 MHz"   cursor under the digit being tuned (blinks in STEP mode)
//   row 1   "TUNE 1kHz    INT"  mode, step, reference status
//           "STEP <1kHz>"       "REF  <EXT> LOCK"   "RF   <ON>"
#include <Wire.h>  // hd44780 needs Wire declared before its headers
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>
#include <stdio.h>
#include <string.h>

#include <freq_format.h>

#include "display.h"

namespace {

constexpr int kCols = 16;
constexpr int kFreqWidth = 12;  // "1417.500 000"; " MHz" follows

const char* refShort(const DisplayModel& m) {
  if (!m.hasRef) return "";
  return m.ref == RefMode::Internal ? "INT" : refStatusLabel(m.refStatus);
}

class LcdDisplay : public Display {
 public:
  explicit LcdDisplay(uint8_t address) : lcd_(address) {}

  bool begin() {
    if (lcd_.begin(kCols, 2) != 0) return false;
    forget();
    return true;
  }

  const char* name() const override { return "16x2 LCD"; }
  uint32_t i2cClockHz() const override { return 100000; }  // PCF8574 is a 100 kHz part

  void showSplash(const char* line1, const char* line2) override {
    lcd_.noCursor();
    lcd_.noBlink();
    writeRow(0, line1);
    writeRow(1, line2);
  }

  void render(const DisplayModel& m) override {
    char freq[16];
    const size_t len = formatFrequency(freq, sizeof freq, m.freqHz);
    char row[kCols + 1];
    snprintf(row, sizeof row, "%*.12s MHz", kFreqWidth, freq);
    writeRow(0, row);

    if (m.message[0] != '\0') {
      snprintf(row, sizeof row, "%.16s", m.message);
    } else {
      switch (m.mode) {
        case Mode::Tune: snprintf(row, sizeof row, "TUNE %-6s %4s", stepLabel(m.stepHz), refShort(m)); break;
        case Mode::Step: snprintf(row, sizeof row, "STEP <%s>", stepLabel(m.stepHz)); break;
        case Mode::Ref:
          snprintf(row, sizeof row, "REF  <%s> %s", m.ref == RefMode::External ? "EXT" : "INT",
                   m.ref == RefMode::External ? refStatusLabel(m.refStatus) : "");
          break;
        case Mode::Rf: snprintf(row, sizeof row, "RF   <%s>", m.rfOn ? "ON" : "OFF"); break;
      }
    }
    writeRow(1, row);

    const int idx = digitIndexForDecade(freq, len, decadeOf(m.stepHz));
    const bool showCursor = (m.mode == Mode::Tune || m.mode == Mode::Step) && idx >= 0;
    if (showCursor) {
      lcd_.setCursor(kFreqWidth - (int)len + idx, 0);
      lcd_.cursor();
      if (m.mode == Mode::Step) lcd_.blink(); else lcd_.noBlink();
    } else {
      lcd_.noCursor();
      lcd_.noBlink();
    }
  }

 private:
  // Only sends the characters that changed: the PCF8574 runs at 100 kHz and shares
  // the bus with the Si570.
  void writeRow(uint8_t r, const char* text) {
    char padded[kCols + 1];
    snprintf(padded, sizeof padded, "%-16.16s", text);
    for (int c = 0; c < kCols; ++c) {
      if (shadow_[r][c] == padded[c]) continue;
      lcd_.setCursor(c, r);
      int end = c;
      while (end < kCols && shadow_[r][end] != padded[end]) {
        lcd_.write(padded[end]);
        shadow_[r][end] = padded[end];
        ++end;
      }
      c = end;
    }
  }

  void forget() { memset(shadow_, 0, sizeof shadow_); }

  hd44780_I2Cexp lcd_;
  char shadow_[2][kCols];
};

}  // namespace

std::unique_ptr<Display> makeLcdDisplay(uint8_t address) {
  std::unique_ptr<LcdDisplay> d(new LcdDisplay(address));
  if (!d->begin()) return nullptr;
  return d;
}
