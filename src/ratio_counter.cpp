#include "ratio_counter.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_timer.h>

namespace {

constexpr int kHighLimit = 32767;  // 16-bit hardware counter, extended in the driver's ISR
constexpr int kLowLimit = -32768;

bool createUnit(int edgePin, int gatePin, pcnt_unit_handle_t* out) {
  pcnt_unit_config_t unitCfg = {};
  unitCfg.low_limit = kLowLimit;
  unitCfg.high_limit = kHighLimit;
  unitCfg.flags.accum_count = 1;
  pcnt_unit_handle_t unit = nullptr;
  if (pcnt_new_unit(&unitCfg, &unit) != ESP_OK) return false;

  pcnt_chan_config_t chanCfg = {};
  chanCfg.edge_gpio_num = edgePin;
  chanCfg.level_gpio_num = gatePin;
  pcnt_channel_handle_t chan = nullptr;
  bool ok = pcnt_new_channel(unit, &chanCfg, &chan) == ESP_OK;
  // Count rising edges, but only while the gate is high.
  ok = ok && pcnt_channel_set_edge_action(chan, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD) == ESP_OK;
  ok = ok && pcnt_channel_set_level_action(chan, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_HOLD) == ESP_OK;
  ok = ok && pcnt_unit_add_watch_point(unit, kHighLimit) == ESP_OK;  // needed for accum_count
  ok = ok && pcnt_unit_enable(unit) == ESP_OK;
  ok = ok && pcnt_unit_clear_count(unit) == ESP_OK;
  ok = ok && pcnt_unit_start(unit) == ESP_OK;
  if (!ok) {
    Serial.printf("Pulse counter setup failed on GPIO %d\n", edgePin);
    return false;
  }
  *out = unit;
  return true;
}

}  // namespace

bool RatioCounter::begin(int refPin, int sigPin, int gatePin) {
  gatePin_ = gatePin;
  if (!createUnit(refPin, gatePin, &ref_) || !createUnit(sigPin, gatePin, &sig_)) return false;
  // The channels configured the gate pad as an input; also drive it from the same pad.
  gpio_set_direction((gpio_num_t)gatePin_, GPIO_MODE_INPUT_OUTPUT);
  gpio_set_level((gpio_num_t)gatePin_, 0);
  return true;
}

void RatioCounter::openGate() {
  pcnt_unit_clear_count(ref_);
  pcnt_unit_clear_count(sig_);
  // Baselines make the result independent of whether clearing also resets the
  // driver's overflow accumulator.
  pcnt_unit_get_count(ref_, &refBase_);
  pcnt_unit_get_count(sig_, &sigBase_);
  openedUs_ = esp_timer_get_time();
  gpio_set_level((gpio_num_t)gatePin_, 1);
}

void RatioCounter::closeGate() {
  gpio_set_level((gpio_num_t)gatePin_, 0);
  closedUs_ = esp_timer_get_time();
}

RatioCounter::Sample RatioCounter::read() const {
  int ref = 0;
  int sig = 0;
  pcnt_unit_get_count(ref_, &ref);
  pcnt_unit_get_count(sig_, &sig);
  return {(int64_t)ref - refBase_, (int64_t)sig - sigBase_, closedUs_ - openedUs_};
}
