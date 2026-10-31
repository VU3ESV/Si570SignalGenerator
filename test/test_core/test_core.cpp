// Host tests for the hardware-independent logic in lib/SigGenCore.
#include <unity.h>

#include <button.h>
#include <fll.h>
#include <freq_format.h>
#include <quadrature.h>
#include <ui_model.h>

#include <cstring>

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------- encoder

static int feed(QuadratureDetentDecoder& d, const uint8_t* seq, size_t n) {
  int clicks = 0;
  for (size_t i = 0; i < n; ++i) clicks += d.update(seq[i]);
  return clicks;
}

void test_full_step_encoder_counts_one_click_per_cycle() {
  QuadratureDetentDecoder d(4);
  d.reset(3);
  const uint8_t up[] = {2, 0, 1, 3, 2, 0, 1, 3};
  const uint8_t down[] = {1, 0, 2, 3};
  TEST_ASSERT_EQUAL(2, feed(d, up, sizeof up));
  TEST_ASSERT_EQUAL(-1, feed(d, down, sizeof down));
}

void test_encoder_bounce_cancels_out() {
  QuadratureDetentDecoder d(4);
  d.reset(3);
  // A chatters on the first edge, then the click completes normally.
  const uint8_t bouncy[] = {2, 3, 2, 3, 2, 0, 1, 0, 1, 3};
  TEST_ASSERT_EQUAL(1, feed(d, bouncy, sizeof bouncy));
  // Wiggling inside a detent and returning is not a click.
  const uint8_t wiggle[] = {2, 3, 1, 3};
  TEST_ASSERT_EQUAL(0, feed(d, wiggle, sizeof wiggle));
}

void test_encoder_resynchronises_after_missed_edge() {
  QuadratureDetentDecoder d(4);
  d.reset(3);
  const uint8_t missed[] = {2, 1, 3};  // 0 was missed: illegal 2->1 counts as nothing
  feed(d, missed, sizeof missed);
  const uint8_t clean[] = {2, 0, 1, 3};
  TEST_ASSERT_EQUAL(1, feed(d, clean, sizeof clean));
}

void test_half_step_encoder_counts_two_clicks_per_cycle() {
  QuadratureDetentDecoder d(2);
  d.reset(3);
  const uint8_t up[] = {2, 0, 1, 3};
  TEST_ASSERT_EQUAL(2, feed(d, up, sizeof up));
}

// ---------------------------------------------------------------- button

void test_button_short_press_on_release() {
  ButtonClassifier b(30, 800);
  TEST_ASSERT_EQUAL(ButtonClassifier::Event::None, b.update(true, 0));
  TEST_ASSERT_EQUAL(ButtonClassifier::Event::None, b.update(true, 40));
  TEST_ASSERT_EQUAL(ButtonClassifier::Event::None, b.update(false, 200));
  TEST_ASSERT_EQUAL(ButtonClassifier::Event::Short, b.update(false, 240));
}

void test_button_long_press_fires_once_while_held() {
  ButtonClassifier b(30, 800);
  b.update(true, 0);
  b.update(true, 40);
  TEST_ASSERT_EQUAL(ButtonClassifier::Event::Long, b.update(true, 850));
  TEST_ASSERT_EQUAL(ButtonClassifier::Event::None, b.update(true, 2000));
  b.update(false, 2100);
  TEST_ASSERT_EQUAL(ButtonClassifier::Event::None, b.update(false, 2200));
}

void test_button_ignores_glitches() {
  ButtonClassifier b(30, 800);
  for (uint32_t t = 0; t < 100; t += 10) TEST_ASSERT_EQUAL(ButtonClassifier::Event::None, b.update(t % 20 == 0, t));
  TEST_ASSERT_FALSE(b.pressed());
}

// ---------------------------------------------------------------- formatting

void test_frequency_formatting() {
  char buf[16];
  TEST_ASSERT_EQUAL(10, formatFrequency(buf, sizeof buf, 10000000));
  TEST_ASSERT_EQUAL_STRING("10.000 000", buf);
  formatFrequency(buf, sizeof buf, 145123456);
  TEST_ASSERT_EQUAL_STRING("145.123 456", buf);
  formatFrequency(buf, sizeof buf, 1417500000);
  TEST_ASSERT_EQUAL_STRING("1417.500 000", buf);
}

void test_digit_index_for_cursor() {
  char buf[16];
  const size_t len = formatFrequency(buf, sizeof buf, 145123456);  // "145.123 456"
  TEST_ASSERT_EQUAL('6', buf[digitIndexForDecade(buf, len, 0)]);
  TEST_ASSERT_EQUAL('4', buf[digitIndexForDecade(buf, len, 2)]);
  TEST_ASSERT_EQUAL('3', buf[digitIndexForDecade(buf, len, 3)]);
  TEST_ASSERT_EQUAL('5', buf[digitIndexForDecade(buf, len, 6)]);
  TEST_ASSERT_EQUAL('1', buf[digitIndexForDecade(buf, len, 8)]);
  TEST_ASSERT_EQUAL(-1, digitIndexForDecade(buf, len, 9));
  TEST_ASSERT_EQUAL(3u, decadeOf(1000));
  TEST_ASSERT_EQUAL_STRING("10kHz", stepLabel(10000));
}

// ---------------------------------------------------------------- UI model

// Output 0 behaves like the Si570 (10-160 MHz); with clocks, outputs 1-3 behave like the
// Si5351 (8 kHz-160 MHz, RF control always present).
static UiModel makeUi(bool hasRef, bool hasRf, bool clocks = false) {
  UiModel ui;
  UiConfig cfg;
  cfg.outputs[0] = {true, 10000000, 160000000, hasRf};
  for (uint8_t i = 1; i < kMaxOutputs; ++i) cfg.outputs[i] = {clocks, 8000, 160000000, true};
  cfg.hasRef = hasRef;
  cfg.refOutput = 0;
  cfg.idleTimeoutMs = 10000;
  ui.configure(cfg);
  OutputState st[kMaxOutputs];
  st[0] = {14000000, true};
  ui.restore(st, 0, 3, RefMode::Internal);
  return ui;
}

static void restoreOne(UiModel& ui, uint32_t hz, uint8_t step, RefMode ref) {
  OutputState st[kMaxOutputs];
  st[0] = {hz, true};
  ui.restore(st, 0, step, ref);
}

void test_tuning_steps_and_clamps() {
  UiModel ui = makeUi(true, true);
  TEST_ASSERT_EQUAL(UiAction::FrequencyChanged, ui.rotate(5, 0));
  TEST_ASSERT_EQUAL_UINT32(14005000, ui.frequency());
  restoreOne(ui, 10000500, 6, RefMode::Internal);  // 1 MHz step
  ui.rotate(-1, 0);
  TEST_ASSERT_EQUAL_UINT32(10000000, ui.frequency());
  TEST_ASSERT_EQUAL(UiAction::None, ui.rotate(-1, 0));  // already at the limit
}

void test_step_mode_changes_step_within_range() {
  UiModel ui = makeUi(true, true);
  ui.shortPress(0);
  TEST_ASSERT_EQUAL(Mode::Step, ui.mode());
  ui.rotate(2, 0);
  TEST_ASSERT_EQUAL_UINT32(100000, ui.step());
  ui.rotate(50, 0);
  TEST_ASSERT_EQUAL_UINT32(100000000, ui.step());  // 150 MHz span allows 100 MHz steps
  ui.rotate(-50, 0);
  TEST_ASSERT_EQUAL_UINT32(1, ui.step());
}

void test_modes_cycle_and_skip_missing_hardware() {
  UiModel full = makeUi(true, true);
  const Mode expectFull[] = {Mode::Step, Mode::Ref, Mode::Rf, Mode::Tune};  // one output: no OUT
  for (Mode m : expectFull) {
    full.shortPress(0);
    TEST_ASSERT_EQUAL(m, full.mode());
  }
  UiModel bare = makeUi(false, false);
  bare.shortPress(0);
  bare.shortPress(0);
  TEST_ASSERT_EQUAL(Mode::Tune, bare.mode());
}

void test_reference_selection_and_long_press_actions() {
  UiModel ui = makeUi(true, true);
  TEST_ASSERT_EQUAL(UiAction::Save, ui.longPress(0));
  ui.shortPress(0);
  ui.shortPress(0);  // REF
  TEST_ASSERT_EQUAL(UiAction::Save, ui.longPress(0));  // internal: nothing to calibrate
  TEST_ASSERT_EQUAL(UiAction::RefModeChanged, ui.rotate(1, 0));
  TEST_ASSERT_EQUAL(RefMode::External, ui.ref());
  TEST_ASSERT_EQUAL(UiAction::None, ui.rotate(1, 0));
  TEST_ASSERT_EQUAL(UiAction::Calibrate, ui.longPress(0));
}

void test_idle_returns_to_tune() {
  UiModel ui = makeUi(true, true);
  ui.shortPress(1000);
  ui.tick(10999);
  TEST_ASSERT_EQUAL(Mode::Step, ui.mode());
  ui.tick(11000);
  TEST_ASSERT_EQUAL(Mode::Tune, ui.mode());
}

void test_restore_sanitises_stored_values() {
  UiModel ui = makeUi(false, false);
  OutputState st[kMaxOutputs];
  st[0] = {5000000, true};
  ui.restore(st, 3, 200, RefMode::External);  // output 3 isn't fitted
  TEST_ASSERT_EQUAL_UINT32(10000000, ui.frequency());
  TEST_ASSERT_EQUAL(0, ui.selected());
  TEST_ASSERT_EQUAL_UINT32(100000000, ui.step());
  TEST_ASSERT_EQUAL(RefMode::Internal, ui.ref());
}

void test_direct_setters_clamp_and_report_changes() {
  UiModel ui = makeUi(false, true);
  TEST_ASSERT_TRUE(ui.setFrequency(0, 20000000));
  TEST_ASSERT_FALSE(ui.setFrequency(0, 20000000));
  ui.setFrequency(0, 500000000);
  TEST_ASSERT_EQUAL_UINT32(160000000, ui.frequency());
  TEST_ASSERT_TRUE(ui.setStepIndex(99));
  TEST_ASSERT_EQUAL(ui.maxStepIndex(), ui.stepIndex());
  TEST_ASSERT_FALSE(ui.setRef(RefMode::External));  // no reference hardware
  TEST_ASSERT_EQUAL(RefMode::Internal, ui.ref());
  TEST_ASSERT_TRUE(ui.setRfOn(0, false));
  TEST_ASSERT_FALSE(ui.rfOn());
  TEST_ASSERT_FALSE(ui.select(2));  // no clocks fitted
}

void test_configure_reclamps_state_to_new_limits() {
  UiModel ui = makeUi(true, true);
  restoreOne(ui, 150000000, 8, RefMode::External);  // 100 MHz step
  ui.shortPress(0);
  ui.shortPress(0);  // REF mode
  UiConfig narrow = ui.config();
  narrow.outputs[0].maxHz = 30000000;
  narrow.hasRef = false;
  ui.configure(narrow);
  TEST_ASSERT_EQUAL_UINT32(30000000, ui.frequency());
  TEST_ASSERT_EQUAL_UINT32(10000000, ui.step());  // 20 MHz span: 10 MHz steps max
  TEST_ASSERT_EQUAL(RefMode::Internal, ui.ref());
  TEST_ASSERT_EQUAL(Mode::Tune, ui.mode());
}

void test_outputs_keep_their_own_frequency_and_rf() {
  UiModel ui = makeUi(true, true, true);
  TEST_ASSERT_TRUE(ui.setFrequency(2, 7074000));
  TEST_ASSERT_TRUE(ui.setRfOn(3, false));
  TEST_ASSERT_TRUE(ui.select(2));
  TEST_ASSERT_EQUAL_UINT32(7074000, ui.frequency());
  TEST_ASSERT_EQUAL(UiAction::FrequencyChanged, ui.rotate(1, 0));  // 1 kHz step
  TEST_ASSERT_EQUAL_UINT32(7075000, ui.frequency(2));
  TEST_ASSERT_EQUAL_UINT32(14000000, ui.frequency(0));  // untouched
  TEST_ASSERT_FALSE(ui.rfOn(3));
  TEST_ASSERT_TRUE(ui.rfOn(2));
  ui.setFrequency(1, 1000);  // below the clock range
  TEST_ASSERT_EQUAL_UINT32(8000, ui.frequency(1));
}

void test_out_mode_selects_fitted_outputs_and_wraps() {
  UiModel ui = makeUi(true, true, true);
  ui.shortPress(0);
  ui.shortPress(0);
  TEST_ASSERT_EQUAL(Mode::Out, ui.mode());
  TEST_ASSERT_EQUAL(UiAction::OutputChanged, ui.rotate(1, 0));
  TEST_ASSERT_EQUAL(1, ui.selected());
  ui.rotate(2, 0);
  TEST_ASSERT_EQUAL(3, ui.selected());
  ui.rotate(1, 0);
  TEST_ASSERT_EQUAL(0, ui.selected());  // wraps
  ui.rotate(-1, 0);
  TEST_ASSERT_EQUAL(3, ui.selected());
  // An output that isn't fitted is skipped.
  UiConfig cfg = ui.config();
  cfg.outputs[2].fitted = false;
  ui.configure(cfg);
  ui.select(1);
  ui.rotate(1, 0);
  TEST_ASSERT_EQUAL(3, ui.selected());
}

void test_ref_mode_only_on_the_reference_output() {
  UiModel ui = makeUi(true, true, true);
  ui.select(1);
  const Mode expect[] = {Mode::Step, Mode::Out, Mode::Rf, Mode::Tune};  // no REF on a clock
  for (Mode m : expect) {
    ui.shortPress(0);
    TEST_ASSERT_EQUAL(m, ui.mode());
  }
  ui.select(0);
  ui.shortPress(0);
  ui.shortPress(0);
  ui.shortPress(0);
  TEST_ASSERT_EQUAL(Mode::Ref, ui.mode());
  ui.select(2);  // e.g. from the web page while the encoder is in REF mode
  TEST_ASSERT_EQUAL(Mode::Tune, ui.mode());
}

void test_step_clamped_to_selected_output_range() {
  UiModel ui = makeUi(false, true, true);
  UiConfig cfg = ui.config();
  cfg.outputs[1].maxHz = 50000;  // 8-50 kHz: steps up to 10 kHz
  ui.configure(cfg);
  ui.setStepIndex(8);
  TEST_ASSERT_EQUAL_UINT32(100000000, ui.step());
  ui.select(1);
  TEST_ASSERT_EQUAL_UINT32(10000, ui.step());
}

// ---------------------------------------------------------------- FLL

static const uint32_t kXtal = 114231984;

void test_fll_locked_when_on_frequency() {
  FllConfig cfg;
  FllResult r = fllEvaluate(cfg, {10000000, 10000000, 1000000}, 10000000, kXtal);
  TEST_ASSERT_EQUAL(RefStatus::Locked, r.status);
  TEST_ASSERT_EQUAL_UINT32(kXtal, r.xtalHz);
}

void test_fll_corrects_high_output_in_one_step() {
  FllConfig cfg;
  // Si570 runs 5 ppm high at 25 MHz; the gate is 30 ppm long (ESP32 clock error), which
  // stretches both counts equally and must not matter.
  FllResult r = fllEvaluate(cfg, {10000300, 25000875, 1000000}, 25000000, kXtal);
  TEST_ASSERT_EQUAL(RefStatus::Tracking, r.status);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 5.0, r.errorPpm);
  TEST_ASSERT_UINT32_WITHIN(1, (uint32_t)(kXtal * (1 + 5e-6) + 0.5), r.xtalHz);
}

void test_fll_small_error_corrected_partially() {
  FllConfig cfg;
  FllResult r = fllEvaluate(cfg, {10000000, 10000010, 1000000}, 10000000, kXtal);  // +1 ppm
  TEST_ASSERT_EQUAL(RefStatus::Tracking, r.status);
  TEST_ASSERT_UINT32_WITHIN(1, (uint32_t)(kXtal * (1 + 0.5e-6) + 0.5), r.xtalHz);
}

void test_fll_detects_missing_or_wrong_reference() {
  FllConfig cfg;
  TEST_ASSERT_EQUAL(RefStatus::NoRef, fllEvaluate(cfg, {0, 10000000, 1000000}, 10000000, kXtal).status);
  TEST_ASSERT_EQUAL(RefStatus::NoRef, fllEvaluate(cfg, {5000000, 10000000, 1000000}, 10000000, kXtal).status);
}

void test_fll_detects_missing_si570_sample() {
  FllConfig cfg;
  FllResult r = fllEvaluate(cfg, {10000000, 0, 1000000}, 10000000, kXtal);
  TEST_ASSERT_EQUAL(RefStatus::NoSignal, r.status);
  TEST_ASSERT_EQUAL_UINT32(kXtal, r.xtalHz);
  // Counting the wrong thing (e.g. half the frequency) must not be "corrected".
  r = fllEvaluate(cfg, {10000000, 5000000, 1000000}, 10000000, kXtal);
  TEST_ASSERT_EQUAL(RefStatus::NoSignal, r.status);
  TEST_ASSERT_EQUAL_UINT32(kXtal, r.xtalHz);
}

void test_crystal_correction_maths() {
  TEST_ASSERT_EQUAL_UINT32(114223113, applyPpb(114223113, 0));
  TEST_ASSERT_EQUAL_UINT32(114223113 + 114, applyPpb(114223113, 1000));  // +1 ppm = 114.2 Hz
  TEST_ASSERT_EQUAL_UINT32(114223113 - 1142, applyPpb(114223113, -10000));
  // 11 MHz read 11.000 110 MHz (+10 ppm): the crystal is 10 ppm fast.
  TEST_ASSERT_EQUAL_INT32(10000, correctionFromMeasurement(11000000, 11000110.0, 0));
  // Already corrected by +10 ppm and now reading 2 ppm low: (1 - 2e-6)(1 + 10e-6) - 1
  // = 7.99998 ppm, i.e. 8000 ppb.
  TEST_ASSERT_EQUAL_INT32(8000, correctionFromMeasurement(11000000, 10999978.0, 10000));
  TEST_ASSERT_EQUAL_INT32(0, correctionFromMeasurement(11000000, 11000000.0, 0));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_full_step_encoder_counts_one_click_per_cycle);
  RUN_TEST(test_encoder_bounce_cancels_out);
  RUN_TEST(test_encoder_resynchronises_after_missed_edge);
  RUN_TEST(test_half_step_encoder_counts_two_clicks_per_cycle);
  RUN_TEST(test_button_short_press_on_release);
  RUN_TEST(test_button_long_press_fires_once_while_held);
  RUN_TEST(test_button_ignores_glitches);
  RUN_TEST(test_frequency_formatting);
  RUN_TEST(test_digit_index_for_cursor);
  RUN_TEST(test_tuning_steps_and_clamps);
  RUN_TEST(test_step_mode_changes_step_within_range);
  RUN_TEST(test_modes_cycle_and_skip_missing_hardware);
  RUN_TEST(test_reference_selection_and_long_press_actions);
  RUN_TEST(test_idle_returns_to_tune);
  RUN_TEST(test_restore_sanitises_stored_values);
  RUN_TEST(test_direct_setters_clamp_and_report_changes);
  RUN_TEST(test_configure_reclamps_state_to_new_limits);
  RUN_TEST(test_outputs_keep_their_own_frequency_and_rf);
  RUN_TEST(test_out_mode_selects_fitted_outputs_and_wraps);
  RUN_TEST(test_ref_mode_only_on_the_reference_output);
  RUN_TEST(test_step_clamped_to_selected_output_range);
  RUN_TEST(test_fll_locked_when_on_frequency);
  RUN_TEST(test_fll_corrects_high_output_in_one_step);
  RUN_TEST(test_fll_small_error_corrected_partially);
  RUN_TEST(test_fll_detects_missing_or_wrong_reference);
  RUN_TEST(test_fll_detects_missing_si570_sample);
  RUN_TEST(test_crystal_correction_maths);
  return UNITY_END();
}
