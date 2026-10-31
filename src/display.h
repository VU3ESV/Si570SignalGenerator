// Display abstraction: 16x2 HD44780 LCD on a PCF8574 I2C backpack, or a 128x64
// SSD1306/SH1106 OLED, picked at boot by probing the I2C bus (see cfg::kDisplay).
#pragma once

#include <fll.h>
#include <stdint.h>
#include <ui_model.h>

#include <memory>

struct DisplayModel {
  uint32_t freqHz = 0;
  uint32_t stepHz = 0;
  Mode mode = Mode::Tune;
  bool hasRef = false;
  RefMode ref = RefMode::Internal;
  RefStatus refStatus = RefStatus::Off;
  int32_t refErrorCentiPpm = 0;  // hundredths of a ppm, so tiny changes don't force a redraw
  bool hasRf = false;
  bool rfOn = true;
  char message[22] = "";  // replaces the bottom status line while non-empty

  bool operator==(const DisplayModel& o) const;
  bool operator!=(const DisplayModel& o) const { return !(*this == o); }
};

class Display {
 public:
  virtual ~Display() = default;
  virtual const char* name() const = 0;
  virtual void showSplash(const char* line1, const char* line2) = 0;
  virtual void render(const DisplayModel& m) = 0;
  // The Si570 shares the bus, so the slowest device sets the clock.
  virtual uint32_t i2cClockHz() const { return 400000; }
};

// Never returns null: falls back to a headless display (serial log only).
std::unique_ptr<Display> createDisplay();

// Return null if the device doesn't initialise.
std::unique_ptr<Display> makeLcdDisplay(uint8_t address);
std::unique_ptr<Display> makeOledDisplay(uint8_t address);
