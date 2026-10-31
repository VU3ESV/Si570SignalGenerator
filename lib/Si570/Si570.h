/*
 * Si570 Library for Arduino
 *
 * MIT License
 *
 * Copyright Jeff Whitlatch - ko7m - 2014
 * Based on previous work by Thomas Sarlandie which was
 * based on previous work by Ashar Farhan
 *
 * ESP32 port / bug-fix copy of https://github.com/ko7m/Si570 (6ea9f47).
 * Every change is marked "ESP32 port:" - see README.md in this folder.
 */
#ifndef SI570_H
#define SI570_H

#include <stdint.h>

typedef enum
{
  SI570_ERROR = 0,
  SI570_READY,
  SI570_SUCCESS
} Si570_Status;

class Si570
{
public:
  // ESP32 port: the constructor no longer touches I2C (call begin() after Wire.begin()).
  // reg_base is 7 for ±50/±20 ppm parts (2nd option letter A/B) and 13 for ±7 ppm parts (C).
  Si570(uint8_t i2c_address, uint32_t calibration_frequency, uint8_t reg_base = 7);
  Si570_Status begin();
  Si570_Status setFrequency(uint32_t newfreq);
  void debugSi570();

  // ESP32 port: crystal frequency access, used for external-reference calibration.
  uint32_t getFrequency() const { return frequency; }
  uint32_t getXtalFrequency() const { return freq_xtal; }
  uint32_t getFactoryXtalFrequency() const { return factory_xtal; }
  Si570_Status setXtalFrequency(uint32_t fxtal);

  Si570_Status status;

private:
  uint8_t i2c_address;
  uint8_t reg_base;
  uint32_t calibration_frequency;
  uint8_t dco_reg[13];
  uint32_t f_center;
  uint32_t frequency;
  uint16_t hs, n1;
  uint32_t freq_xtal;
  uint32_t factory_xtal;
  uint64_t fdco;
  uint64_t rfreq;
  uint32_t max_delta;

  uint8_t i2c_read(uint8_t reg_address);
  int i2c_read(uint8_t reg_address, uint8_t *output, uint8_t length);

  bool i2c_write(uint8_t reg_address, uint8_t data);
  int i2c_write(uint8_t reg_address, uint8_t *data, uint8_t length);

  bool read_si570();
  bool write_si570();
  bool qwrite_si570();

  uint8_t getHSDIV();
  uint8_t getN1();
  uint64_t getRFREQ();

  void setRFREQ(uint32_t fnew);
  int findDivisors(uint32_t f);
  bool dcoInRange(uint32_t f) const;
  Si570_Status invalidate();
};

#endif
