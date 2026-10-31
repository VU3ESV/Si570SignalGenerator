// User-interface state machine: what the encoder and its push switch do in each mode.
//
//   rotate        TUNE: frequency += clicks * step   STEP: change step
//                 REF:  CW = external, CCW = internal RF: CW = on, CCW = off
//   short press   next mode (REF / RF only appear when the hardware supports them)
//   long press    REF mode with external reference selected: calibrate now
//                 any other mode: save settings as the power-on defaults
//   idle          back to TUNE after idleTimeoutMs without input
//
// The web page and serial console use the direct setters instead.
#pragma once

#include <stdint.h>

enum class Mode : uint8_t { Tune, Step, Ref, Rf };
enum class RefMode : uint8_t { Internal, External };
enum class UiAction : uint8_t { None, FrequencyChanged, RefModeChanged, RfChanged, Save, Calibrate };

struct UiConfig {
  uint32_t minHz = 10000000;
  uint32_t maxHz = 160000000;
  bool hasRef = false;
  bool hasRf = false;
  uint32_t idleTimeoutMs = 10000;  // 0 disables
};

class UiModel {
 public:
  static constexpr uint8_t kStepCount = 9;
  static const uint32_t kSteps[kStepCount];  // 1 Hz ... 100 MHz

  // Also re-clamps the current state, e.g. when the frequency limits change.
  void configure(const UiConfig& cfg);
  void restore(uint32_t freqHz, uint8_t stepIndex, RefMode ref, bool rfOn);

  UiAction rotate(int clicks, uint32_t nowMs);
  UiAction shortPress(uint32_t nowMs);
  UiAction longPress(uint32_t nowMs);
  void tick(uint32_t nowMs);

  // Direct setters. Each returns true if the value changed.
  bool setFrequency(uint32_t hz);  // clamped to the configured range
  bool setStepIndex(int index);    // clamped to the usable steps
  bool setRef(RefMode ref);        // External is refused without reference hardware
  bool setRfOn(bool on);

  uint32_t frequency() const { return freq_; }
  uint32_t step() const { return kSteps[stepIdx_]; }
  uint8_t stepIndex() const { return stepIdx_; }
  Mode mode() const { return mode_; }
  RefMode ref() const { return ref_; }
  bool rfOn() const { return rfOn_; }
  const UiConfig& config() const { return cfg_; }
  uint8_t maxStepIndex() const;  // largest step that fits in the frequency range

  static const char* modeName(Mode m);

 private:
  uint32_t clampFrequency(int64_t hz) const;
  bool available(Mode m) const;

  UiConfig cfg_;
  uint32_t freq_ = 10000000;
  uint8_t stepIdx_ = 3;
  RefMode ref_ = RefMode::Internal;
  bool rfOn_ = true;
  Mode mode_ = Mode::Tune;
  uint32_t lastInputMs_ = 0;
};
