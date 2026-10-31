#include "controller.h"

#include <Arduino.h>
#include <stdarg.h>

namespace {
constexpr uint32_t kSynthRetryMs = 2000;
constexpr uint32_t kMessageMs = 2500;
}  // namespace

const char* cmdResultText(CmdResult r) {
  switch (r) {
    case CmdResult::Ok: return "ok";
    case CmdResult::OutOfRange: return "frequency out of range";
    case CmdResult::NotReady: return "Si570 not ready";
    case CmdResult::Unsupported: return "not supported by this hardware";
    case CmdResult::Busy: return "busy calibrating";
    case CmdResult::Failed: return "failed";
  }
  return "?";
}

void Controller::begin(const HardwareConfig& hw, const StoredSettings& stored) {
  hw_ = hw;
  synth_.configure(hw_);

  bool hasRef = false;
  if (cfg::kPinRefIn >= 0) {
    hasRef = counter_.begin(cfg::kPinRefIn, cfg::kPinSi570Sense, cfg::kPinCountGate);
    if (!hasRef) Serial.println("External reference disabled: pulse counter setup failed");
  }
  ref_.begin(hasRef, stored.extXtalHz, hw_.refHz);

  ui_.configure(uiConfig());
  ui_.restore(stored.freqHz, stored.stepIndex, stored.ref, stored.rfOn);

  startSynth(millis());
  if (!synth_.ready()) Serial.printf("Si570 not found at 0x%02X, retrying\n", synth_.address());
}

void Controller::loop(uint32_t nowMs) {
  if (!synth_.ready() && nowMs - lastSynthTryMs_ >= kSynthRetryMs) startSynth(nowMs);
  ui_.tick(nowMs);
  ref_.service(nowMs);
  reportCalibration(nowMs);
}

// ---------------------------------------------------------------- encoder

void Controller::encoderClicks(int clicks, uint32_t nowMs) {
  if (ref_.calibrating()) return;  // the output is parked on the calibration frequency
  handle(ui_.rotate(clicks, nowMs), nowMs);
}

void Controller::encoderShortPress(uint32_t nowMs) {
  if (!ref_.calibrating()) handle(ui_.shortPress(nowMs), nowMs);
}

void Controller::encoderLongPress(uint32_t nowMs) {
  if (!ref_.calibrating()) handle(ui_.longPress(nowMs), nowMs);
}

void Controller::handle(UiAction action, uint32_t nowMs) {
  switch (action) {
    case UiAction::None: break;
    case UiAction::FrequencyChanged: applyFrequency(nowMs); break;
    case UiAction::RefModeChanged:
      ref_.setMode(ui_.ref());
      Serial.printf("Reference: %s\n", ui_.ref() == RefMode::External ? "external" : "internal");
      break;
    case UiAction::RfChanged: synth_.setRfEnabled(ui_.rfOn()); break;
    case UiAction::Save: saveState(); break;
    case UiAction::Calibrate: calibrate(); break;
  }
}

// ---------------------------------------------------------------- commands

CmdResult Controller::setFrequency(uint32_t hz) {
  if (ref_.calibrating()) return CmdResult::Busy;
  if (hz < hw_.minHz || hz > hw_.maxHz) return CmdResult::OutOfRange;
  ui_.setFrequency(hz);
  if (!synth_.ready()) return CmdResult::NotReady;  // applied once the Si570 answers
  if (synth_.setFrequency(hz)) return CmdResult::Ok;
  ui_.setFrequency(synth_.frequency());
  return synth_.ready() ? CmdResult::OutOfRange : CmdResult::NotReady;
}

CmdResult Controller::setStepIndex(int index) {
  ui_.setStepIndex(index);
  return CmdResult::Ok;
}

CmdResult Controller::setRefMode(RefMode mode) {
  if (ref_.calibrating()) return CmdResult::Busy;
  if (mode == RefMode::External && !hasRef()) return CmdResult::Unsupported;
  if (ui_.setRef(mode)) handle(UiAction::RefModeChanged, millis());
  return CmdResult::Ok;
}

CmdResult Controller::setRfEnabled(bool on) {
  if (ref_.calibrating()) return CmdResult::Busy;
  if (!hasRf()) return CmdResult::Unsupported;
  if (ui_.setRfOn(on)) synth_.setRfEnabled(on);
  return CmdResult::Ok;
}

CmdResult Controller::calibrate() {
  if (ref_.calibrating()) return CmdResult::Busy;
  if (!hasRef() || ui_.ref() != RefMode::External) return CmdResult::Unsupported;
  if (!synth_.ready()) return CmdResult::NotReady;
  if (!ref_.startCalibration()) {
    showMessage(kMessageMs, synth_.rfEnabled() ? "Can't calibrate" : "RF is off");
    return CmdResult::Failed;
  }
  Serial.println("Calibrating against external reference");
  return CmdResult::Ok;
}

CmdResult Controller::saveState() {
  StoredSettings s;
  s.freqHz = ui_.frequency();
  s.stepIndex = ui_.stepIndex();
  s.ref = ui_.ref();
  s.rfOn = ui_.rfOn();
  s.extXtalHz = ref_.extXtal();
  saveSettings(s);
  showMessage(kMessageMs, "Settings saved");
  return CmdResult::Ok;
}

CmdResult Controller::applyHardware(const HardwareConfig& hw) {
  if (ref_.calibrating()) return CmdResult::Busy;
  const bool sameChip = hw_.sameChip(hw);
  hw_ = hw;
  saveHardwareConfig(hw_);
  ui_.configure(uiConfig());
  ref_.setRefHz(hw_.refHz);

  if (!sameChip) {
    // A different part (or different part settings): the factory calibration must be
    // re-read and an old external-reference calibration no longer applies.
    ref_.clearExtXtal();
    saveExtXtal(0);
    synth_.configure(hw_);
    startSynth(millis());
    return synth_.ready() ? CmdResult::Ok : CmdResult::NotReady;
  }
  synth_.setOeActiveHigh(hw_.oeActiveHigh);
  applyFrequency(millis());  // the limits may have moved the frequency
  return CmdResult::Ok;
}

// ---------------------------------------------------------------- internals

UiConfig Controller::uiConfig() const {
  UiConfig c;
  c.minHz = hw_.minHz;
  c.maxHz = hw_.maxHz;
  c.hasRef = ref_.available();
  c.hasRf = synth_.hasRfControl();
  c.idleTimeoutMs = cfg::kIdleTimeoutMs;
  return c;
}

void Controller::startSynth(uint32_t nowMs) {
  lastSynthTryMs_ = nowMs;
  if (!synth_.begin()) return;
  Serial.printf("Si570 ready at 0x%02X, factory crystal %lu Hz\n", synth_.address(),
                (unsigned long)synth_.factoryXtal());
  if (!synth_.xtalPlausible()) showMessage(6000, "Chk start freq cfg");
  synth_.setRfEnabled(ui_.rfOn());
  ref_.setMode(ui_.ref());
  applyFrequency(nowMs);
}

void Controller::applyFrequency(uint32_t nowMs) {
  if (!synth_.ready()) return;
  if (synth_.setFrequency(ui_.frequency())) return;
  ui_.setFrequency(synth_.frequency());
  showMessage(kMessageMs, synth_.ready() ? "Can't tune there" : "Si570 I2C error");
}

void Controller::reportCalibration(uint32_t nowMs) {
  switch (ref_.takeCalibrationResult()) {
    case RefController::CalResult::None:
      break;
    case RefController::CalResult::Ok: {
      saveExtXtal(ref_.extXtal());
      const double ppm = ((double)ref_.extXtal() / synth_.factoryXtal() - 1.0) * 1e6;
      showMessage(4000, "Cal OK %+.2fppm", ppm);
      break;
    }
    case RefController::CalResult::Failed:
      switch (ref_.lastCalibrationStatus()) {
        case RefStatus::NoRef: showMessage(4000, "Cal fail: no ref"); break;
        case RefStatus::NoSignal: showMessage(4000, "Cal fail: no sig"); break;
        default: showMessage(4000, "Cal fail: unlock"); break;
      }
      break;
  }
}

void Controller::showMessage(uint32_t durationMs, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vsnprintf(message_, sizeof message_, fmt, args);
  va_end(args);
  messageUntilMs_ = millis() + durationMs;
  Serial.println(message_);
}

const char* Controller::statusMessage() const {
  if ((int32_t)(messageUntilMs_ - millis()) > 0) return message_;
  if (!synth_.ready()) return "Si570 not found";
  if (ref_.calibrating()) return "Calibrating...";
  return "";
}

DisplayModel Controller::displayModel() const {
  DisplayModel m;
  m.freqHz = ui_.frequency();
  m.stepHz = ui_.step();
  m.mode = ui_.mode();
  m.hasRef = ref_.available();
  m.ref = ui_.ref();
  m.refStatus = ref_.status();
  m.refErrorCentiPpm = (int32_t)lround(ref_.errorPpm() * 100.0);
  m.hasRf = synth_.hasRfControl();
  m.rfOn = ui_.rfOn();
  snprintf(m.message, sizeof m.message, "%s", statusMessage());
  return m;
}
