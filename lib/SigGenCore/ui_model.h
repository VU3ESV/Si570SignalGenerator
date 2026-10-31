// User-interface state machine: what the encoder and its push switch do in each mode.
//
// The generator has up to kMaxOutputs outputs (here: 0 = Si570, 1-3 = Si5351 CLK0-CLK2).
// One output is selected; TUNE, STEP and RF act on it, and the display shows it.
//
//   rotate        TUNE: frequency += clicks * step   STEP: change step
//                 OUT:  select the next / previous output
//                 REF:  CW = external, CCW = internal RF: CW = on, CCW = off
//   short press   next mode (OUT / REF / RF only appear where they apply)
//   long press    REF mode with external reference selected: calibrate now
//                 any other mode: save settings as the power-on defaults
//   idle          back to TUNE after idleTimeoutMs without input
//
// The web page and serial console use the direct setters instead.
#pragma once

#include <stdint.h>

enum class Mode : uint8_t { Tune, Step, Out, Ref, Rf };
enum class RefMode : uint8_t { Internal, External };
enum class UiAction : uint8_t { None, FrequencyChanged, OutputChanged, RefModeChanged, RfChanged, Save, Calibrate };

constexpr uint8_t kMaxOutputs = 4;

struct OutputConfig {
  bool fitted = false;  // part of the generator: shown and selectable
  uint32_t minHz = 10000000;
  uint32_t maxHz = 160000000;
  bool hasRf = false;  // RF on/off control
};

struct UiConfig {
  OutputConfig outputs[kMaxOutputs];
  bool hasRef = false;    // external reference hardware
  uint8_t refOutput = 0;  // the output the external reference corrects; REF mode only shows there
  uint32_t idleTimeoutMs = 10000;  // 0 disables
};

struct OutputState {
  uint32_t freqHz = 10000000;
  bool rfOn = true;
};

class UiModel {
 public:
  static constexpr uint8_t kStepCount = 9;
  static const uint32_t kSteps[kStepCount];  // 1 Hz ... 100 MHz

  // Also re-clamps the current state, e.g. when the frequency limits change. At least
  // one output must be fitted.
  void configure(const UiConfig& cfg);
  void restore(const OutputState (&outputs)[kMaxOutputs], uint8_t selected, uint8_t stepIndex, RefMode ref);

  UiAction rotate(int clicks, uint32_t nowMs);
  UiAction shortPress(uint32_t nowMs);
  UiAction longPress(uint32_t nowMs);
  void tick(uint32_t nowMs);

  // Direct setters. Each returns true if the value changed.
  bool setFrequency(uint8_t output, uint32_t hz);  // clamped to that output's range
  bool setRfOn(uint8_t output, bool on);
  bool select(uint8_t output);     // refused for an output that isn't fitted
  bool setStepIndex(int index);    // clamped to the selected output's usable steps
  bool setRef(RefMode ref);        // External is refused without reference hardware

  uint8_t selected() const { return selected_; }
  uint32_t frequency(uint8_t output) const { return outputs_[output].freqHz; }
  uint32_t frequency() const { return outputs_[selected_].freqHz; }
  bool rfOn(uint8_t output) const { return outputs_[output].rfOn; }
  bool rfOn() const { return outputs_[selected_].rfOn; }
  uint32_t step() const { return kSteps[stepIdx_]; }
  uint8_t stepIndex() const { return stepIdx_; }
  Mode mode() const { return mode_; }
  RefMode ref() const { return ref_; }
  const UiConfig& config() const { return cfg_; }
  const OutputConfig& output(uint8_t output) const { return cfg_.outputs[output]; }
  uint8_t fittedCount() const;
  uint8_t maxStepIndex() const;  // largest step that fits in the selected output's range

  static const char* modeName(Mode m);

 private:
  uint32_t clampFrequency(uint8_t output, int64_t hz) const;
  bool available(Mode m) const;
  void clampStep();

  UiConfig cfg_;
  OutputState outputs_[kMaxOutputs];
  uint8_t selected_ = 0;
  uint8_t stepIdx_ = 3;
  RefMode ref_ = RefMode::Internal;
  Mode mode_ = Mode::Tune;
  uint32_t lastInputMs_ = 0;
};
