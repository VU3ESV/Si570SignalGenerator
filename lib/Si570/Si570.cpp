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

#include <Arduino.h>
#include <Wire.h>

#include "Si570.h"

// ESP32 port: removed unused IF_FREQ, debugx() and dump()

// Debug output
static void debug(char const *fmt, ... )
{
  char tmp[128]; // resulting string limited to 128 chars
  va_list args;
  va_start (args, fmt );
  vsnprintf(tmp, sizeof(tmp), fmt, args);
  va_end (args);
  Serial.println(tmp);
}

// ESP32 port: the constructor only stores settings. The original called Wire.begin() and
// talked to the chip here, which runs before setup() for a global object, and wrote
// digitalWrite(SDA/SCL, 0) to drop AVR pull-ups. Call Wire.begin(sda, scl) then begin().
Si570::Si570(uint8_t si570_address, uint32_t calibration_frequency, uint8_t register_base)
{
  i2c_address = si570_address;
  reg_base = register_base;
  this->calibration_frequency = calibration_frequency;
  status = SI570_ERROR;
  f_center = frequency = calibration_frequency;
  hs = n1 = 0;
  freq_xtal = factory_xtal = 114285000L;
  fdco = rfreq = 0;
  max_delta = 0;
  for (int i = 0; i < 13; i++)
    dco_reg[i] = 0;
}

// Initialize the Si570 and determine its internal crystal frequency given the default output frequency
Si570_Status Si570::begin()
{
  debug("Si570 init, calibration frequency = %lu", (unsigned long) calibration_frequency);

  // We are about the reset the Si570, so set the current and center frequency to the calibration frequency.
  f_center = frequency = calibration_frequency;

  max_delta = ((uint64_t) f_center * 10035LL / 10000LL) - f_center;

  // Force Si570 to reset to initial freq (RECALL: reload the factory NVM settings)
  debug("Resetting Si570");
  bool ok = i2c_write(135,0x01);
  delay(20);

  // ESP32 port: guard the division below against an all-zero read
  if (ok && read_si570() && getRFREQ() != 0)
  {
    debug("Successfully initialized Si570");
    // ESP32 port: start from the factory dividers. The original left hs/n1 unset, so a
    // first step within 3500 ppm of the start-up frequency computed RFREQ from 0 * 0.
    hs = getHSDIV();
    n1 = getN1();
    freq_xtal = (unsigned long) ((uint64_t) calibration_frequency * hs * n1 * (1L << 28) / getRFREQ());
    status = SI570_READY;
  }
  else
  {
    // Use the factory default if we were unable to talk to the chip
    freq_xtal = 114285000L;
    debug("Unable to properly initialize Si570");
    status = SI570_ERROR;
  }
  factory_xtal = freq_xtal;
  debug("freq_xtal = %lu", (unsigned long) freq_xtal);
  return status;
}

// Debug routine for examination of Si570 state
void Si570::debugSi570()
{
  debug(" --- Si570 Debug Info ---");
  debug("Status: %i", status);
  for (int i = 7; i < 13; i++) {
    debug("Register[%i] = %02x", i - 7 + reg_base, dco_reg[i]);
  }
  debug("HSDIV = %i, N1 = %i", getHSDIV(), getN1());
  debug("RFREQ (hex): %04lx%08lx", (unsigned long)(getRFREQ() >> 32), (unsigned long)(getRFREQ() & 0xffffffff));
}

// Return the 8 bit HSDIV value from register 7
uint8_t Si570::getHSDIV()
{
  uint8_t hs_reg_value = dco_reg[7] >> 5;
  return 4 + hs_reg_value;
}

// Compute and return the 8 bit N1 value from registers 7 and 8
uint8_t Si570::getN1()
{
  uint8_t n_reg_value = ((dco_reg[7] & 0x1F) << 2) + (dco_reg[8] >> 6);
  return n_reg_value + 1;
}

// Return 38 bit RFREQ value in a 64 bit integer
uint64_t Si570::getRFREQ()
{
  fdco  = (uint64_t)(dco_reg[8] & 0x3F) << 32;
  fdco |= (uint64_t) dco_reg[9] << 24;
  fdco |= (uint64_t) dco_reg[10] << 16;
  fdco |= (uint64_t) dco_reg[11] << 8;
  fdco |= (uint64_t) dco_reg[12];

  return fdco;
}

// Write a byte to I2C device
// ESP32 port: report success so frequency updates can detect a missing chip
bool Si570::i2c_write(uint8_t reg_address, uint8_t data)
{
  Wire.beginTransmission(i2c_address);
  Wire.write(reg_address);
  Wire.write(data);
  return Wire.endTransmission() == 0;
}

// Write length bytes to I2C device.
int Si570::i2c_write(uint8_t reg_address, uint8_t *data, uint8_t length)
{
  Wire.beginTransmission(i2c_address);
  Wire.write(reg_address);
  Wire.write(data, length);

  int error = Wire.endTransmission();
  if (error != 0)
  {
    debug("Error writing %i bytes to register %i: %i", length, reg_address, error);
    return -1;
  }
  return length;
}

// Read a one byte register from the I2C device
uint8_t Si570::i2c_read(uint8_t reg_address)
{
  uint8_t rdata = 0xFF;
  // ESP32 port: removed a duplicated beginTransmission()/write() pair
  Wire.beginTransmission(i2c_address);
  Wire.write(reg_address);
  Wire.endTransmission();
  Wire.requestFrom(i2c_address, (uint8_t)1);
  if (Wire.available()) rdata = Wire.read();
  return rdata;
}

// Read multiple bytes fromt he I2C device
int Si570::i2c_read(uint8_t reg_address, uint8_t *output, uint8_t length) {
  Wire.beginTransmission(i2c_address);
  Wire.write(reg_address);

  int error = Wire.endTransmission();
  if (error != 0)
  {
    // ESP32 port: format string had three conversions but only two arguments
    debug("Error reading %i bytes from register %i. endTransmission() returned %i", length, reg_address, error);
    return 0;
  }

  int len = Wire.requestFrom(i2c_address,length);
  if (len != length)
  {
    debug("Requested %i bytes and only got %i bytes", length, len);
  }
  for (int i = 0; i < len && Wire.available(); i++)
    output[i] = Wire.read();

  return len;
}

// Read the Si570 chip and populate dco_reg values
bool Si570::read_si570(){
  // Try 3 times to read the registers
  for (int i = 0; i < 3; i++)
  {
    // we have to read six consecutive registers starting at register 7 (13 on 7 ppm parts)
    if (i2c_read(reg_base, &(dco_reg[7]), 6) == 6)
    {
      return true;
    }
    debug("Error reading Si570 registers... Retrying.");
    delay(50);
  }
  return false;
}

// Write dco_reg values to the Si570
// ESP32 port: returns false if any I2C write failed (all steps are still attempted so the
// DCO is never left frozen)
bool Si570::write_si570()
{
  int idco;
  bool ok;

  // Freeze DCO
  idco = i2c_read(137);
  ok = i2c_write(137, idco | 0x10 );

  ok = i2c_write(reg_base, &dco_reg[7], 6) == 6 && ok;

  // Unfreeze DCO
  ok = i2c_write(137, idco & 0xEF) && ok;

  // Set new freq
  ok = i2c_write(135,0x40) && ok;
  return ok;
}

// In the case of a frequency change < 3500 ppm, only RFREQ must change
bool Si570::qwrite_si570()
{
  int idco;
  bool ok;

  // Freeze the M Control Word to prevent interim frequency changes when writing RFREQ registers.
  idco = i2c_read(135);
  ok = i2c_write(135, idco | 0x20);

  // Write RFREQ registers
  ok = i2c_write(reg_base, &dco_reg[7], 6) == 6 && ok;

  // Unfreeze the M Control Word
  ok = i2c_write(135, idco &  0xdf) && ok;
  return ok;
}

// ESP32 port: limits in Hz (the originals were truncated kHz values)
static const uint64_t kDcoMinHz = 4850000000ULL;  // Minimum DCO frequency
static const uint64_t kDcoMaxHz = 5670000000ULL;  // Maximum DCO frequency

// ESP32 port: true if fout with the current dividers keeps the DCO inside its range
bool Si570::dcoInRange(uint32_t f) const
{
  uint64_t dco = (uint64_t) f * hs * n1;
  return dco >= kDcoMinHz && dco <= kDcoMaxHz;
}

// Locate an appropriate set of divisors (HSDiv and N1) give a desired output frequency
// ESP32 port: rewritten. The original only checked the 5.67 GHz ceiling, so ~16% of
// 10-160 MHz settings ran the DCO below its 4.85 GHz minimum, and it changed hs/n1 even
// when the search failed. The first pass prefers a DCO with room for the full ±3500 ppm
// glitch-free tuning window; the second pass accepts any in-range DCO.
int Si570::findDivisors(uint32_t fout)
{
  const uint16_t HS_DIV[] = {11, 9, 7, 6, 5, 4};
  const uint64_t limits[2][2] = {
    { kDcoMinHz * 10036 / 10000, kDcoMaxHz * 10000 / 10036 },
    { kDcoMinHz, kDcoMaxHz },
  };

  for (int pass = 0; pass < 2; ++pass)
  {
    // Lowest N1 with the highest HS_DIV uses the least power (datasheet section 3.1.2)
    for (uint16_t n = 1; n <= 128; n = (n == 1) ? 2 : n + 2)
    {
      for (int i = 0; i < 6; ++i)
      {
        uint64_t dco = (uint64_t) fout * HS_DIV[i] * n;
        if (dco >= limits[pass][0] && dco <= limits[pass][1])
        {
          hs = HS_DIV[i];
          n1 = n;
          return SI570_SUCCESS;
        }
      }
    }
  }
  return SI570_ERROR;
}

// Set RFREQ register (38 bits)
void Si570::setRFREQ(uint32_t fnew)
{
  // Calculate new DCO frequency
  fdco = (uint64_t) fnew * hs * n1;

  // Calculate the new RFREQ value, rounded to the nearest LSB
  // ESP32 port: the original "rfreq + ((rfreq & 1L<<(28-1))<<1)" added 1.0 (not one
  // LSB) to RFREQ whenever the fraction was >= 0.5, putting about half of all
  // frequencies off by up to 2.6 %.
  rfreq = ((fdco << 28) + freq_xtal / 2) / freq_xtal;

  // Reset all Si570 registers
  for (int i = 7; i <= 12; i++)
    dco_reg[i] = 0;

  // Set up the RFREQ register values
  dco_reg[12] = rfreq & 0xff;
  dco_reg[11] = rfreq >> 8 & 0xff;
  dco_reg[10] = rfreq >> 16 & 0xff;
  dco_reg[9]  = rfreq >> 24 & 0xff;
  dco_reg[8]  = rfreq >> 32 & 0x3f;

  // set up HS and N1 in registers 7 and 8
  dco_reg[7]  = (hs - 4) << 5;
  dco_reg[7]  = dco_reg[7] | ((n1 - 1) >> 2);
  dco_reg[8] |= ((n1-1) & 0x3) << 6;
  // ESP32 port: register dump is opt-in; ~8 serial lines per step slowed down tuning
#ifdef SI570_DEBUG
  debugSi570();
#endif
}

// Set the Si570 frequency
Si570_Status Si570::setFrequency(uint32_t newfreq)
{
  // If the current frequency has not changed, we are done
  if (frequency == newfreq)
    return status;

  // Check how far we have moved the frequency (without using abs() function)
  uint32_t delta_freq = newfreq < f_center ? f_center - newfreq : newfreq - f_center;

  // If the jump is small enough, we don't have to fiddle with the dividers
  // ESP32 port: ...as long as the DCO also stays in range with the current dividers
  if (delta_freq < max_delta && dcoInRange(newfreq))
  {
    setRFREQ(newfreq);
    frequency = newfreq;
    if (!qwrite_si570())
      return invalidate();
  }
  else
  {
    // otherwise it is a big jump and we need a new set of divisors and reset center frequency
    // ESP32 port: reject frequencies with no valid divider set instead of programming them
    if (findDivisors(newfreq) != SI570_SUCCESS)
      return SI570_ERROR;
    setRFREQ(newfreq);
    f_center = frequency = newfreq;
    // Calculate the new 3500 ppm delta
    max_delta = ((uint64_t) f_center * 10035LL / 10000LL) - f_center;
    if (!write_si570())
      return invalidate();
  }
  return status;
}

// ESP32 port: use a corrected crystal frequency (e.g. measured against an external
// reference) and re-derive RFREQ for the current output. The DCO target (fout*HS*N1)
// does not change, so a correction of a few ppm stays on the glitch-free Freeze-M path.
Si570_Status Si570::setXtalFrequency(uint32_t fxtal)
{
  if (fxtal == 0)
    return SI570_ERROR;

  uint32_t old_xtal = freq_xtal;
  freq_xtal = fxtal;
  if (status != SI570_READY || frequency == 0)
    return status;

  uint32_t diff = fxtal > old_xtal ? fxtal - old_xtal : old_xtal - fxtal;
  // A large correction, or still sitting on factory dividers with an out-of-spec DCO
  // (e.g. 10 MHz parts ship with a 4.80 GHz DCO): redo the divider search and DCO freeze
  if ((uint64_t) diff * 1000000ULL / old_xtal >= 1000 || !dcoInRange(frequency))
  {
    uint32_t f = frequency;
    invalidate();
    return setFrequency(f);
  }

  setRFREQ(frequency);
  if (!qwrite_si570())
    return invalidate();
  return status;
}

// ESP32 port: after a failed write the chip state is unknown, so force the next
// setFrequency() through the full freeze/NewFreq sequence.
Si570_Status Si570::invalidate()
{
  frequency = f_center = max_delta = 0;
  return SI570_ERROR;
}
