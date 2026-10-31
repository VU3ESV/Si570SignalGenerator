// The one place that changes the generator. The encoder, the web page and the serial
// console all go through it, so every interface always shows the same state.
//
// Outputs: 0 = Si570, 1-3 = Si5351 CLK0-CLK2 (when the Si5351 is enabled). The encoder
// and the display work on the selected output; the web page addresses outputs directly.
#pragma once

#include <fll.h>
#include <stdint.h>
#include <ui_model.h>

#include "clockgen.h"
#include "display.h"
#include "ratio_counter.h"
#include "ref_controller.h"
#include "settings.h"
#include "synth.h"

enum class CmdResult : uint8_t { Ok, OutOfRange, NotReady, Unsupported, Busy, PllInUse, Failed };
const char* cmdResultText(CmdResult r);

class Controller {
 public:
  void begin(const HardwareConfig& hw, const StoredSettings& stored);
  void loop(uint32_t nowMs);

  // Encoder
  void encoderClicks(int clicks, uint32_t nowMs);
  void encoderShortPress(uint32_t nowMs);
  void encoderLongPress(uint32_t nowMs);

  // Direct commands (web page, serial console)
  CmdResult setFrequency(uint8_t output, uint32_t hz);
  CmdResult setRfEnabled(uint8_t output, bool on);
  CmdResult setDrive(uint8_t output, uint8_t mA);  // Si5351 outputs: 2, 4, 6 or 8 mA
  CmdResult selectOutput(uint8_t output);
  CmdResult setStepIndex(int index);
  CmdResult setRefMode(RefMode mode);
  CmdResult calibrate();
  CmdResult saveState();
  CmdResult applyHardware(const HardwareConfig& hw);  // validated by the caller

  // Shown on the display and the web page for durationMs.
  void showMessage(uint32_t durationMs, const char* fmt, ...);

  const UiModel& ui() const { return ui_; }
  const Synth& synth() const { return synth_; }
  const ClockGen& clock() const { return clock_; }
  const RefController& ref() const { return ref_; }
  const HardwareConfig& hardware() const { return hw_; }
  bool hasRef() const { return ref_.available(); }
  bool hasEncoder() const { return cfg::kPinEncA >= 0; }

  static const char* outputName(uint8_t output);  // "Si570", "CLK0", ...
  bool outputReady(uint8_t output) const;
  uint8_t driveMa(uint8_t output) const { return driveMa_[output]; }

  // Transient message, else a standing condition for the selected output
  // ("Si5351 not found"), else "".
  const char* statusMessage() const;
  DisplayModel displayModel() const;

 private:
  static bool isClock(uint8_t output) { return output != kSi570Output && output < kMaxOutputs; }
  static uint8_t clockIndex(uint8_t output) { return output - 1; }

  UiConfig uiConfig() const;
  void startSynth(uint32_t nowMs);
  void startClock(uint32_t nowMs);
  bool applyFrequency(uint8_t output);  // false if the hardware refused it
  void applyRf(uint8_t output);
  void handle(UiAction action, uint32_t nowMs);
  void reportCalibration(uint32_t nowMs);

  Synth synth_;
  ClockGen clock_;
  RatioCounter counter_;
  RefController ref_{synth_, counter_};
  UiModel ui_;
  HardwareConfig hw_;
  uint8_t driveMa_[kMaxOutputs] = {};
  uint32_t lastSynthTryMs_ = 0;
  uint32_t lastClockTryMs_ = 0;
  char message_[sizeof(DisplayModel::message)] = "";
  uint32_t messageUntilMs_ = 0;
};
