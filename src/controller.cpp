#include "controller.h"

#include <Arduino.h>
#include <stdarg.h>

namespace {
constexpr uint32_t kChipRetryMs = 2000;  // re-probe interval for a missing or lost chip
constexpr uint32_t kMessageMs = 2500;
}  // namespace

const char* cmdResultText(CmdResult r) {
  switch (r) {
    case CmdResult::Ok: return "ok";
    case CmdResult::OutOfRange: return "frequency out of range";
    case CmdResult::NotReady: return "the chip is not answering";
    case CmdResult::Unsupported: return "not supported by this hardware";
    case CmdResult::Busy: return "busy calibrating";
    case CmdResult::PllInUse: return "CLK0 and CLK1 share a PLL: only one of them can be above 100 MHz";
    case CmdResult::Failed: return "failed";
  }
  return "?";
}

const char* Controller::outputName(uint8_t output) {
  static const char* const kNames[kMaxOutputs] = {"Si570", "CLK0", "CLK1", "CLK2"};
  return output < kMaxOutputs ? kNames[output] : "?";
}

void Controller::begin(const HardwareConfig& hw, const StoredSettings& stored) {
  hw_ = hw;
  synth_.configure(hw_);
  clock_.configure(hw_);

  bool hasRef = false;
  if (cfg::kPinRefIn >= 0) {
    hasRef = counter_.begin(cfg::kPinRefIn, cfg::kPinSi570Sense, cfg::kPinCountGate);
    if (!hasRef) Serial.println("External reference disabled: pulse counter setup failed");
  }
  ref_.begin(hasRef, stored.extXtalHz, hw_.refHz);
  ref_.setIntCorrection(hw_.si570CorrPpb);

  for (uint8_t i = 0; i < kMaxOutputs; ++i)
    driveMa_[i] = ClockGen::validDrive(stored.driveMa[i]) ? stored.driveMa[i] : cfg::kSi5351DefaultDriveMa;
  ui_.configure(uiConfig());
  ui_.restore(stored.outputs, stored.selected, stored.stepIndex, stored.ref);

  const uint32_t now = millis();
  startSynth(now);
  if (!synth_.ready()) Serial.printf("Si570 not found at 0x%02X, retrying\n", synth_.address());
  if (clock_.enabled()) {
    startClock(now);
    if (!clock_.ready()) Serial.printf("Si5351 not found at 0x%02X, retrying\n", clock_.address());
  }
}

void Controller::loop(uint32_t nowMs) {
  if (!synth_.ready() && nowMs - lastSynthTryMs_ >= kChipRetryMs) startSynth(nowMs);
  if (clock_.enabled() && nowMs - lastClockTryMs_ >= kChipRetryMs) {
    if (!clock_.ready()) {
      startClock(nowMs);
    } else {
      lastClockTryMs_ = nowMs;
      if (!clock_.check()) showMessage(kMessageMs, "Si5351 lost");  // re-initialised once it answers
    }
  }
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
  (void)nowMs;
  switch (action) {
    case UiAction::None:
    case UiAction::OutputChanged: break;  // only the display changes
    case UiAction::FrequencyChanged: applyFrequency(ui_.selected()); break;
    case UiAction::RefModeChanged:
      ref_.setMode(ui_.ref());
      Serial.printf("Reference: %s\n", ui_.ref() == RefMode::External ? "external" : "internal");
      break;
    case UiAction::RfChanged: applyRf(ui_.selected()); break;
    case UiAction::Save: saveState(); break;
    case UiAction::Calibrate: calibrate(); break;
  }
}

// ---------------------------------------------------------------- commands

CmdResult Controller::setFrequency(uint8_t output, uint32_t hz) {
  if (output >= kMaxOutputs || !ui_.output(output).fitted) return CmdResult::Unsupported;
  const OutputConfig& oc = ui_.output(output);
  if (hz < oc.minHz || hz > oc.maxHz) return CmdResult::OutOfRange;

  if (output == kSi570Output) {
    if (ref_.calibrating()) return CmdResult::Busy;
    ui_.setFrequency(output, hz);
    if (!synth_.ready()) return CmdResult::NotReady;  // applied once the Si570 answers
    if (synth_.setFrequency(hz)) return CmdResult::Ok;
    ui_.setFrequency(output, synth_.frequency());
    return synth_.ready() ? CmdResult::OutOfRange : CmdResult::NotReady;
  }

  const uint8_t clk = clockIndex(output);
  const uint32_t before = ui_.frequency(output);
  ui_.setFrequency(output, hz);
  if (!clock_.ready()) return CmdResult::NotReady;  // applied once the Si5351 answers
  if (clock_.setFrequency(clk, hz)) return CmdResult::Ok;
  ui_.setFrequency(output, clock_.frequency(clk) != 0 ? clock_.frequency(clk) : before);
  return CmdResult::PllInUse;
}

CmdResult Controller::setRfEnabled(uint8_t output, bool on) {
  if (output >= kMaxOutputs || !ui_.output(output).fitted || !ui_.output(output).hasRf)
    return CmdResult::Unsupported;
  if (output == kSi570Output && ref_.calibrating()) return CmdResult::Busy;
  if (ui_.setRfOn(output, on)) applyRf(output);
  return CmdResult::Ok;
}

CmdResult Controller::setDrive(uint8_t output, uint8_t mA) {
  if (!isClock(output) || !ui_.output(output).fitted || !ClockGen::validDrive(mA)) return CmdResult::Unsupported;
  driveMa_[output] = mA;
  clock_.setDrive(clockIndex(output), mA);
  return CmdResult::Ok;
}

CmdResult Controller::selectOutput(uint8_t output) {
  if (output >= kMaxOutputs || !ui_.output(output).fitted) return CmdResult::Unsupported;
  ui_.select(output);
  return CmdResult::Ok;
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
  for (uint8_t i = 0; i < kMaxOutputs; ++i) {
    s.outputs[i] = {ui_.frequency(i), ui_.rfOn(i)};
    s.driveMa[i] = driveMa_[i];
  }
  s.selected = ui_.selected();
  s.stepIndex = ui_.stepIndex();
  s.ref = ui_.ref();
  s.extXtalHz = ref_.extXtal();
  saveSettings(s);
  showMessage(kMessageMs, "Settings saved");
  return CmdResult::Ok;
}

CmdResult Controller::applyHardware(const HardwareConfig& hw) {
  if (ref_.calibrating()) return CmdResult::Busy;
  const bool sameChip = hw_.sameChip(hw);
  const bool sameClock = hw_.sameSi5351(hw);
  const bool corrChanged = hw_.s5CorrPpb != hw.s5CorrPpb;
  hw_ = hw;
  if (!sameChip) hw_.si570CorrPpb = 0;  // a correction measured on another part (or config) is meaningless
  saveHardwareConfig(hw_);
  ref_.setIntCorrection(hw_.si570CorrPpb);
  ui_.configure(uiConfig());  // new limits: clamps the stored frequencies
  ref_.setRefHz(hw_.refHz);

  if (!sameClock) {
    clock_.configure(hw_);
    if (clock_.enabled()) startClock(millis());
  } else {
    if (corrChanged) clock_.setCorrection(hw_.s5CorrPpb);
    for (uint8_t o = 1; o < kMaxOutputs; ++o) applyFrequency(o);
  }

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
  applyFrequency(kSi570Output);  // the limits may have moved the frequency
  return CmdResult::Ok;
}

// ---------------------------------------------------------------- internals

UiConfig Controller::uiConfig() const {
  UiConfig c;
  c.outputs[kSi570Output] = {true, hw_.minHz, hw_.maxHz, synth_.hasRfControl()};
  for (uint8_t o = 1; o < kMaxOutputs; ++o) c.outputs[o] = {hw_.s5Enabled, hw_.s5MinHz, hw_.s5MaxHz, true};
  c.hasRef = ref_.available();
  c.refOutput = kSi570Output;
  c.idleTimeoutMs = cfg::kIdleTimeoutMs;
  return c;
}

void Controller::startSynth(uint32_t nowMs) {
  lastSynthTryMs_ = nowMs;
  if (!synth_.begin()) return;
  Serial.printf("Si570 ready at 0x%02X, factory crystal %lu Hz\n", synth_.address(),
                (unsigned long)synth_.factoryXtal());
  if (!synth_.xtalPlausible()) showMessage(6000, "Chk start freq cfg");
  synth_.setRfEnabled(ui_.rfOn(kSi570Output));
  ref_.setMode(ui_.ref());
  applyFrequency(kSi570Output);
}

void Controller::startClock(uint32_t nowMs) {
  lastClockTryMs_ = nowMs;
  if (!clock_.begin()) return;
  Serial.printf("Si5351 ready at 0x%02X, %lu Hz crystal, correction %+.3f ppm\n", clock_.address(),
                (unsigned long)clock_.xtal(), clock_.correctionPpb() / 1000.0);
  for (uint8_t o = 1; o < kMaxOutputs; ++o) {
    const uint8_t clk = clockIndex(o);
    clock_.setDrive(clk, driveMa_[o]);
    clock_.setEnabled(clk, ui_.rfOn(o));
    applyFrequency(o);
  }
}

bool Controller::applyFrequency(uint8_t output) {
  if (output == kSi570Output) {
    if (!synth_.ready()) return false;
    if (synth_.setFrequency(ui_.frequency(output))) return true;
    ui_.setFrequency(output, synth_.frequency());
    showMessage(kMessageMs, synth_.ready() ? "Can't tune there" : "Si570 I2C error");
    return false;
  }
  if (!clock_.ready()) return false;
  const uint8_t clk = clockIndex(output);
  if (clock_.setFrequency(clk, ui_.frequency(output))) return true;
  if (clock_.frequency(clk) != 0) ui_.setFrequency(output, clock_.frequency(clk));
  showMessage(kMessageMs, "PLL busy >100MHz");
  return false;
}

void Controller::applyRf(uint8_t output) {
  if (output == kSi570Output)
    synth_.setRfEnabled(ui_.rfOn(output));
  else
    clock_.setEnabled(clockIndex(output), ui_.rfOn(output));
}

void Controller::reportCalibration(uint32_t nowMs) {
  (void)nowMs;
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

bool Controller::outputReady(uint8_t output) const {
  return output == kSi570Output ? synth_.ready() : clock_.ready();
}

const char* Controller::statusMessage() const {
  if ((int32_t)(messageUntilMs_ - millis()) > 0) return message_;
  const uint8_t sel = ui_.selected();
  if (sel == kSi570Output && !synth_.ready()) return "Si570 not found";
  if (isClock(sel) && !clock_.ready()) return "Si5351 not found";
  if (ref_.calibrating()) return "Calibrating...";
  return "";
}

DisplayModel Controller::displayModel() const {
  const uint8_t sel = ui_.selected();
  DisplayModel m;
  m.output = sel;
  m.outputCount = ui_.fittedCount();
  m.freqHz = ui_.frequency();
  m.stepHz = ui_.step();
  m.mode = ui_.mode();
  m.hasRef = ref_.available() && sel == kSi570Output;  // the reference only corrects the Si570
  m.ref = ui_.ref();
  m.refStatus = ref_.status();
  m.refErrorCentiPpm = (int32_t)lround(ref_.errorPpm() * 100.0);
  m.hasRf = ui_.output(sel).hasRf;
  m.rfOn = ui_.rfOn();
  m.driveMa = isClock(sel) ? driveMa_[sel] : 0;
  snprintf(m.message, sizeof m.message, "%s", statusMessage());
  return m;
}
