#include "ui_model.h"

const uint32_t UiModel::kSteps[UiModel::kStepCount] = {1,      10,      100,      1000,     10000,
                                                       100000, 1000000, 10000000, 100000000};

void UiModel::configure(const UiConfig& cfg) {
  cfg_ = cfg;
  freq_ = clampFrequency(freq_);
  if (stepIdx_ > maxStepIndex()) stepIdx_ = maxStepIndex();
  if (!cfg_.hasRef) ref_ = RefMode::Internal;
  if (!available(mode_)) mode_ = Mode::Tune;
}

void UiModel::restore(uint32_t freqHz, uint8_t stepIndex, RefMode ref, bool rfOn) {
  freq_ = clampFrequency(freqHz);
  stepIdx_ = stepIndex > maxStepIndex() ? maxStepIndex() : stepIndex;
  ref_ = cfg_.hasRef ? ref : RefMode::Internal;
  rfOn_ = rfOn;
  mode_ = Mode::Tune;
}

bool UiModel::setFrequency(uint32_t hz) {
  const uint32_t f = clampFrequency(hz);
  if (f == freq_) return false;
  freq_ = f;
  return true;
}

bool UiModel::setStepIndex(int index) {
  const uint8_t idx = index < 0 ? 0 : (index > maxStepIndex() ? maxStepIndex() : (uint8_t)index);
  if (idx == stepIdx_) return false;
  stepIdx_ = idx;
  return true;
}

bool UiModel::setRef(RefMode ref) {
  if (ref == RefMode::External && !cfg_.hasRef) return false;
  if (ref == ref_) return false;
  ref_ = ref;
  return true;
}

bool UiModel::setRfOn(bool on) {
  if (on == rfOn_) return false;
  rfOn_ = on;
  return true;
}

UiAction UiModel::rotate(int clicks, uint32_t nowMs) {
  if (clicks == 0) return UiAction::None;
  lastInputMs_ = nowMs;
  switch (mode_) {
    case Mode::Tune: {
      const uint32_t f = clampFrequency((int64_t)freq_ + (int64_t)clicks * step());
      if (f == freq_) return UiAction::None;
      freq_ = f;
      return UiAction::FrequencyChanged;
    }
    case Mode::Step: {
      const int idx = (int)stepIdx_ + clicks;
      stepIdx_ = idx < 0 ? 0 : (idx > maxStepIndex() ? maxStepIndex() : (uint8_t)idx);
      return UiAction::None;
    }
    case Mode::Ref: {
      const RefMode want = clicks > 0 ? RefMode::External : RefMode::Internal;
      if (want == ref_) return UiAction::None;
      ref_ = want;
      return UiAction::RefModeChanged;
    }
    case Mode::Rf: {
      const bool want = clicks > 0;
      if (want == rfOn_) return UiAction::None;
      rfOn_ = want;
      return UiAction::RfChanged;
    }
  }
  return UiAction::None;
}

UiAction UiModel::shortPress(uint32_t nowMs) {
  lastInputMs_ = nowMs;
  Mode next = mode_;
  do {
    next = (Mode)(((uint8_t)next + 1) % 4);
  } while (!available(next));
  mode_ = next;
  return UiAction::None;
}

UiAction UiModel::longPress(uint32_t nowMs) {
  lastInputMs_ = nowMs;
  if (mode_ == Mode::Ref && ref_ == RefMode::External) return UiAction::Calibrate;
  return UiAction::Save;
}

void UiModel::tick(uint32_t nowMs) {
  if (mode_ != Mode::Tune && cfg_.idleTimeoutMs != 0 && nowMs - lastInputMs_ >= cfg_.idleTimeoutMs)
    mode_ = Mode::Tune;
}

const char* UiModel::modeName(Mode m) {
  switch (m) {
    case Mode::Tune: return "TUNE";
    case Mode::Step: return "STEP";
    case Mode::Ref: return "REF";
    case Mode::Rf: return "RF";
  }
  return "?";
}

uint8_t UiModel::maxStepIndex() const {
  const uint32_t span = cfg_.maxHz > cfg_.minHz ? cfg_.maxHz - cfg_.minHz : 0;
  uint8_t idx = 0;
  while (idx + 1 < kStepCount && kSteps[idx + 1] <= span) ++idx;
  return idx;
}

uint32_t UiModel::clampFrequency(int64_t hz) const {
  if (hz < (int64_t)cfg_.minHz) return cfg_.minHz;
  if (hz > (int64_t)cfg_.maxHz) return cfg_.maxHz;
  return (uint32_t)hz;
}

bool UiModel::available(Mode m) const {
  switch (m) {
    case Mode::Ref: return cfg_.hasRef;
    case Mode::Rf: return cfg_.hasRf;
    default: return true;
  }
}
