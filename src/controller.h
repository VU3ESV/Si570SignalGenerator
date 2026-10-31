// The one place that changes the generator. The encoder, the web page and the serial
// console all go through it, so every interface always shows the same state.
#pragma once

#include <fll.h>
#include <stdint.h>
#include <ui_model.h>

#include "display.h"
#include "ratio_counter.h"
#include "ref_controller.h"
#include "settings.h"
#include "synth.h"

enum class CmdResult : uint8_t { Ok, OutOfRange, NotReady, Unsupported, Busy, Failed };
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
  CmdResult setFrequency(uint32_t hz);
  CmdResult setStepIndex(int index);
  CmdResult setRefMode(RefMode mode);
  CmdResult setRfEnabled(bool on);
  CmdResult calibrate();
  CmdResult saveState();
  CmdResult applyHardware(const HardwareConfig& hw);  // validated by the caller

  // Shown on the display and the web page for durationMs.
  void showMessage(uint32_t durationMs, const char* fmt, ...);

  const UiModel& ui() const { return ui_; }
  const Synth& synth() const { return synth_; }
  const RefController& ref() const { return ref_; }
  const HardwareConfig& hardware() const { return hw_; }
  bool hasRef() const { return ref_.available(); }
  bool hasRf() const { return synth_.hasRfControl(); }
  bool hasEncoder() const { return cfg::kPinEncA >= 0; }

  // Transient message, else a standing condition ("Si570 not found"), else "".
  const char* statusMessage() const;
  DisplayModel displayModel() const;

 private:
  UiConfig uiConfig() const;
  void startSynth(uint32_t nowMs);
  void applyFrequency(uint32_t nowMs);
  void handle(UiAction action, uint32_t nowMs);
  void reportCalibration(uint32_t nowMs);

  Synth synth_;
  RatioCounter counter_;
  RefController ref_{synth_, counter_};
  UiModel ui_;
  HardwareConfig hw_;
  uint32_t lastSynthTryMs_ = 0;
  char message_[sizeof(DisplayModel::message)] = "";
  uint32_t messageUntilMs_ = 0;
};
