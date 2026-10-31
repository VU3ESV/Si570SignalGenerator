#include "clockgen.h"

#include <Wire.h>
#include <si5351.h>

namespace {

si5351_clock clockId(uint8_t clk) { return (si5351_clock)((uint8_t)SI5351_CLK0 + clk); }

uint8_t crystalLoad() {
  switch (cfg::kSi5351CrystalLoadPf) {
    case 6: return SI5351_CRYSTAL_LOAD_6PF;
    case 10: return SI5351_CRYSTAL_LOAD_10PF;
    default: return SI5351_CRYSTAL_LOAD_8PF;
  }
}

si5351_drive driveId(uint8_t mA) {
  switch (mA) {
    case 2: return SI5351_DRIVE_2MA;
    case 4: return SI5351_DRIVE_4MA;
    case 6: return SI5351_DRIVE_6MA;
    default: return SI5351_DRIVE_8MA;
  }
}

}  // namespace

ClockGen::ClockGen() = default;
ClockGen::~ClockGen() = default;

void ClockGen::configure(const HardwareConfig& hw) {
  chip_.reset();
  ready_ = false;
  enabled_ = hw.s5Enabled;
  address_ = hw.s5Address;
  xtalHz_ = hw.s5XtalHz;
  corrPpb_ = hw.s5CorrPpb;
}

bool ClockGen::begin() {
  ready_ = false;
  if (!enabled_) return false;
  // Probe first: init() would otherwise run its set-up writes against an empty address.
  Wire.beginTransmission(address_);
  if (Wire.endTransmission() != 0) return false;

  chip_.reset(new Si5351(address_));
  if (!chip_->init(crystalLoad(), xtalHz_, corrPpb_)) return false;  // also turns every output off
  chip_->set_ms_source(SI5351_CLK2, SI5351_PLLB);  // see clockgen.h
  for (uint8_t i = 0; i < kClocks; ++i) {
    freqHz_[i] = 0;
    chip_->drive_strength(clockId(i), driveId(driveMa_[i]));
  }
  ready_ = true;
  return true;
}

bool ClockGen::check() {
  if (!ready_) return false;
  Wire.beginTransmission(address_);
  if (Wire.endTransmission() == 0) return true;
  ready_ = false;
  return false;
}

bool ClockGen::setFrequency(uint8_t clk, uint32_t hz) {
  if (!ready_ || clk >= kClocks) return false;
  if (chip_->set_freq((uint64_t)hz * SI5351_FREQ_MULT, clockId(clk)) != 0) return false;
  freqHz_[clk] = hz;
  // set_freq() switches an output on the first time it is set; keep the RF state instead.
  chip_->output_enable(clockId(clk), on_[clk] ? 1 : 0);
  return true;
}

void ClockGen::setEnabled(uint8_t clk, bool on) {
  if (clk >= kClocks) return;
  on_[clk] = on;
  // An output that has never had a frequency stays off: it would run on stale settings.
  if (ready_ && freqHz_[clk] != 0) chip_->output_enable(clockId(clk), on ? 1 : 0);
}

void ClockGen::setDrive(uint8_t clk, uint8_t mA) {
  if (clk >= kClocks || !validDrive(mA)) return;
  driveMa_[clk] = mA;
  if (ready_) chip_->drive_strength(clockId(clk), driveId(mA));
}

void ClockGen::setCorrection(int32_t ppb) {
  corrPpb_ = ppb;
  // Re-plans both PLLs with the new crystal value; the output dividers stay as they are.
  if (ready_) chip_->set_correction(ppb, SI5351_PLL_INPUT_XO);
}
