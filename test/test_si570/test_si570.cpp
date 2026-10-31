// Si570 driver tests against the simulated chip in test/fakes/Wire.h.
#include <unity.h>

#include <Si570.h>
#include <Wire.h>

#include <cmath>

// Factory registers 7..12 of a real 10 MHz part, from AA0ZZ's QEX article (Table 5).
static const uint8_t kFactory10MHz[6] = {0x53, 0xC2, 0xA0, 0x50, 0xE9, 0xFD};
static const double kStartupHz = 10e6;

static double ppm(double actual, double target) { return (actual / target - 1.0) * 1e6; }

void setUp() { Wire.reset(kFactory10MHz, kStartupHz); }
void tearDown() {}

static void assertClean() {
  if (!Wire.violations.empty()) TEST_FAIL_MESSAGE(Wire.violations.front().c_str());
}

void test_begin_reads_factory_crystal_frequency() {
  Si570 si(0x55, 10000000);
  TEST_ASSERT_EQUAL(SI570_READY, si.begin());
  // The article derives 114.231984 MHz for this part.
  TEST_ASSERT_UINT32_WITHIN(2, 114231984, si.getXtalFrequency());
  TEST_ASSERT_EQUAL_UINT32(si.getXtalFrequency(), si.getFactoryXtalFrequency());
}

void test_begin_reports_missing_chip() {
  Wire.present = false;
  Si570 si(0x55, 10000000);
  TEST_ASSERT_EQUAL(SI570_ERROR, si.begin());
}

void test_every_frequency_is_accurate_and_in_spec() {
  Si570 si(0x55, 10000000);
  si.begin();
  for (uint32_t f = 10000000; f <= 160000000; f += 12347) {
    TEST_ASSERT_EQUAL(SI570_READY, si.setFrequency(f));
    // 1 Hz crystal-frequency resolution limits accuracy to ~0.009 ppm
    if (std::fabs(ppm(Wire.outputHz(), f)) > 0.01) {
      char msg[96];
      snprintf(msg, sizeof msg, "f=%u Hz off by %.4f ppm", f, ppm(Wire.outputHz(), f));
      TEST_FAIL_MESSAGE(msg);
    }
    assertClean();
  }
}

void test_fine_tuning_walk_stays_glitch_free_and_in_spec() {
  Si570 si(0x55, 10000000);
  si.begin();
  for (uint32_t f = 50000000; f <= 52000000; f += 1000) {
    TEST_ASSERT_EQUAL(SI570_READY, si.setFrequency(f));
    TEST_ASSERT_TRUE(std::fabs(ppm(Wire.outputHz(), f)) < 0.01);
  }
  for (uint32_t f = 52000000; f >= 50000000; f -= 777) si.setFrequency(f);
  assertClean();
  TEST_ASSERT_TRUE(Wire.freezeMUpdates > 2 * Wire.newFreqCount);
}

void test_small_step_uses_freeze_m_sequence() {
  Si570 si(0x55, 10000000);
  si.begin();
  si.setFrequency(14000000);
  Wire.writes.clear();
  const int newFreqBefore = Wire.newFreqCount;

  si.setFrequency(14000100);

  TEST_ASSERT_EQUAL(newFreqBefore, Wire.newFreqCount);
  TEST_ASSERT_EQUAL(3, (int)Wire.writes.size());
  TEST_ASSERT_EQUAL(135, Wire.writes[0].reg);
  TEST_ASSERT_EQUAL_HEX8(0x20, Wire.writes[0].data[0]);
  TEST_ASSERT_EQUAL(7, Wire.writes[1].reg);
  TEST_ASSERT_EQUAL(6, (int)Wire.writes[1].data.size());
  TEST_ASSERT_EQUAL(135, Wire.writes[2].reg);
  TEST_ASSERT_EQUAL_HEX8(0x00, Wire.writes[2].data[0]);
  assertClean();
}

void test_large_step_uses_freeze_dco_sequence() {
  Si570 si(0x55, 10000000);
  si.begin();
  si.setFrequency(14000000);
  Wire.writes.clear();

  si.setFrequency(28000000);

  TEST_ASSERT_EQUAL(4, (int)Wire.writes.size());
  TEST_ASSERT_EQUAL(137, Wire.writes[0].reg);
  TEST_ASSERT_EQUAL_HEX8(0x10, Wire.writes[0].data[0]);
  TEST_ASSERT_EQUAL(7, Wire.writes[1].reg);
  TEST_ASSERT_EQUAL(137, Wire.writes[2].reg);
  TEST_ASSERT_EQUAL_HEX8(0x00, Wire.writes[2].data[0]);
  TEST_ASSERT_EQUAL(135, Wire.writes[3].reg);
  TEST_ASSERT_EQUAL_HEX8(0x40, Wire.writes[3].data[0]);
  TEST_ASSERT_TRUE(std::fabs(ppm(Wire.outputHz(), 28e6)) < 0.01);
  assertClean();
}

// Upstream left HS_DIV/N1 unset after init, so this first small step programmed RFREQ = 0.
void test_first_small_step_after_begin_uses_factory_dividers() {
  Si570 si(0x55, 10000000);
  si.begin();
  TEST_ASSERT_EQUAL(SI570_READY, si.setFrequency(10001000));
  TEST_ASSERT_TRUE(std::fabs(ppm(Wire.outputHz(), 10001000)) < 0.01);
  assertClean();
}

void test_7ppm_parts_use_registers_13_to_18() {
  Wire.reset(kFactory10MHz, kStartupHz, 13);
  Si570 si(0x55, 10000000, 13);
  TEST_ASSERT_EQUAL(SI570_READY, si.begin());
  TEST_ASSERT_UINT32_WITHIN(2, 114231984, si.getXtalFrequency());
  si.setFrequency(21000000);
  TEST_ASSERT_TRUE(std::fabs(ppm(Wire.outputHz(), 21e6)) < 0.01);
  for (int r = 7; r <= 12; ++r) TEST_ASSERT_EQUAL_HEX8(0, Wire.regs[r]);
  assertClean();
}

void test_out_of_range_frequency_is_rejected_without_writes() {
  Si570 si(0x55, 10000000);
  si.begin();
  si.setFrequency(14000000);
  Wire.writes.clear();

  TEST_ASSERT_EQUAL(SI570_ERROR, si.setFrequency(3000000));    // below the ~3.44 MHz divider floor
  TEST_ASSERT_EQUAL(SI570_ERROR, si.setFrequency(955000000));  // 945-970 MHz gap
  TEST_ASSERT_EQUAL(0, (int)Wire.writes.size());
  TEST_ASSERT_EQUAL_UINT32(14000000, si.getFrequency());
  TEST_ASSERT_TRUE(std::fabs(ppm(Wire.outputHz(), 14e6)) < 0.01);
}

void test_small_crystal_correction_retunes_glitch_free() {
  Si570 si(0x55, 10000000);
  si.begin();
  si.setFrequency(10100000);
  const uint32_t xtal = si.getXtalFrequency();
  const int newFreqBefore = Wire.newFreqCount;

  // Pretend an external reference showed the crystal is 2 ppm high.
  const uint32_t corrected = xtal + 228;
  TEST_ASSERT_EQUAL(SI570_READY, si.setXtalFrequency(corrected));

  TEST_ASSERT_EQUAL(newFreqBefore, Wire.newFreqCount);
  // The real crystal didn't change, so the output moves by -(corrected/xtal - 1).
  TEST_ASSERT_FLOAT_WITHIN(0.01, ppm(xtal, corrected), ppm(Wire.outputHz(), 10.1e6));
  assertClean();
}

// This part's factory 10 MHz setting runs the DCO at 4.80 GHz, below the 4.85 GHz spec.
void test_crystal_correction_on_factory_dividers_moves_dco_into_spec() {
  Si570 si(0x55, 10000000);
  si.begin();
  TEST_ASSERT_TRUE(Wire.dco() < 4.85e9);
  si.setFrequency(10000000);  // no-op: already there

  TEST_ASSERT_EQUAL(SI570_READY, si.setXtalFrequency(si.getXtalFrequency() + 228));

  TEST_ASSERT_TRUE(Wire.dco() >= 4.85e9);
  assertClean();
}

void test_large_crystal_change_uses_full_reprogramming() {
  Si570 si(0x55, 10000000);
  si.begin();
  si.setFrequency(10000000);
  const int newFreqBefore = Wire.newFreqCount;

  // e.g. a wrong start-up frequency setting being replaced by the nominal crystal
  TEST_ASSERT_EQUAL(SI570_READY, si.setXtalFrequency(si.getXtalFrequency() / 100 * 101));

  TEST_ASSERT_EQUAL(newFreqBefore + 1, Wire.newFreqCount);
  assertClean();
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_begin_reads_factory_crystal_frequency);
  RUN_TEST(test_begin_reports_missing_chip);
  RUN_TEST(test_every_frequency_is_accurate_and_in_spec);
  RUN_TEST(test_fine_tuning_walk_stays_glitch_free_and_in_spec);
  RUN_TEST(test_small_step_uses_freeze_m_sequence);
  RUN_TEST(test_large_step_uses_freeze_dco_sequence);
  RUN_TEST(test_first_small_step_after_begin_uses_factory_dividers);
  RUN_TEST(test_7ppm_parts_use_registers_13_to_18);
  RUN_TEST(test_out_of_range_frequency_is_rejected_without_writes);
  RUN_TEST(test_small_crystal_correction_retunes_glitch_free);
  RUN_TEST(test_crystal_correction_on_factory_dividers_moves_dco_into_spec);
  RUN_TEST(test_large_crystal_change_uses_full_reprogramming);
  return UNITY_END();
}
