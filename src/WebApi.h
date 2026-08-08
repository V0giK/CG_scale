// WebApi.h — V2.5.x PR 3b step 1 (Read-Handler only)
//
// Owns the HTTP GET handlers that send state to the client. Extracted
// from CG_scale.ino as a header-only module — see docs/REFACTOR_PLAN.md.
//
// Header-only rationale: same as Battery.h, Util.h, Display.h,
// HX711Manager.h, Models.h, Wifi.h, WebFiles.h, OtaUpdate.h — see the
// pio-windows-esp8266 skill. PIO 6.x on Windows drops setup()/loop()
// when a second .cpp appears in src/.
//
// This is the READ half of the WebApi split. Write-Handler
// (saveParameter, autoCalibrate, runTare, saveModel, openModel,
// deleteModel) live in the same module but will be added in a
// separate commit (PR 3b step 2) to keep the diff per commit small
// and the review surface manageable.
//
// Cross-module dependencies (READ from other modules, must be defined
// in CG_scale.ino BEFORE this header is included):
//   - server (ESP8266WebServer)             from CG_scale.ino
//   - ssid_AP, password_AP, ssid_STA,       from CG_scale.ino (Wifi.h owns
//     password_STA, device_Name              them but they were initialised
//                                            from settings_ESP8266.h macros
//                                            before the include chain
//                                            reached this header)
//   - loadCellURL[][]                       from CG_scale.ino (stayed there
//                                            per PR 3a step 1 rationale)
//   - enableUpdate                          from OtaUpdate.h
//   - enableOTA                             from CG_scale.ino
//   - gitVersion                            from OtaUpdate.h
//   - errMsg[], errMsgCnt                   from CG_scale.ino (cross-cutting
//                                            shared global, see PR 3a step 3)
//   - model (struct Model), VirtualWeight   from defaults.h struct decl +
//                                            CG_scale.ino global instance
//   - model.distance[], model.name,         ditto
//     model.virtualWeight[], model.targetCGmin/max,
//     model.mechanicsType
//   - weightTotal, CG_length, CG_trans      from HX711Manager.h (owned
//                                            there; computed in updateLoadcells)
//   - weightLoadCell[]                      from HX711Manager.h
//   - batType, batCells, batVolt            from Battery.h / CG_scale.ino
//   - refWeight, refCG                      from CG_scale.ino
//   - nLoadcells, calFactorLoadcell[],      from HX711Manager.h / CG_scale.ino
//     resistor[]
//   - WiFi.scanNetworks(), WiFi.SSID(i)     from ESP8266WiFi.h
//   - LittleFS, MODEL_FILE, JSONDOC_SIZE,   from LittleFS.h, defaults.h
//     MAX_VIRTUAL_WEIGHT, LC1/LC2/LC3,
//     X1/X2/X3, R1/R2, B_VOLT, CGSCALE_VERSION
//
// What lives here (READ handlers only):
//   - getHead()                  version, errors, gitVersion
//   - getValue()                 current weight / CG / battery
//   - getRawValue()              raw per-cell weight + battery
//   - getParameter()             full parameter dump (loads MODEL_FILE)
//   - getVirtualWeight()         virtual weight list as JSON
//   - getWiFiNetworks()          scanned networks + ssid_STA fallback
//
// What stays in CG_scale.ino:
//   - The server.on(...) calls in setup() — those register these handlers
//     against the routes. They stay inline in setup() for now; future
//     PR could move them into a setupWebApi() wrapper, but that would
//     be a behaviour-preserving refactor of setup() itself, which is
//     out of scope here.
//   - The 6 write handlers (saveParameter…deleteModel) — separate commit.

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>

// ---------- getHead ----------
//
// Returns a "&"-separated string: ssid_AP, error messages, current
// firmware version, latest git release version.

inline void getHead() {
  String response = ssid_AP;
  response += "&";
  for (int i = 1; i <= errMsgCnt; i++) {
    response += errMsg[i];
  }
  response += "&";
  response += CGSCALE_VERSION;
  response += "&";
  response += gitVersion;
  server.send(200, "text/html", response);
}

// ---------- getValue ----------
//
// Returns weightTotal, CG_length, CG_trans, batVolt formatted as a
// "&"-separated string with units appended.

inline void getValue() {
  char buff[8];
  String response = "";
  dtostrf(weightTotal, 5, 1, buff);
  response += buff;
  response += "g&";
  dtostrf(CG_length, 5, 1, buff);
  response += buff;
  response += "mm&";
  dtostrf(CG_trans, 5, 1, buff);
  response += buff;
  response += "mm&";
  if (batType == B_VOLT) {
    dtostrf(batVolt, 5, 2, buff);
    response += buff;
    response += "V";
  } else {
    dtostrf(batVolt, 5, 0, buff);
    response += buff;
    response += "%";
  }
  server.send(200, "text/html", response);
}

// ---------- getRawValue ----------
//
// Per-cell raw weights + battery. Same battery formatting as getValue().

inline void getRawValue() {
  char buff[8];
  String response = "";
  dtostrf(weightLoadCell[LC1], 5, 1, buff);
  response += buff;
  response += "g&";
  dtostrf(weightLoadCell[LC2], 5, 1, buff);
  response += buff;
  response += "g&";
  dtostrf(weightLoadCell[LC3], 5, 1, buff);
  response += buff;
  response += "g&";
  if (batType == B_VOLT) {
    dtostrf(batVolt, 5, 2, buff);
    response += buff;
    response += "V";
  } else {
    dtostrf(batVolt, 5, 0, buff);
    response += buff;
    response += "%";
  }
  server.send(200, "text/html", response);
}

// ---------- getParameter ----------
//
// Dumps the full parameter set used by the settings page. Reads the
// currently-active model from MODEL_FILE on LittleFS to populate the
// saved weight/CG/target fields. Returns a "&"-separated string with
// all values, in the order the settings.html page expects.

inline void getParameter() {
  char buff[8];
  String response = "";
  float weightTotal_saved = 0;
  float CG_length_saved = 0;
  float CG_trans_saved = 0;
  model.targetCGmin = 0;
  model.targetCGmax = 0;

  DynamicJsonDocument jsonDoc(JSONDOC_SIZE);

  if (LittleFS.exists(MODEL_FILE)) {
    // read json file
    File f = LittleFS.open(MODEL_FILE, "r");
    auto error = deserializeJson(jsonDoc, f);
    f.close();
    // check if model exists
    if (!error && jsonDoc.containsKey(model.name)) {
      weightTotal_saved = jsonDoc[model.name]["wt"];
      CG_length_saved = jsonDoc[model.name]["cg"];
      CG_trans_saved = jsonDoc[model.name]["cglr"];
      model.targetCGmin = jsonDoc[model.name]["cgmin"];
      model.targetCGmax = jsonDoc[model.name]["cgmax"];
      model.mechanicsType = jsonDoc[model.name]["mType"];
    }
  }

  // parameter list
  response += nLoadcells;
  response += "&";
  for (int i = X1; i <= X3; i++) {
    response += model.distance[i];
    response += "&";
  }
  response += refWeight;
  response += "&";
  response += refCG;
  response += "&";
  for (int i = LC1; i <= LC3; i++) {
    response += calFactorLoadcell[i];
    response += "&";
  }
  for (int i = R1; i <= R2; i++) {
    response += resistor[i];
    response += "&";
  }
  response += batType;
  response += "&";
  response += batCells;
  response += "&";
  response += ssid_STA;
  response += "&";
  response += password_STA;
  response += "&";
  response += ssid_AP;
  response += "&";
  response += password_AP;
  response += "&";
  response += model.name;
  response += "&";
  dtostrf(weightTotal_saved, 5, 1, buff);
  response += buff;
  response += "g&";
  dtostrf(CG_length_saved, 5, 1, buff);
  response += buff;
  response += "mm&";
  dtostrf(CG_trans_saved, 5, 1, buff);
  response += buff;
  response += "mm&";
  response += model.targetCGmin;
  response += "&";
  response += model.targetCGmax;
  response += "&";
  response += model.mechanicsType;
  response += "&";
  response += enableUpdate;
  response += "&";
  response += enableOTA;
  response += "&";
  response += device_Name;
  for (int i = LC1; i <= LC3; i++) {
    response += "&";
    response += loadCellURL[LC1];
  }
  server.send(200, "text/html", response);
}

// ---------- getVirtualWeight ----------
//
// Serialises the VirtualWeight[] array as a JSON array under the
// "virtual" key. Each entry: name, cg, weight, enabled.

inline void getVirtualWeight() {
  String response = "";

  DynamicJsonDocument jsonDoc(JSONDOC_SIZE);

  JsonArray virtw = jsonDoc.createNestedArray("virtual");
  for (int i = 0; i < MAX_VIRTUAL_WEIGHT; i++) {
    JsonArray virtWeight = virtw.createNestedArray();
    virtWeight.add(model.virtualWeight[i].name);
    virtWeight.add(model.virtualWeight[i].cg);
    virtWeight.add(model.virtualWeight[i].weight);
    virtWeight.add(model.virtualWeight[i].enabled);
  }

  serializeJson(jsonDoc["virtual"], response);
  server.send(200, "text/html", response);
}

// ---------- getWiFiNetworks ----------
//
// Scans for available networks and returns their SSIDs as a
// "&"-separated string. If the configured ssid_STA is not in the
// scan result, append it so the settings page can show the saved
// network even when it's currently out of range.

inline void getWiFiNetworks() {
  bool ssidSTAavailable = false;
  String response = "";
  int n = WiFi.scanNetworks();

  if (n > 0) {
    for (int i = 0; i < n; ++i) {
      response += WiFi.SSID(i);
      if (WiFi.SSID(i) == ssid_STA) ssidSTAavailable = true;
      if (i < n - 1) response += "&";
    }
    if (!ssidSTAavailable) {
      response += "&";
      response += ssid_STA;
    }
  }
  server.send(200, "text/html", response);
}
