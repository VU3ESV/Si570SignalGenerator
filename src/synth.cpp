#include "synth.h"

#include <Arduino.h>
#include <Wire.h>

namespace {
constexpr uint32_t kNominalXtalHz = 114285000;
constexpr uint32_t kXtalTolerancePpm = 2000;  // datasheet: "maximum variation is ±2000 ppm"
}  // namespace

void Synth::configure(const HardwareConfig& hw) {
  si570_ = Si570(hw.si570Address, hw.si570StartupHz, hw.si570RegBase);
  address_ = hw.si570Address;
  startupHz_ = hw.si570StartupHz;
  oeActiveHigh_ = hw.oeActiveHigh;
  ready_ = false;
}

bool Synth::begin() {
  if (hasRfControl()) {
    pinMode(cfg::kPinSi570Oe, OUTPUT);
    setRfEnabled(rfOn_);
  }

  // Probe first: the driver's init logs four lines per attempt, and the main loop
  // retries every 2 s while no Si570 is connected.
  Wire.beginTransmission(address_);
  if (Wire.endTransmission() != 0) {
    ready_ = false;
    return false;
  }

  ready_ = si570_.begin() == SI570_READY;
  if (!ready_) return false;

  freqHz_ = si570_.getFrequency();
  derivedXtal_ = si570_.getFactoryXtalFrequency();
  const uint32_t diff = derivedXtal_ > kNominalXtalHz ? derivedXtal_ - kNominalXtalHz : kNominalXtalHz - derivedXtal_;
  xtalPlausible_ = (uint64_t)diff * 1000000ULL / kNominalXtalHz <= kXtalTolerancePpm;
  if (!xtalPlausible_) {
    // Better to be within ±2000 ppm than off by the ratio of two start-up frequencies.
    Serial.printf("Si570: derived crystal %lu Hz is implausible. The part probably starts at %lu Hz;\n"
                  "       set that as the start-up frequency. Using nominal %lu Hz for now.\n",
                  (unsigned long)derivedXtal_, (unsigned long)likelyStartupHz(), (unsigned long)kNominalXtalHz);
    check(si570_.setXtalFrequency(kNominalXtalHz));
  }
  factoryXtal_ = si570_.getXtalFrequency();
  changed();
  return ready_;
}

uint32_t Synth::likelyStartupHz() const {
  // derived = startupCfg * HS * N1 / RFREQ, while the real start-up frequency is
  // nominal * RFREQ / (HS * N1); so real ≈ startupCfg * nominal / derived.
  if (derivedXtal_ == 0) return 0;
  return (uint32_t)((uint64_t)startupHz_ * kNominalXtalHz / derivedXtal_);
}

bool Synth::setFrequency(uint32_t hz) {
  if (!ready_) return false;
  if (!check(si570_.setFrequency(hz))) return false;
  if (hz != freqHz_) {
    freqHz_ = hz;
    changed();
  }
  return true;
}

bool Synth::setXtal(uint32_t hz) {
  if (!ready_) return false;
  if (hz == si570_.getXtalFrequency()) return true;
  if (!check(si570_.setXtalFrequency(hz))) return false;
  changed();
  return true;
}

void Synth::setRfEnabled(bool on) {
  rfOn_ = on;
  if (!hasRfControl()) return;
  digitalWrite(cfg::kPinSi570Oe, on == oeActiveHigh_ ? HIGH : LOW);
  changed();
}

void Synth::setOeActiveHigh(bool activeHigh) {
  oeActiveHigh_ = activeHigh;
  setRfEnabled(rfOn_);
}

bool Synth::check(Si570_Status s) {
  if (s == SI570_READY) return true;
  // The driver zeroes its frequency when an I2C write failed, as opposed to a
  // frequency it simply can't reach. Lost chip: let the main loop re-initialise it.
  if (si570_.getFrequency() == 0) {
    ready_ = false;
    Serial.println("Si570: I2C write failed");
  }
  return false;
}

void Synth::changed() {
  ++generation_;
  lastChangeMs_ = millis();
}
