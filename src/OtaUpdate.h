// OtaUpdate.h — V2.5.x PR 3a step 3
//
// Owns the HTTPS-based firmware-update probe and the OLED update-progress
// screen. Extracted from CG_scale.ino as a header-only module — see
// docs/REFACTOR_PLAN.md.
//
// Header-only rationale: same as Battery.h, Util.h, Display.h,
// HX711Manager.h, Models.h, Wifi.h, WebFiles.h — see the
// pio-windows-esp8266 skill. PIO 6.x on Windows drops setup()/loop()
// when a second .cpp appears in src/.
//
// Cross-module dependencies (READ from other modules, must be defined
// in CG_scale.ino BEFORE this header is included):
//   - printConsole(int, String)         from Util.h
//   - oledDisplay (U8G2)                from Display.h (firstPage/nextPage/
//                                       setFont/setCursor/print/printf/
//                                       drawFrame/drawBox)
//   - oledFontSmall                     from Display.h (the small font for
//                                       the update progress screen)
//   - HOST / HTTPS_PORT / URL           from settings_ESP8266.h (constants
//                                       for the GitHub release probe URL)
//   - PROBE_UPDATE                      from defaults.h (uint8_t command
//                                       value passed to httpsUpdate())
//   - CGSCALE_VERSION                   from CG_scale.ino (#define, compared
//                                       against the parsed gitVersion)
//
// What lives here:
//   - updateMsg                         update status string shown on OLED
//                                       and serial console
//   - gitVersion                        parsed from GitHub release URL,
//                                       compared against CGSCALE_VERSION
//   - enableUpdate                      bool flag controlling whether
//                                       setup() runs the HTTPS probe
//   - printUpdateProgress(progress,total)  renders the OLED update screen
//   - httpsUpdate(command)              GitHub release probe; updates
//                                       gitVersion on success
//
// What stays in CG_scale.ino:
//   - enableUpdate = ENABLE_UPDATE in the global block (initial value)
//   - EEPROM.get/put for P_ENABLE_UPDATE (lives in saveParameter / setup)
//   - httpsClient definition (line ~116) and httpsClient.setInsecure()
//     call (line ~779) — the WiFiClientSecure instance is shared and
//     must live where it's declared
//   - The ArduinoOTA setup callbacks in setup() (lines ~740–776) that
//     write to updateMsg and call printUpdateProgress — they will move
//     here in PR 3b when setup() is restructured
//   - The `httpsUpdate(PROBE_UPDATE)` call in setup() (line ~781) —
//     part of the setup sequence
//   - The ArduinoOTA.handle() call in loop() (line ~793) — part of the
//     loop dispatch
//   - getHead() reads gitVersion; saveParameter() reads enableUpdate —
//     both stay in the .ino for now, will move to WebApi.h in PR 3b

#pragma once

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <U8g2lib.h>

// ---------- OTA state ----------
//
// All three are owned here. enableUpdate is initialised from the
// ENABLE_UPDATE macro defined in settings_ESP8266.h (must be included
// before this header from CG_scale.ino).

String updateMsg = "";
float gitVersion = -1;
bool enableUpdate = ENABLE_UPDATE;

// ---------- Forward declarations ----------
//
// setupArduinoOTA() (defined below) references printUpdateProgress()
// before its inline definition appears later in the same header.
// inline functions need forward declarations when they are called
// before their definition in the same TU — same pattern as
// setupWebApi() in WebApi.h.

inline void printUpdateProgress(unsigned int progress, unsigned int total);

// ---------- setupArduinoOTA ----------
//
// Wires up the ArduinoOTA library: hostname, password, lifecycle
// callbacks, and finally ArduinoOTA.begin(). Called from setup()
// guarded by `if (enableOTA)` — the guard stays in the .ino because
// enableOTA is a cross-cutting flag read by loop()'s ArduinoOTA.handle()
// too.
//
// Lifecycle callbacks write into the updateMsg global (cross-cutting,
// read by printUpdateProgress() and WebApi.h's getHead()) and call
// printUpdateProgress() for the OLED progress screen.
//
// Cross-module deps consumed:
//   - ssid_AP, password_AP          from Wifi.h / CG_scale.ino
//   - updateMsg                     from this header (owned)
//   - printUpdateProgress()         defined below in this header
//                                    (forward-declared above)
//   - printConsole(T_*, String)     from Util.h

inline void setupArduinoOTA() {
  ArduinoOTA.setHostname(ssid_AP);
  ArduinoOTA.setPassword(password_AP);

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "firmware";
    } else {
      type = "LittleFS";
    }
    updateMsg = "Updating " + type;
    printConsole(T_UPDATE, type);
  });

  ArduinoOTA.onEnd([]() {
    updateMsg = "successful..";
    printUpdateProgress(100, 100);
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    printUpdateProgress(progress, total);
  });

  ArduinoOTA.onError([](ota_error_t error) {
    if (error == OTA_AUTH_ERROR) {
      updateMsg = "Auth Failed";
    } else if (error == OTA_BEGIN_ERROR) {
      updateMsg = "Begin Failed";
    } else if (error == OTA_CONNECT_ERROR) {
      updateMsg = "Connect Failed";
    } else if (error == OTA_RECEIVE_ERROR) {
      updateMsg = "Receive Failed";
    } else if (error == OTA_END_ERROR) {
      updateMsg = "End Failed";
    }
    printUpdateProgress(0, 100);
  });

  ArduinoOTA.begin();
  printConsole(T_RUN, "OTA is up and running");
}

// ---------- printUpdateProgress ----------
//
// Renders the firmware-update progress screen on the OLED and logs the
// current updateMsg to the serial console. Called from:
//   - httpsUpdate (via the ArduinoOTA callbacks that still live in
//     CG_scale.ino — those will move to this module in PR 3b)
//   - ArduinoOTA.onProgress / .onError (still in CG_scale.ino)
//
// Cross-module deps consumed:
//   - printConsole(T_UPDATE, ...)        from Util.h
//   - oledDisplay.firstPage/nextPage/... from Display.h
//   - oledFontSmall                      from Display.h

inline void printUpdateProgress(unsigned int progress, unsigned int total) {
  printConsole(T_UPDATE, updateMsg);

  oledDisplay.firstPage();
  do {
    oledDisplay.setFont(oledFontSmall);
    oledDisplay.setCursor(0, 12);
    oledDisplay.print(updateMsg);

    oledDisplay.setCursor(107, 35);
    oledDisplay.printf("%u%%\r", (progress / (total / 100)));

    oledDisplay.drawFrame(0, 40, 128, 10);
    oledDisplay.drawBox(0, 40, (progress / (total / 128)), 10);

  } while (oledDisplay.nextPage());
}

// ---------- httpsUpdate ----------
//
// Probes the GitHub release URL and parses the latest version from the
// Location-header redirect. Stores the version in gitVersion and
// compares it against CGSCALE_VERSION.
//
// Returns true on success (any HTTP response received), false on
// connection failure. The caller (currently in setup()) decides what
// to do with the comparison.
//
// Cross-module deps consumed:
//   - printConsole(T_*, ...)           from Util.h
//   - HOST, HTTPS_PORT, URL            from settings_ESP8266.h
//   - httpsClient (WiFiClientSecure)   from CG_scale.ino — shared global,
//                                     not owned here. Must already be
//                                     initialised by the caller via
//                                     httpsClient.setInsecure() etc.
//
// NOTE: `httpsClient` is declared in CG_scale.ino and accessed here
// through header-concat. This is the same pattern as `server` in
// WebFiles.h — a WiFi-stack global that has to live somewhere, and
// the .ino is the natural home since multiple modules use it.

inline bool httpsUpdate(uint8_t command) {
  if (!httpsClient.connect(HOST, HTTPS_PORT)) {
    printConsole(T_ERROR, "Wifi: connection to GIT failed");
    return false;
  }

  const char *headerKeys[] = {"Location"};
  const size_t numberOfHeaders = 1;

  HTTPClient https;
  https.setUserAgent("cgscale");
  https.setRedirectLimit(0);
  https.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

  String url = "https://" + String(HOST) + String(URL);
  if (https.begin(httpsClient, url)) {
    https.collectHeaders(headerKeys, numberOfHeaders);

    printConsole(T_HTTPS, "GET: " + url);
    int httpCode = https.GET();
    if (httpCode > 0) {
      // response
      if (httpCode == HTTP_CODE_FOUND) {
        String newUrl = https.header("Location");
        gitVersion = newUrl.substring(newUrl.lastIndexOf('/') + 2).toFloat();
        if (gitVersion > String(CGSCALE_VERSION).toFloat()) {
          printConsole(T_UPDATE, "Firmware update available: V" + String(gitVersion));
        } else {
          printConsole(T_UPDATE, "Firmware version found on GitHub: V" + String(gitVersion) + " - current firmware is up to date");
        }
      } else if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
        // Serial.println(https.getString());
      } else {
        printConsole(T_ERROR, "HTTPS: GET... failed, " + https.errorToString(httpCode));
        https.end();
        return false;
      }
    } else {
      return false;
    }
    https.end();
  } else {
    printConsole(T_ERROR, "Wifi: Unable to connect");
    return false;
  }

  return true;
}
