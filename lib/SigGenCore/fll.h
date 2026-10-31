// External 10 MHz reference: frequency-locked loop maths.
//
// The Si570 has no reference input - it always runs from its internal ~114.285 MHz
// crystal. To lock it to a 10 MHz standard, the ESP32 counts the Si570 output and the
// reference over the same hardware-gated window. Their ratio gives the true output
// frequency (independent of the ESP32's own clock), and from that the true crystal
// frequency, which is fed back into the driver's RFREQ calculation.
#pragma once

#include <stdint.h>

enum class RefStatus : uint8_t {
  Off,          // internal reference selected
  NoRef,        // no (or wrong-frequency) reference signal
  NoSignal,     // reference OK but the Si570 sample input is missing / implausible
  Tracking,     // correcting
  Locked,       // within lockPpm of the reference
  Hold,         // output too high to count (or RF off): keeping the last correction
  Calibrating,  // one-shot calibration in progress
};

struct FllConfig {
  uint32_t refHz = 10000000;
  double refTolerancePpm = 500;     // reference vs the ESP32's own crystal (presence check)
  double signalTolerancePpm = 1000; // Si570 output vs its programmed frequency
  double lockPpm = 0.5;
  double coarsePpm = 2.0;           // errors above this are corrected in one step...
  double fineGain = 0.5;            // ...smaller ones only partly, to average out count jitter
};

struct FllSample {
  int64_t refCount;  // reference edges in the gate
  int64_t sigCount;  // Si570 edges in the same gate
  int64_t gateUs;    // gate length by the ESP32 clock (only used for the presence check)
};

struct FllResult {
  RefStatus status;
  double errorPpm;  // Si570 output vs programmed frequency, + = high
  uint32_t xtalHz;  // corrected crystal estimate (unchanged unless Tracking / Locked)
};

bool referencePresent(const FllConfig& cfg, const FllSample& s);
FllResult fllEvaluate(const FllConfig& cfg, const FllSample& s, uint32_t programmedHz, uint32_t xtalHz);

// Four characters max, for the 16x2 LCD.
const char* refStatusLabel(RefStatus s);

// hz * (1 + ppb / 1e9), rounded: a crystal frequency with a correction in parts per
// billion applied (+ = the crystal runs fast).
uint32_t applyPpb(uint32_t hz, int32_t ppb);

// The correction that makes an output read right: it was set to setHz with the crystal
// corrected by oldPpb, and a trusted counter measured measuredHz. Used with the internal
// reference; the same formula as the web page's Si5351 helper.
int32_t correctionFromMeasurement(uint32_t setHz, double measuredHz, int32_t oldPpb);
