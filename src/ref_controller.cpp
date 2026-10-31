#include "ref_controller.h"

#include <Arduino.h>

#include "config.h"

namespace {
constexpr uint32_t kSettleMs = 20;    // Si570 large-change settling is <= 10 ms
constexpr uint8_t kMaxCalGates = 8;
}  // namespace

void RefController::begin(bool available, uint32_t storedExtXtalHz, uint32_t refHz) {
  available_ = available;
  extXtal_ = storedExtXtalHz;
  fll_.refHz = refHz;
  fll_.lockPpm = cfg::kLockPpm;
}

void RefController::setMode(RefMode mode) {
  if (!available_) mode = RefMode::Internal;
  if (mode_ == RefMode::External && mode == RefMode::Internal && synth_.ready()) extXtal_ = synth_.xtal();
  mode_ = mode;
  abortGate();
  if (calibrating_) finishCalibration(false, RefStatus::Off);

  if (mode_ == RefMode::Internal) {
    status_ = RefStatus::Off;
    synth_.setXtal(intXtal());
  } else {
    // Start from the last external calibration (kept across power cycles) until the
    // first measurement completes.
    status_ = RefStatus::Hold;
    synth_.setXtal(extXtal_ != 0 ? extXtal_ : intXtal());
  }
}

void RefController::setIntCorrection(int32_t ppb) {
  intCorrPpb_ = ppb;
  if (mode_ == RefMode::Internal) synth_.setXtal(intXtal());  // no-op until the Si570 is ready
}

uint32_t RefController::intXtal() const { return applyPpb(synth_.factoryXtal(), intCorrPpb_); }

void RefController::service(uint32_t nowMs) {
  if (mode_ != RefMode::External || !synth_.ready()) return;

  if (!gateOpen_) {
    if (nowMs - synth_.lastChangeMs() < kSettleMs) return;
    gateGeneration_ = synth_.generation();
    counter_.openGate();
    gateOpen_ = true;
    gateStartMs_ = nowMs;
    return;
  }
  if (nowMs - gateStartMs_ < cfg::kRefGateMs) return;

  counter_.closeGate();
  gateOpen_ = false;
  evaluate(counter_.read(), synth_.generation() == gateGeneration_);
}

void RefController::evaluate(const RatioCounter::Sample& sample, bool stable) {
  const FllSample s{sample.refCount, sample.sigCount, sample.gateUs};

  if (!referencePresent(fll_, s)) {
    status_ = RefStatus::NoRef;
    Serial.printf("EXT ref: no reference (%lld edges in %lld us)\n", (long long)s.refCount, (long long)s.gateUs);
    if (calibrating_) finishCalibration(false, status_);
    return;
  }
  if (!stable) return;  // output changed during the gate
  if (!synth_.rfEnabled() || synth_.frequency() > cfg::kTrackMaxHz) {
    status_ = RefStatus::Hold;
    return;
  }

  const FllResult r = fllEvaluate(fll_, s, synth_.frequency(), synth_.xtal());
  status_ = r.status;
  errorPpm_ = r.errorPpm;
  if (r.status == RefStatus::Tracking || r.status == RefStatus::Locked) {
    synth_.setXtal(r.xtalHz);
    extXtal_ = synth_.xtal();
  }
  Serial.printf("EXT ref: %-4s output %+.3f ppm, xtal %lu Hz\n", refStatusLabel(r.status), r.errorPpm,
                (unsigned long)synth_.xtal());

  if (!calibrating_) return;
  ++calGates_;
  if (r.status == RefStatus::NoSignal)
    finishCalibration(false, r.status);
  else if (r.status == RefStatus::Locked && calGates_ >= 2)
    finishCalibration(true, r.status);
  else if (calGates_ >= kMaxCalGates)
    finishCalibration(false, r.status);
}

bool RefController::startCalibration() {
  if (mode_ != RefMode::External || calibrating_ || !synth_.ready() || !synth_.rfEnabled()) return false;
  restoreHz_ = 0;
  if (synth_.frequency() > cfg::kTrackMaxHz) {
    restoreHz_ = synth_.frequency();
    if (!synth_.setFrequency(cfg::kCalibrationHz)) return false;
  }
  abortGate();
  calibrating_ = true;
  calGates_ = 0;
  return true;
}

RefController::CalResult RefController::takeCalibrationResult() {
  const CalResult r = calResult_;
  calResult_ = CalResult::None;
  return r;
}

RefStatus RefController::status() const { return calibrating_ ? RefStatus::Calibrating : status_; }

void RefController::finishCalibration(bool ok, RefStatus why) {
  calibrating_ = false;
  if (restoreHz_ != 0) synth_.setFrequency(restoreHz_);
  restoreHz_ = 0;
  calResult_ = ok ? CalResult::Ok : CalResult::Failed;
  calStatus_ = why;
}

void RefController::abortGate() {
  if (!gateOpen_) return;
  counter_.closeGate();
  gateOpen_ = false;
}
