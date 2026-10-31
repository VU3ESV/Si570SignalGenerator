// Web control page and JSON API, following the SO2R/BPF controller's WebPortal.
//
//   GET  /                 control page (web/index.html, embedded in the firmware)
//   GET  /status           live state as JSON           (open, read-only)
//   GET  /config           settings as JSON, no secrets (open; ?download=1 for a backup file)
//   GET  /discover         identity for discovery tools (open)
//   POST /api/freq         hz=<Hz> [output=0-3]   (0 = Si570, the default; 1-3 = Si5351 CLK0-2)
//   POST /api/step         index=<0..8>   (1 Hz ... 100 MHz)
//   POST /api/ref          mode=int|ext   (the Si570's reference)
//   POST /api/rf           on=1|0 [output=0-3]
//   POST /api/output       output=0-3     select the output the encoder and display work on
//   POST /api/drive        output=1-3 ma=2|4|6|8   Si5351 drive strength
//   POST /api/calibrate    calibrate against the external reference now
//   POST /api/save         save the current state as the power-on default
//   POST /save/hardware    Si570 part, frequency limits, OE polarity, reference frequency,
//                          si570_corr_ppb (internal-reference correction);
//                          Si5351 s5_enabled, s5_address, s5_xtal_hz, s5_corr_ppb, s5_min_hz, s5_max_hz
//   POST /save/network     ssid, pass (blank = keep), hostname; applies after a reboot
//   POST /save/admin       admin_pass
//   POST /update           firmware .bin upload (multipart), reboots when done
//   POST /reboot
//   POST /factory_reset    confirm=YES
//   ws://<host>:81/        the /status JSON pushed every 250 ms (read-only)
//
// Once an admin password is set, GET / and every POST need HTTP Digest auth as user
// "admin" (Basic is refused: it would send the password in clear). The read-only
// routes stay open so status views and discovery keep working.
#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

#include <functional>

#include "config.h"
#include "controller.h"
#include "net.h"

class WebPortal {
 public:
  using EventFn = std::function<void(const char* text)>;

  // onEvent gets display-sized notices ("Web update...").
  void begin(Controller& ctl, Net& net, const String& adminPassword, EventFn onEvent);
  void loop(uint32_t nowMs);

  String statusJson() const;

 private:
  bool isAdmin();
  bool requireAdmin();
  bool argUInt(const char* name, uint32_t& out);
  bool argInt(const char* name, int32_t& out);
  bool argOutput(uint8_t& out);
  void sendJson(int code, const String& body);
  void sendError(int code, const String& message);
  void sendResult(CmdResult r);

  void handleRoot();
  void handleStatus();
  void handleConfig();
  void handleDiscover();
  void handleFreq();
  void handleStep();
  void handleRef();
  void handleRf();
  void handleOutput();
  void handleDrive();
  void handleSaveHardware();
  void handleSaveNetwork();
  void handleSaveAdmin();
  void handleUpdateUpload();
  void handleUpdateDone();
  void handleReboot();
  void handleFactoryReset();
  void handleNotFound();

  Controller* ctl_ = nullptr;
  Net* net_ = nullptr;
  EventFn onEvent_;
  WebServer server_{cfg::kHttpPort};
  WebSocketsServer ws_{cfg::kWsPort};
  String adminPass_;  // empty = open
  uint32_t lastBroadcastMs_ = 0;
  bool uploadAuthorized_ = false;
  bool uploadOk_ = false;
};
