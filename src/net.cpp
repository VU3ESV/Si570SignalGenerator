#include "net.h"

#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include "config.h"

void Net::begin(const NetConfig& cfg, const String& adminPassword, EventFn onEvent) {
  cfg_ = cfg;
  otaPassword_ = adminPassword;
  onEvent_ = onEvent;
  if (cfg_.ssid[0] != '\0')
    startSta();
  else
    startAp();
}

void Net::loop(uint32_t nowMs) {
  if (apMode_) {
    dns_.processNextRequest();
    return;
  }
  if (connecting_) {
    if (WiFi.status() == WL_CONNECTED) {
      connecting_ = false;
      onStaUp();
    } else if (nowMs - connectStartMs_ >= cfg::kWifiConnectTimeoutMs) {
      connecting_ = false;
      Serial.printf("[wifi] could not join '%s'; falling back to the set-up hotspot\n", cfg_.ssid);
      WiFi.disconnect(true);
      startAp();
    }
    return;
  }
  if (otaUp_) ArduinoOTA.handle();
}

void Net::startSta() {
  // Setting the hostname right after WiFi.mode() races with the netif being created,
  // so routers log "esp32-xxxx". Re-applying it on STA_START is the reliable pattern.
  WiFi.onEvent([this](arduino_event_id_t, arduino_event_info_t) { WiFi.setHostname(cfg_.hostname); },
               ARDUINO_EVENT_WIFI_STA_START);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(cfg_.hostname);
  // With only an IPv4 address, mDNS answers A but never AAAA, and macOS then waits 5 s
  // on every "Si570-SigGen.local" lookup. A link-local IPv6 address lets mDNS answer
  // both. The web and WebSocket servers already listen on IPv4 and IPv6.
  WiFi.enableIPv6();
  WiFi.begin(cfg_.ssid, cfg_.pass);
  connecting_ = true;
  connectStartMs_ = millis();
  Serial.printf("[wifi] joining '%s' as %s\n", cfg_.ssid, cfg_.hostname);
}

void Net::startAp() {
  apMode_ = true;
  WiFi.mode(WIFI_AP);
  // Name it after the last two MAC bytes, the per-chip part (e8:db:84:0c:91:d0 -> "91D0").
  // getEfuseMac() keeps the first MAC byte in its low bits, so its low bits are
  // Espressif's vendor prefix, shared by many boards. 4 hex digits keep the name within
  // the 16-character LCD.
  const uint64_t mac = ESP.getEfuseMac();
  const unsigned suffix = (unsigned)((((mac >> 32) & 0xFF) << 8) | ((mac >> 40) & 0xFF));
  snprintf(apSsid_, sizeof apSsid_, "%s%04X", cfg::kApSsidPrefix, suffix);
  WiFi.softAP(apSsid_);
  const IPAddress ip = WiFi.softAPIP();
  dns_.start(53, "*", ip);
  Serial.printf("[wifi] set-up hotspot '%s', open http://%s/\n", apSsid_, ip.toString().c_str());
  if (onEvent_) onEvent_(apSsid_);
}

void Net::onStaUp() {
  const String addr = WiFi.localIP().toString();
  Serial.printf("[wifi] joined, http://%s/  rssi %d dBm\n", addr.c_str(), WiFi.RSSI());
  if (MDNS.begin(cfg_.hostname)) {
    MDNS.addService("http", "tcp", cfg::kHttpPort);
    // Discovery: browse _si570-siggen._tcp to find every generator on the LAN. The TXT
    // records only identify the device; live state is at /status.
    MDNS.addService("si570-siggen", "tcp", cfg::kHttpPort);
    MDNS.addServiceTxt("si570-siggen", "tcp", "vendor", "VU3ESV");
    MDNS.addServiceTxt("si570-siggen", "tcp", "product", "Si570SignalGenerator");
    MDNS.addServiceTxt("si570-siggen", "tcp", "version", cfg::kFirmwareVersion);
    MDNS.addServiceTxt("si570-siggen", "tcp", "path", "/discover");
    MDNS.addServiceTxt("si570-siggen", "tcp", "ws_live", String(cfg::kWsPort));
    Serial.printf("[mdns] http://%s.local/\n", cfg_.hostname);
  }
  setupOta();
  if (onEvent_) onEvent_(addr.c_str());
}

// ArduinoOTA ("espota", UDP 3232): `pio run -e esp32-ota -t upload`. Uses the admin
// password; with none set, anyone on the LAN can replace the firmware, so set one.
void Net::setupOta() {
  ArduinoOTA.setHostname(cfg_.hostname);
  if (otaPassword_.length() > 0) ArduinoOTA.setPassword(otaPassword_.c_str());
  ArduinoOTA.onStart([this]() {
    Serial.println("[ota] update starting");
    if (onEvent_) onEvent_("OTA update...");
  });
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    static int lastPct = -1;
    const int pct = (int)((done * 100UL) / total);
    if (pct != lastPct && pct % 10 == 0) {
      Serial.printf("[ota] %d%%\n", pct);
      lastPct = pct;
    }
  });
  ArduinoOTA.onEnd([]() { Serial.println("[ota] complete, rebooting"); });
  ArduinoOTA.onError([this](ota_error_t err) {
    Serial.printf("[ota] error %u\n", err);
    if (onEvent_) onEvent_("OTA failed");
  });
  ArduinoOTA.begin();
  otaUp_ = true;
  Serial.printf("[ota] ready at %s.local:3232 (password %s)\n", cfg_.hostname,
                otaPassword_.length() ? "set" : "NOT set");
}

void Net::setOtaPassword(const String& pass) {
  otaPassword_ = pass;
  if (otaUp_) ArduinoOTA.setPassword(pass.c_str());
}

bool Net::connected() const { return !apMode_ && WiFi.status() == WL_CONNECTED; }

String Net::ip() const { return apMode_ ? WiFi.softAPIP().toString() : WiFi.localIP().toString(); }

String Net::ssid() const { return apMode_ ? String(apSsid_) : String(cfg_.ssid); }

int Net::rssi() const { return connected() ? WiFi.RSSI() : 0; }
