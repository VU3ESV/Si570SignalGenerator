#include "web_portal.h"

#include <Update.h>
#include <WiFi.h>
#include <stdlib.h>

#include "settings.h"

// web/index.html, linked in by board_build.embed_txtfiles (NUL-terminated).
extern const char kIndexHtml[] asm("_binary_web_index_html_start");

namespace {

constexpr const char* kAuthRealm = "Si570SignalGenerator";
constexpr uint32_t kBroadcastMs = 250;

const char* b(bool v) { return v ? "true" : "false"; }

// Escape a C string for use inside a JSON string literal.
String jsonEsc(const char* s) {
  String o;
  for (; *s; ++s) {
    switch (*s) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default:
        if ((unsigned char)*s < 0x20) {
          char buf[7];
          snprintf(buf, sizeof buf, "\\u%04x", (unsigned char)*s);
          o += buf;
        } else {
          o += *s;
        }
    }
  }
  return o;
}

}  // namespace

void WebPortal::begin(Controller& ctl, Net& net, const String& adminPassword, EventFn onEvent) {
  ctl_ = &ctl;
  net_ = &net;
  adminPass_ = adminPassword;
  onEvent_ = onEvent;

  server_.on("/", HTTP_GET, [this]() { handleRoot(); });
  server_.on("/status", HTTP_GET, [this]() { handleStatus(); });
  server_.on("/config", HTTP_GET, [this]() { handleConfig(); });
  server_.on("/discover", HTTP_GET, [this]() { handleDiscover(); });
  server_.on("/api/freq", HTTP_POST, [this]() { handleFreq(); });
  server_.on("/api/step", HTTP_POST, [this]() { handleStep(); });
  server_.on("/api/ref", HTTP_POST, [this]() { handleRef(); });
  server_.on("/api/rf", HTTP_POST, [this]() { handleRf(); });
  server_.on("/api/output", HTTP_POST, [this]() { handleOutput(); });
  server_.on("/api/drive", HTTP_POST, [this]() { handleDrive(); });
  server_.on("/api/calibrate", HTTP_POST, [this]() {
    if (requireAdmin()) sendResult(ctl_->calibrate());
  });
  server_.on("/api/save", HTTP_POST, [this]() {
    if (requireAdmin()) sendResult(ctl_->saveState());
  });
  server_.on("/save/hardware", HTTP_POST, [this]() { handleSaveHardware(); });
  server_.on("/save/network", HTTP_POST, [this]() { handleSaveNetwork(); });
  server_.on("/save/admin", HTTP_POST, [this]() { handleSaveAdmin(); });
  server_.on("/update", HTTP_POST, [this]() { handleUpdateDone(); }, [this]() { handleUpdateUpload(); });
  server_.on("/reboot", HTTP_POST, [this]() { handleReboot(); });
  server_.on("/factory_reset", HTTP_POST, [this]() { handleFactoryReset(); });
  server_.onNotFound([this]() { handleNotFound(); });
  server_.begin();

  ws_.begin();
  ws_.onEvent([this](uint8_t num, WStype_t type, uint8_t*, size_t) {
    if (type == WStype_CONNECTED) {
      String s = statusJson();
      ws_.sendTXT(num, s);  // paint the page straight away
    }
  });
  Serial.printf("[web] http on :%u, live feed on ws :%u, admin password %s\n", cfg::kHttpPort, cfg::kWsPort,
                adminPass_.length() ? "set" : "NOT set");
}

void WebPortal::loop(uint32_t nowMs) {
  server_.handleClient();
  ws_.loop();
  if (nowMs - lastBroadcastMs_ >= kBroadcastMs) {
    lastBroadcastMs_ = nowMs;
    if (ws_.connectedClients() > 0) {
      String s = statusJson();
      ws_.broadcastTXT(s);
    }
  }
}

// ---------------------------------------------------------------- JSON

String WebPortal::statusJson() const {
  const UiModel& ui = ctl_->ui();
  const Synth& s = ctl_->synth();
  const RefController& r = ctl_->ref();
  const HardwareConfig& hw = ctl_->hardware();

  const ClockGen& c = ctl_->clock();

  String j;
  j.reserve(1900);
  j += "{\"version\":\"";
  j += cfg::kFirmwareVersion;
  j += "\",\"build\":\"";
  j += cfg::kFirmwareBuild;
  j += "\",\"uptime_s\":";
  j += millis() / 1000;
  // Top-level freq/limits/RF describe the Si570 (output 0), as before the Si5351 existed.
  j += ",\"freq_hz\":";
  j += ui.frequency(kSi570Output);
  j += ",\"step_index\":";
  j += ui.stepIndex();
  j += ",\"max_step_index\":";
  j += ui.maxStepIndex();
  j += ",\"min_hz\":";
  j += hw.minHz;
  j += ",\"max_hz\":";
  j += hw.maxHz;
  j += ",\"has_rf\":";
  j += b(ui.output(kSi570Output).hasRf);
  j += ",\"rf_on\":";
  j += b(ui.rfOn(kSi570Output));
  j += ",\"selected\":";
  j += ui.selected();
  j += ",\"outputs\":[";
  for (uint8_t i = 0; i < kMaxOutputs; ++i) {
    const OutputConfig& oc = ui.output(i);
    if (i) j += ',';
    j += "{\"name\":\"";
    j += Controller::outputName(i);
    j += "\",\"fitted\":";
    j += b(oc.fitted);
    j += ",\"ready\":";
    j += b(ctl_->outputReady(i));
    j += ",\"freq_hz\":";
    j += ui.frequency(i);
    j += ",\"min_hz\":";
    j += oc.minHz;
    j += ",\"max_hz\":";
    j += oc.maxHz;
    j += ",\"has_rf\":";
    j += b(oc.hasRf);
    j += ",\"rf_on\":";
    j += b(ui.rfOn(i));
    j += ",\"drive_ma\":";
    j += ctl_->driveMa(i);
    j += '}';
  }
  j += ']';
  j += ",\"has_ref\":";
  j += b(ctl_->hasRef());
  j += ",\"ref\":\"";
  j += ui.ref() == RefMode::External ? "ext" : "int";
  j += "\",\"ref_status\":\"";
  j += refStatusLabel(r.status());
  j += "\",\"ref_err_ppm\":";
  j += String(r.errorPpm(), 3);
  j += ",\"ref_hz\":";
  j += hw.refHz;
  j += ",\"calibrating\":";
  j += b(r.calibrating());
  j += ",\"has_encoder\":";
  j += b(ctl_->hasEncoder());
  j += ",\"si570\":{\"ready\":";
  j += b(s.ready());
  j += ",\"address\":";
  j += s.address();
  j += ",\"xtal_hz\":";
  j += s.xtal();
  j += ",\"factory_xtal_hz\":";
  j += s.factoryXtal();
  j += ",\"ext_xtal_hz\":";
  j += r.extXtal();
  j += ",\"plausible\":";
  j += b(s.xtalPlausible());
  j += ",\"likely_startup_hz\":";
  j += s.xtalPlausible() ? 0 : s.likelyStartupHz();
  j += ",\"corr_ppb\":";
  j += hw.si570CorrPpb;
  j += "},\"si5351\":{\"enabled\":";
  j += b(c.enabled());
  j += ",\"ready\":";
  j += b(c.ready());
  j += ",\"address\":";
  j += c.address();
  j += ",\"xtal_hz\":";
  j += c.xtal();
  j += ",\"corr_ppb\":";
  j += c.correctionPpb();
  j += "},\"message\":\"";
  j += jsonEsc(ctl_->statusMessage());
  j += "\",\"wifi\":{\"mode\":\"";
  j += net_->apMode() ? "ap" : "sta";
  j += "\",\"connected\":";
  j += b(net_->connected());
  j += ",\"ssid\":\"";
  j += jsonEsc(net_->ssid().c_str());
  j += "\",\"ip\":\"";
  j += net_->ip();
  j += "\",\"rssi\":";
  j += net_->rssi();
  j += ",\"hostname\":\"";
  j += jsonEsc(net_->hostname());
  j += "\"},\"auth_required\":";
  j += b(adminPass_.length() > 0);
  j += "}";
  return j;
}

// GET /config: the stored settings, from the same structs the firmware uses. The Wi-Fi
// and admin passwords are never included.
void WebPortal::handleConfig() {
  const HardwareConfig& hw = ctl_->hardware();
  const NetConfig& net = net_->config();
  const UiModel& ui = ctl_->ui();
  String j;
  j.reserve(500);
  j += "{\"version\":\"";
  j += cfg::kFirmwareVersion;
  j += "\",\"hostname\":\"";
  j += jsonEsc(net.hostname);
  j += "\",\"ssid\":\"";
  j += jsonEsc(net.ssid);
  j += "\",\"hardware\":{\"si570_address\":";
  j += hw.si570Address;
  j += ",\"startup_hz\":";
  j += hw.si570StartupHz;
  j += ",\"reg_base\":";
  j += hw.si570RegBase;
  j += ",\"min_hz\":";
  j += hw.minHz;
  j += ",\"max_hz\":";
  j += hw.maxHz;
  j += ",\"oe_active_high\":";
  j += b(hw.oeActiveHigh);
  j += ",\"ref_hz\":";
  j += hw.refHz;
  j += ",\"si570_corr_ppb\":";
  j += hw.si570CorrPpb;
  j += ",\"s5_enabled\":";
  j += b(hw.s5Enabled);
  j += ",\"s5_address\":";
  j += hw.s5Address;
  j += ",\"s5_xtal_hz\":";
  j += hw.s5XtalHz;
  j += ",\"s5_corr_ppb\":";
  j += hw.s5CorrPpb;
  j += ",\"s5_min_hz\":";
  j += hw.s5MinHz;
  j += ",\"s5_max_hz\":";
  j += hw.s5MaxHz;
  j += "},\"state\":{\"freq_hz\":";
  j += ui.frequency(kSi570Output);
  j += ",\"step_index\":";
  j += ui.stepIndex();
  j += ",\"ref\":\"";
  j += ui.ref() == RefMode::External ? "ext" : "int";
  j += "\",\"rf_on\":";
  j += b(ui.rfOn(kSi570Output));
  j += ",\"selected\":";
  j += ui.selected();
  j += ",\"outputs\":[";
  for (uint8_t i = 0; i < kMaxOutputs; ++i) {
    if (i) j += ',';
    j += "{\"freq_hz\":";
    j += ui.frequency(i);
    j += ",\"rf_on\":";
    j += b(ui.rfOn(i));
    j += ",\"drive_ma\":";
    j += ctl_->driveMa(i);
    j += '}';
  }
  j += "]}}";
  if (server_.arg("download") == "1") {
    server_.sendHeader("Content-Disposition",
                       String("attachment; filename=\"si570-siggen-") + net.hostname + ".json\"");
  }
  sendJson(200, j);
}

void WebPortal::handleDiscover() {
  String j;
  j.reserve(400);
  j += "{\"service\":\"si570-siggen\",\"vendor\":\"VU3ESV\",\"product\":\"Si570SignalGenerator\",\"version\":\"";
  j += cfg::kFirmwareVersion;
  j += "\",\"build\":\"";
  j += cfg::kFirmwareBuild;
  j += "\",\"hostname\":\"";
  j += jsonEsc(net_->hostname());
  j += "\",\"ip\":\"";
  j += net_->ip();
  j += "\",\"auth_required\":";
  j += b(adminPass_.length() > 0);
  j += ",\"ports\":{\"http\":";
  j += cfg::kHttpPort;
  j += ",\"ws_live\":";
  j += cfg::kWsPort;
  j += ",\"ota\":3232},\"endpoints\":{\"page\":\"/\",\"status\":\"/status\",\"config\":\"/config\","
       "\"freq\":\"/api/freq\",\"step\":\"/api/step\",\"ref\":\"/api/ref\",\"rf\":\"/api/rf\","
       "\"output\":\"/api/output\",\"drive\":\"/api/drive\",\"calibrate\":\"/api/calibrate\","
       "\"save\":\"/api/save\",\"update\":\"/update\"}}";
  sendJson(200, j);
}

// ---------------------------------------------------------------- helpers

// Digest only: WebServer would also accept Basic credentials, which carry the password
// in clear, so the callback refuses any other scheme.
bool WebPortal::isAdmin() {
  if (adminPass_.length() == 0) return true;
  return server_.authenticate([this](HTTPAuthMethod mode, String user, String params[]) -> String* {
    (void)params;
    if (mode != DIGEST_AUTH) return nullptr;
    return user.equalsConstantTime(kAdminUser) ? new String(adminPass_) : nullptr;
  });
}

// Returns true when the request may proceed; otherwise a 401 challenge has been sent.
// The browser answers it with the credentials it cached when the page was opened.
bool WebPortal::requireAdmin() {
  if (isAdmin()) return true;
  server_.requestAuthentication(DIGEST_AUTH, kAuthRealm, "admin password required\n");
  return false;
}

bool WebPortal::argUInt(const char* name, uint32_t& out) {
  if (!server_.hasArg(name)) return false;
  const String v = server_.arg(name);
  if (v.length() == 0) return false;
  char* end = nullptr;
  const unsigned long long n = strtoull(v.c_str(), &end, 0);  // accepts 0x.. as well
  if (*end != '\0' || v[0] == '-' || n > 0xFFFFFFFFULL) return false;
  out = (uint32_t)n;
  return true;
}

bool WebPortal::argInt(const char* name, int32_t& out) {
  if (!server_.hasArg(name)) return false;
  const String v = server_.arg(name);
  if (v.length() == 0) return false;
  char* end = nullptr;
  const long long n = strtoll(v.c_str(), &end, 10);
  if (*end != '\0' || n < INT32_MIN || n > INT32_MAX) return false;
  out = (int32_t)n;
  return true;
}

// "output" is optional and defaults to the Si570 (output 0), as before the Si5351 existed.
bool WebPortal::argOutput(uint8_t& out) {
  out = kSi570Output;
  if (!server_.hasArg("output")) return true;
  uint32_t v = 0;
  if (!argUInt("output", v) || v >= kMaxOutputs) return false;
  out = (uint8_t)v;
  return true;
}

void WebPortal::sendJson(int code, const String& body) {
  server_.sendHeader("Cache-Control", "no-store");
  server_.send(code, "application/json", body);
}

void WebPortal::sendError(int code, const String& message) {
  sendJson(code, "{\"ok\":false,\"error\":\"" + jsonEsc(message.c_str()) + "\",\"status\":" + statusJson() + "}");
}

void WebPortal::sendResult(CmdResult r) {
  if (r == CmdResult::Ok) {
    sendJson(200, "{\"ok\":true,\"status\":" + statusJson() + "}");
    return;
  }
  int code = 500;
  switch (r) {
    case CmdResult::OutOfRange:
    case CmdResult::Unsupported: code = 400; break;
    case CmdResult::Busy:
    case CmdResult::PllInUse: code = 409; break;
    case CmdResult::NotReady: code = 503; break;
    default: break;
  }
  sendError(code, cmdResultText(r));
}

// ---------------------------------------------------------------- routes

void WebPortal::handleRoot() {
  if (!requireAdmin()) return;
  server_.sendHeader("Cache-Control", "no-cache");  // a firmware update also updates the page
  server_.send_P(200, "text/html", kIndexHtml);
}

void WebPortal::handleStatus() { sendJson(200, statusJson()); }

void WebPortal::handleFreq() {
  if (!requireAdmin()) return;
  uint32_t hz = 0;
  uint8_t output = 0;
  if (!argUInt("hz", hz)) return sendError(400, "need hz=<integer Hz>");
  if (!argOutput(output)) return sendError(400, "output must be 0-3");
  sendResult(ctl_->setFrequency(output, hz));
}

void WebPortal::handleStep() {
  if (!requireAdmin()) return;
  uint32_t index = 0;
  if (!argUInt("index", index) || index >= UiModel::kStepCount) return sendError(400, "need index=0..8");
  sendResult(ctl_->setStepIndex((int)index));
}

void WebPortal::handleRef() {
  if (!requireAdmin()) return;
  const String mode = server_.arg("mode");
  if (mode != "int" && mode != "ext") return sendError(400, "need mode=int|ext");
  sendResult(ctl_->setRefMode(mode == "ext" ? RefMode::External : RefMode::Internal));
}

void WebPortal::handleRf() {
  if (!requireAdmin()) return;
  const String on = server_.arg("on");
  uint8_t output = 0;
  if (on != "1" && on != "0") return sendError(400, "need on=1|0");
  if (!argOutput(output)) return sendError(400, "output must be 0-3");
  sendResult(ctl_->setRfEnabled(output, on == "1"));
}

void WebPortal::handleOutput() {
  if (!requireAdmin()) return;
  uint32_t v = 0;
  if (!argUInt("output", v) || v >= kMaxOutputs) return sendError(400, "need output=0-3");
  sendResult(ctl_->selectOutput((uint8_t)v));
}

void WebPortal::handleDrive() {
  if (!requireAdmin()) return;
  uint32_t output = 0, ma = 0;
  if (!argUInt("output", output) || output < 1 || output >= kMaxOutputs) return sendError(400, "need output=1-3");
  if (!argUInt("ma", ma) || !ClockGen::validDrive((uint8_t)ma) || ma > 8) return sendError(400, "need ma=2|4|6|8");
  sendResult(ctl_->setDrive((uint8_t)output, (uint8_t)ma));
}

void WebPortal::handleSaveHardware() {
  if (!requireAdmin()) return;
  HardwareConfig hw = ctl_->hardware();
  uint32_t v = 0;
  // Every field is optional; missing ones keep their current value.
  if (argUInt("si570_address", v)) hw.si570Address = v > 0xFF ? 0 : (uint8_t)v;
  if (argUInt("startup_hz", v)) hw.si570StartupHz = v;
  if (argUInt("reg_base", v)) hw.si570RegBase = v > 0xFF ? 0 : (uint8_t)v;
  if (argUInt("min_hz", v)) hw.minHz = v;
  if (argUInt("max_hz", v)) hw.maxHz = v;
  if (argUInt("oe_active_high", v)) hw.oeActiveHigh = v != 0;
  if (argUInt("ref_hz", v)) hw.refHz = v;
  if (argUInt("s5_enabled", v)) hw.s5Enabled = v != 0;
  if (argUInt("s5_address", v)) hw.s5Address = v > 0xFF ? 0 : (uint8_t)v;
  if (argUInt("s5_xtal_hz", v)) hw.s5XtalHz = v;
  if (argUInt("s5_min_hz", v)) hw.s5MinHz = v;
  if (argUInt("s5_max_hz", v)) hw.s5MaxHz = v;
  int32_t corr = 0;
  if (server_.hasArg("si570_corr_ppb") && !argInt("si570_corr_ppb", corr)) return sendError(400, "si570_corr_ppb: integer ppb");
  if (server_.hasArg("si570_corr_ppb")) hw.si570CorrPpb = corr;
  if (server_.hasArg("s5_corr_ppb") && !argInt("s5_corr_ppb", corr)) return sendError(400, "s5_corr_ppb: integer ppb");
  if (server_.hasArg("s5_corr_ppb")) hw.s5CorrPpb = corr;
  if (const char* why = validateHardwareConfig(hw)) return sendError(400, why);
  sendResult(ctl_->applyHardware(hw));
}

void WebPortal::handleSaveNetwork() {
  if (!requireAdmin()) return;
  NetConfig net = net_->config();
  // Missing fields keep their stored value (an empty ssid field means "use the hotspot").
  const String ssid = server_.hasArg("ssid") ? server_.arg("ssid") : String(net.ssid);
  const String pass = server_.arg("pass");
  const String host = server_.hasArg("hostname") ? server_.arg("hostname") : String(net.hostname);
  if (ssid.length() > 32) return sendError(400, "SSID is at most 32 characters");
  if (pass.length() > 0 && (pass.length() < 8 || pass.length() > 63))
    return sendError(400, "Wi-Fi password must be 8-63 characters");
  if (!validHostname(host.c_str())) return sendError(400, "hostname: 1-31 letters, digits or '-'");
  strncpy(net.ssid, ssid.c_str(), sizeof(net.ssid) - 1);
  net.ssid[sizeof(net.ssid) - 1] = '\0';
  // The page never receives the stored password, so blank means "keep the current one".
  if (pass.length() > 0) {
    strncpy(net.pass, pass.c_str(), sizeof(net.pass) - 1);
    net.pass[sizeof(net.pass) - 1] = '\0';
  }
  if (ssid.length() == 0) net.pass[0] = '\0';  // no network = set-up hotspot after reboot
  strncpy(net.hostname, host.c_str(), sizeof(net.hostname) - 1);
  net.hostname[sizeof(net.hostname) - 1] = '\0';
  saveNetConfig(net);
  sendJson(200, "{\"ok\":true,\"reboot_required\":true}");
}

void WebPortal::handleSaveAdmin() {
  if (!requireAdmin()) return;
  const String pass = server_.arg("admin_pass");
  if (pass.length() == 0 || pass.length() > kAdminPassMax) return sendError(400, "password must be 1-64 characters");
  saveAdminPassword(pass);
  adminPass_ = pass;               // web: effective immediately
  net_->setOtaPassword(pass);      // ArduinoOTA: likewise
  sendJson(200, "{\"ok\":true}");
}

// Multipart body of POST /update, delivered in chunks before handleUpdateDone() runs.
// An unauthorised upload is read and discarded; handleUpdateDone() then sends the 401
// and the browser repeats the upload with the admin credentials.
void WebPortal::handleUpdateUpload() {
  HTTPUpload& up = server_.upload();
  switch (up.status) {
    case UPLOAD_FILE_START:
      uploadOk_ = false;
      uploadAuthorized_ = isAdmin();
      if (!uploadAuthorized_) return;
      Serial.printf("[update] receiving %s\n", up.filename.c_str());
      if (onEvent_) onEvent_("Web update...");
      if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) Update.printError(Serial);
      break;
    case UPLOAD_FILE_WRITE:
      if (uploadAuthorized_ && Update.isRunning() && Update.write(up.buf, up.currentSize) != up.currentSize)
        Update.printError(Serial);
      break;
    case UPLOAD_FILE_END:
      if (!uploadAuthorized_ || !Update.isRunning()) break;
      uploadOk_ = Update.end(true);  // true: size was unknown, accept what arrived
      if (uploadOk_)
        Serial.printf("[update] %u bytes written\n", (unsigned)up.totalSize);
      else
        Update.printError(Serial);
      break;
    case UPLOAD_FILE_ABORTED:
      if (uploadAuthorized_ && Update.isRunning()) Update.abort();
      Serial.println("[update] upload aborted");
      break;
  }
}

void WebPortal::handleUpdateDone() {
  if (!requireAdmin()) return;
  if (!uploadOk_) {
    if (onEvent_) onEvent_("Update failed");
    return sendError(500, String("update failed: ") + Update.errorString());
  }
  sendJson(200, "{\"ok\":true,\"rebooting\":true}");
  if (onEvent_) onEvent_("Update OK, reboot");
  delay(500);
  ESP.restart();
}

void WebPortal::handleReboot() {
  if (!requireAdmin()) return;
  sendJson(200, "{\"ok\":true,\"rebooting\":true}");
  delay(200);
  ESP.restart();
}

void WebPortal::handleFactoryReset() {
  if (!requireAdmin()) return;
  if (server_.arg("confirm") != "YES") return sendError(400, "confirm=YES required");
  factoryReset();
  sendJson(200, "{\"ok\":true,\"rebooting\":true}");
  delay(200);
  ESP.restart();
}

// On the set-up hotspot every unknown URL (phones probing for a captive portal) is sent
// to the page, so it pops up by itself.
void WebPortal::handleNotFound() {
  if (net_->apMode()) {
    server_.sendHeader("Location", String("http://") + net_->ip() + "/");
    server_.send(302, "text/plain", "");
    return;
  }
  server_.send(404, "text/plain", "not found");
}
