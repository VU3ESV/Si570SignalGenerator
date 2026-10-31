// Wi-Fi, mDNS and ArduinoOTA, following the SO2R/BPF controller:
//
//  - With an SSID configured: join it as a station. Connecting doesn't block, so the
//    generator works straight away. After cfg::kWifiConnectTimeoutMs without a
//    connection, fall back to the set-up hotspot.
//  - Without one: open a hotspot "Si570-Setup-XXXX" with a captive DNS so any URL
//    lands on the web page at http://192.168.4.1/.
//  - Once joined: mDNS (http://<hostname>.local/, service _si570-siggen._tcp) and
//    ArduinoOTA on UDP 3232, protected by the admin password.
#pragma once

#include <Arduino.h>
#include <DNSServer.h>

#include <functional>

#include "settings.h"

class Net {
 public:
  using EventFn = std::function<void(const char* text)>;

  // onEvent gets short, display-sized notices ("192.168.1.50", "OTA update...").
  void begin(const NetConfig& cfg, const String& adminPassword, EventFn onEvent);
  void loop(uint32_t nowMs);
  void setOtaPassword(const String& pass);

  bool apMode() const { return apMode_; }
  bool connected() const;
  String ip() const;
  String ssid() const;
  int rssi() const;
  const char* hostname() const { return cfg_.hostname; }
  const NetConfig& config() const { return cfg_; }  // as saved; changes apply after a reboot

 private:
  void startSta();
  void startAp();
  void onStaUp();
  void setupOta();

  NetConfig cfg_;
  String otaPassword_;
  EventFn onEvent_;
  DNSServer dns_;
  bool apMode_ = false;
  bool connecting_ = false;
  bool otaUp_ = false;
  uint32_t connectStartMs_ = 0;
  char apSsid_[32] = "";
};
