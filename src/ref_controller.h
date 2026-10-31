// INT / EXT reference selection.
//
// INT: the Si570 maths uses the factory-calibrated crystal frequency read at start-up.
// EXT: once per gate (1 s) the Si570 output is counted against the 10 MHz reference and
//      the crystal value is corrected (a frequency-locked loop). This works while the
//      output is <= cfg::kTrackMaxHz; above that the last correction is held. A long
//      press in REF mode runs a calibration, briefly moving the output to
//      cfg::kCalibrationHz if needed.
#pragma once

#include <fll.h>
#include <stdint.h>
#include <ui_model.h>

#include "ratio_counter.h"
#include "synth.h"

class RefController {
 public:
  enum class CalResult : uint8_t { None, Ok, Failed };

  RefController(Synth& synth, RatioCounter& counter) : synth_(synth), counter_(counter) {}

  void begin(bool available, uint32_t storedExtXtalHz, uint32_t refHz);
  bool available() const { return available_; }
  void setRefHz(uint32_t hz) { fll_.refHz = hz; }
  void clearExtXtal() { extXtal_ = 0; }  // e.g. a different Si570 was configured

  // Selects which crystal value the Si570 maths uses. Call again after re-initialising the Si570.
  void setMode(RefMode mode);
  void service(uint32_t nowMs);

  bool startCalibration();
  bool calibrating() const { return calibrating_; }
  CalResult takeCalibrationResult();
  RefStatus lastCalibrationStatus() const { return calStatus_; }

  RefStatus status() const;
  double errorPpm() const { return errorPpm_; }
  uint32_t extXtal() const { return extXtal_; }  // 0 = never measured

 private:
  void evaluate(const RatioCounter::Sample& sample, bool stable);
  void finishCalibration(bool ok, RefStatus why);
  void abortGate();

  Synth& synth_;
  RatioCounter& counter_;
  FllConfig fll_;
  bool available_ = false;
  RefMode mode_ = RefMode::Internal;
  RefStatus status_ = RefStatus::Off;
  double errorPpm_ = 0;
  uint32_t extXtal_ = 0;

  bool gateOpen_ = false;
  uint32_t gateStartMs_ = 0;
  uint32_t gateGeneration_ = 0;

  bool calibrating_ = false;
  uint8_t calGates_ = 0;
  uint32_t restoreHz_ = 0;
  CalResult calResult_ = CalResult::None;
  RefStatus calStatus_ = RefStatus::Off;
};
