// The Si5351's CLK0-CLK2, driven by Etherkit's Si5351Arduino (GPL-3.0). Shares the I2C bus
// with the Si570 and the display.
//
// CLK0 and CLK1 share PLLA; CLK2 has PLLB to itself. At or below 100 MHz any mix of
// frequencies works (fractional dividers from an 800 MHz PLL). Above 100 MHz an output
// needs its PLL to itself, so CLK2 and one of CLK0/CLK1 can be above 100 MHz together,
// but not CLK0 and CLK1: the library refuses that and setFrequency() returns false.
#pragma once

#include <stdint.h>

#include <memory>

#include "settings.h"

class Si5351;

class ClockGen {
 public:
  static constexpr uint8_t kClocks = 3;

  ClockGen();
  ~ClockGen();

  // Part settings for the next begin(). Drops the current initialisation.
  void configure(const HardwareConfig& hw);
  bool enabled() const { return enabled_; }

  // Probes the address and initialises the chip with every output off. False if it
  // doesn't answer.
  bool begin();
  bool ready() const { return ready_; }
  // Re-probes the chip; false (and no longer ready) if it stopped answering.
  bool check();

  // False if the chip isn't ready or the frequency can't be made alongside the others.
  bool setFrequency(uint8_t clk, uint32_t hz);
  uint32_t frequency(uint8_t clk) const { return freqHz_[clk]; }
  void setEnabled(uint8_t clk, bool on);
  static bool validDrive(uint8_t mA) { return mA == 2 || mA == 4 || mA == 6 || mA == 8; }
  void setDrive(uint8_t clk, uint8_t mA);  // 2, 4, 6 or 8 mA
  void setCorrection(int32_t ppb);

  uint8_t address() const { return address_; }
  uint32_t xtal() const { return xtalHz_; }
  int32_t correctionPpb() const { return corrPpb_; }

 private:
  std::unique_ptr<Si5351> chip_;
  bool enabled_ = cfg::kSi5351Enabled;
  bool ready_ = false;
  uint8_t address_ = cfg::kSi5351Address;
  uint32_t xtalHz_ = cfg::kSi5351XtalHz;
  int32_t corrPpb_ = 0;
  uint32_t freqHz_[kClocks] = {};  // 0 = not set since begin()
  bool on_[kClocks] = {};
  uint8_t driveMa_[kClocks] = {cfg::kSi5351DefaultDriveMa, cfg::kSi5351DefaultDriveMa,
                               cfg::kSi5351DefaultDriveMa};
};
