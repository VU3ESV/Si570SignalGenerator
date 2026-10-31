#include "ui_model.h"

const uint32_t UiModel::kSteps[UiModel::kStepCount] = {1,      10,      100,      1000,     10000,
                                                       100000, 1000000, 10000000, 100000000};

namespace {
constexpr uint8_t kModeCount = 5;
}

void UiModel::configure(const UiConfig& cfg) {
  cfg_ = cfg;
  for (uint8_t i = 0; i < kMaxOutputs; ++i) outputs_[i].freqHz = clampFrequency(i, outputs_[i].freqHz);
  if (!cfg_.outputs[selected_].fitted) {
    for (uint8_t i = 0; i < kMaxOutputs; ++i)
      if (cfg_.outputs[i].fitted) {
        selected_ = i;
        break;
      }
  }
  clampStep();
  if (!cfg_.hasRef) ref_ = RefMode::Internal;
  if (!available(mode_)) mode_ = Mode::Tune;
}

void UiModel::restore(const OutputState (&outputs)[kMaxOutputs], uint8_t selected, uint8_t stepIndex, RefMode ref) {
  for (uint8_t i = 0; i < kMaxOutputs; ++i) {
    outputs_[i].freqHz = clampFrequency(i, outputs[i].freqHz);
    outputs_[i].rfOn = outputs[i].rfOn;
  }
  if (selected < kMaxOutputs && cfg_.outputs[selected].fitted) selected_ = selected;
  stepIdx_ = stepIndex;
  clampStep();
  ref_ = cfg_.hasRef ? ref : RefMode::Internal;
  mode_ = Mode::Tune;
}

bool UiModel::setFrequency(uint8_t output, uint32_t hz) {
  if (output >= kMaxOutputs) return false;
  const uint32_t f = clampFrequency(output, hz);
  if (f == outputs_[output].freqHz) return false;
  outputs_[output].freqHz = f;
  return true;
}

bool UiModel::setRfOn(uint8_t output, bool on) {
  if (output >= kMaxOutputs || on == outputs_[output].rfOn) return false;
  outputs_[output].rfOn = on;
  return true;
}

bool UiModel::select(uint8_t output) {
  if (output >= kMaxOutputs || !cfg_.outputs[output].fitted || output == selected_) return false;
  selected_ = output;
  clampStep();
  if (!available(mode_)) mode_ = Mode::Tune;
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

UiAction UiModel::rotate(int clicks, uint32_t nowMs) {
  if (clicks == 0) return UiAction::None;
  lastInputMs_ = nowMs;
  OutputState& out = outputs_[selected_];
  switch (mode_) {
    case Mode::Tune: {
      const uint32_t f = clampFrequency(selected_, (int64_t)out.freqHz + (int64_t)clicks * step());
      if (f == out.freqHz) return UiAction::None;
      out.freqHz = f;
      return UiAction::FrequencyChanged;
    }
    case Mode::Step: {
      const int idx = (int)stepIdx_ + clicks;
      stepIdx_ = idx < 0 ? 0 : (idx > maxStepIndex() ? maxStepIndex() : (uint8_t)idx);
      return UiAction::None;
    }
    case Mode::Out: {
      // One fitted output per click, wrapping round.
      uint8_t next = selected_;
      const int dir = clicks > 0 ? 1 : -1;
      for (int n = clicks > 0 ? clicks : -clicks; n > 0; --n) {
        do {
          next = (uint8_t)((next + kMaxOutputs + dir) % kMaxOutputs);
        } while (!cfg_.outputs[next].fitted);
      }
      return select(next) ? UiAction::OutputChanged : UiAction::None;
    }
    case Mode::Ref: {
      const RefMode want = clicks > 0 ? RefMode::External : RefMode::Internal;
      if (want == ref_) return UiAction::None;
      ref_ = want;
      return UiAction::RefModeChanged;
    }
    case Mode::Rf: {
      const bool want = clicks > 0;
      if (want == out.rfOn) return UiAction::None;
      out.rfOn = want;
      return UiAction::RfChanged;
    }
  }
  return UiAction::None;
}

UiAction UiModel::shortPress(uint32_t nowMs) {
  lastInputMs_ = nowMs;
  Mode next = mode_;
  do {
    next = (Mode)(((uint8_t)next + 1) % kModeCount);
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
    case Mode::Out: return "OUT";
    case Mode::Ref: return "REF";
    case Mode::Rf: return "RF";
  }
  return "?";
}

uint8_t UiModel::fittedCount() const {
  uint8_t n = 0;
  for (const OutputConfig& o : cfg_.outputs) n += o.fitted ? 1 : 0;
  return n;
}

uint8_t UiModel::maxStepIndex() const {
  const OutputConfig& o = cfg_.outputs[selected_];
  const uint32_t span = o.maxHz > o.minHz ? o.maxHz - o.minHz : 0;
  uint8_t idx = 0;
  while (idx + 1 < kStepCount && kSteps[idx + 1] <= span) ++idx;
  return idx;
}

void UiModel::clampStep() {
  if (stepIdx_ > maxStepIndex()) stepIdx_ = maxStepIndex();
}

uint32_t UiModel::clampFrequency(uint8_t output, int64_t hz) const {
  const OutputConfig& o = cfg_.outputs[output];
  if (hz < (int64_t)o.minHz) return o.minHz;
  if (hz > (int64_t)o.maxHz) return o.maxHz;
  return (uint32_t)hz;
}

bool UiModel::available(Mode m) const {
  switch (m) {
    case Mode::Out: return fittedCount() > 1;
    case Mode::Ref: return cfg_.hasRef && selected_ == cfg_.refOutput;
    case Mode::Rf: return cfg_.outputs[selected_].hasRf;
    default: return true;
  }
}
