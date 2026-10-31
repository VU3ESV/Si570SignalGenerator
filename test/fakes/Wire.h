// Host-side TwoWire with a simulated Si570 behind it.
//
// The simulation applies the datasheet rules, so tests catch protocol mistakes as
// well as wrong maths:
//  - RECALL (135 bit 0) loads the factory NVM configuration
//  - HS_DIV/N1 may only change while Freeze DCO (137 bit 4) is set
//  - RFREQ written without Freeze M / Freeze DCO counts as a glitch
//  - a Freeze-M update must stay within ±3500 ppm of the last NewFreq centre
//  - the DCO must stay inside 4.85 - 5.67 GHz
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

class TwoWire {
 public:
  struct Write {
    uint8_t reg;
    std::vector<uint8_t> data;
  };

  // ---- Arduino API subset used by the driver ----
  bool begin() { return true; }
  void beginTransmission(uint8_t address) {
    txAddress_ = address;
    tx_.clear();
  }
  size_t write(uint8_t b) {
    tx_.push_back(b);
    return 1;
  }
  size_t write(const uint8_t* data, size_t n) {
    tx_.insert(tx_.end(), data, data + n);
    return n;
  }
  uint8_t endTransmission(bool = true) {
    if (!present || txAddress_ != address) return 2;  // address NACK
    if (tx_.empty()) return 0;
    pointer_ = tx_[0];
    if (tx_.size() > 1) {
      writes.push_back({tx_[0], std::vector<uint8_t>(tx_.begin() + 1, tx_.end())});
      for (size_t i = 1; i < tx_.size(); ++i) storeRegister(pointer_++, tx_[i]);
    }
    return 0;
  }
  uint8_t requestFrom(uint8_t addr, uint8_t n) {
    rx_.clear();
    rxPos_ = 0;
    if (!present || addr != address) return 0;
    for (uint8_t i = 0; i < n; ++i) rx_.push_back(regs[pointer_++]);
    return n;
  }
  int available() { return (int)(rx_.size() - rxPos_); }
  int read() { return rxPos_ < rx_.size() ? rx_[rxPos_++] : -1; }

  // ---- Simulated device ----
  uint8_t address = 0x55;
  bool present = true;
  uint8_t base = 7;           // 13 for 7 ppm parts
  double xtalHz = 0;          // the part's true crystal frequency
  uint8_t regs[256] = {};
  uint8_t nvm[6] = {};
  std::vector<Write> writes;
  std::vector<std::string> violations;
  int newFreqCount = 0;
  int freezeMUpdates = 0;

  void reset(const uint8_t factory[6], double startupHz, uint8_t regBase = 7) {
    *this = TwoWire();
    base = regBase;
    memcpy(nvm, factory, 6);
    memcpy(&regs[base], nvm, 6);
    xtalHz = startupHz * hsdiv() * n1() / rfreq();
    centreDco_ = dco();
  }

  int hsdiv() const { return 4 + (regs[base] >> 5); }
  int n1() const { return (((regs[base] & 0x1F) << 2) | (regs[base + 1] >> 6)) + 1; }
  double rfreq() const {
    uint64_t r = (uint64_t)(regs[base + 1] & 0x3F) << 32 | (uint64_t)regs[base + 2] << 24 |
                 (uint64_t)regs[base + 3] << 16 | (uint64_t)regs[base + 4] << 8 | regs[base + 5];
    return (double)r / (double)(1ULL << 28);
  }
  double dco() const { return xtalHz * rfreq(); }
  double outputHz() const { return dco() / (hsdiv() * n1()); }

 private:
  void storeRegister(uint8_t reg, uint8_t value) {
    const bool frozenDco = regs[137] & 0x10;
    const bool frozenM = regs[135] & 0x20;
    const bool config = reg >= base && reg < base + 6;
    if (config && reg <= base + 1 && !frozenDco) {
      const uint8_t mask = (reg == base) ? 0xFF : 0xC0;  // HS_DIV / N1 bits
      if ((regs[reg] & mask) != (value & mask)) violations.push_back("HS_DIV/N1 changed without Freeze DCO");
    }
    if (config && !frozenDco && !frozenM && regs[reg] != value) violations.push_back("RFREQ written without freeze");

    if (reg == 135) {
      if (value & 0x01) memcpy(&regs[base], nvm, 6), value &= ~0x01;  // RECALL
      if (value & 0x40) {                                             // NewFreq
        if (frozenDco) violations.push_back("NewFreq while DCO frozen");
        checkDcoRange();
        centreDco_ = dco();
        newFreqCount++;
        value &= ~0x40;
      }
      if (frozenM && !(value & 0x20)) {  // Freeze M released: small change applied
        checkDcoRange();
        if (std::fabs(dco() / centreDco_ - 1) > 3500e-6) violations.push_back("Freeze-M change beyond 3500 ppm");
        freezeMUpdates++;
      }
    }
    regs[reg] = value;
  }
  void checkDcoRange() {
    if (dco() < 4.85e9 - 1 || dco() > 5.67e9 + 1) violations.push_back("DCO out of range");
  }

  uint8_t txAddress_ = 0;
  std::vector<uint8_t> tx_;
  std::vector<uint8_t> rx_;
  size_t rxPos_ = 0;
  uint8_t pointer_ = 0;
  double centreDco_ = 0;
};

inline TwoWire Wire;
