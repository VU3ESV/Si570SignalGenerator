// The Si570 plus its OE pin. Every change to the output (frequency, crystal value,
// RF on/off) bumps generation() so the reference loop can discard counts that straddle it.
#pragma once

#include <Si570.h>
#include <stdint.h>

#include "config.h"
#include "settings.h"

class Synth {
 public:
  // Part settings for the next begin(). Changing the chip settings drops the current
  // initialisation, so call begin() again afterwards.
  void configure(const HardwareConfig& hw);
  bool begin();
  bool ready() const { return ready_; }

  bool setFrequency(uint32_t hz);
  uint32_t frequency() const { return freqHz_; }

  // Crystal frequency used for the RFREQ maths (factory value, or one measured against
  // an external reference).
  bool setXtal(uint32_t hz);
  uint32_t xtal() const { return si570_.getXtalFrequency(); }
  uint32_t factoryXtal() const { return factoryXtal_; }

  // False if the crystal frequency derived at start-up is outside the datasheet's
  // 114.285 MHz ±2000 ppm, i.e. the configured start-up frequency doesn't match the part.
  bool xtalPlausible() const { return xtalPlausible_; }
  uint32_t likelyStartupHz() const;

  uint8_t address() const { return address_; }

  bool hasRfControl() const { return cfg::kPinSi570Oe >= 0; }
  void setOeActiveHigh(bool activeHigh);
  void setRfEnabled(bool on);
  bool rfEnabled() const { return rfOn_; }

  uint32_t generation() const { return generation_; }
  uint32_t lastChangeMs() const { return lastChangeMs_; }

 private:
  bool check(Si570_Status s);
  void changed();

  Si570 si570_{cfg::kSi570Address, cfg::kSi570StartupHz, cfg::kSi570RegBase};
  uint8_t address_ = cfg::kSi570Address;
  uint32_t startupHz_ = cfg::kSi570StartupHz;
  bool oeActiveHigh_ = cfg::kSi570OeActiveHigh;
  bool ready_ = false;
  bool xtalPlausible_ = true;
  bool rfOn_ = true;
  uint32_t freqHz_ = 0;
  uint32_t factoryXtal_ = 0;
  uint32_t derivedXtal_ = 0;
  uint32_t generation_ = 0;
  uint32_t lastChangeMs_ = 0;
};
