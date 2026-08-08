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
// What lives here (READ handlers):
//   - getHead()                  version, errors, gitVersion
//   - getValue()                 current weight / CG / battery
//   - getRawValue()              raw per-cell weight + battery
//   - getParameter()             full parameter dump (loads MODEL_FILE)
//   - getVirtualWeight()         virtual weight list as JSON
//   - getWiFiNetworks()          scanned networks + ssid_STA fallback
//
// What lives here (WRITE handlers, added in step 2):
//   - saveParameter()            parses server.arg(...) and writes
//                                all tunables + WiFi creds + model
//                                distances; persists via EEPROM + Models
//   - autoCalibrate()            runs runAutoCalibrate() in a loop
//   - runTare()                  calls tareLoadcells(); 404 if it errors
//   - saveModel()                writes target CG + virtual weights
//                                into the current model, then persists
//   - openModel()                loads a named model from MODEL_FILE
//   - deleteModel()              removes a named model from MODEL_FILE
//
// What stays in CG_scale.ino:
//   - The server.on(...) calls in setup() — those register these handlers
//     against the routes. They stay inline in setup() for now; future
//     PR could move them into a setupWebApi() wrapper, but that would
//     be a behaviour-preserving refactor of setup() itself, which is
//     out of scope here.

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>

// ---------- Forward declarations ----------
//
// These functions live in other modules (HX711Manager.h, Models.h) and
// are called from the write handlers. WebApi.h must be includable even
// when those modules are not yet included, so we forward-declare them.
// HX711Manager.h / Models.h provide the actual definitions and are
// included BEFORE WebApi.h from CG_scale.ino.
//
// saveModelJson is a single-argument (String) function in Models.h —
// there is no JsonObject overload in this codebase.

inline void saveCalFactor(int nLC);
inline bool runAutoCalibrate();
inline void tareLoadcells();
inline bool getLoadcellError();
inline bool saveModelJson(String name);
inline bool openModelJson(String name);
inline bool deleteModelJson(String name);

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

// ---------- saveParameter ----------
//
// Parses all server.arg(...) entries and writes them into the matching
// tunables (nLoadcells, model.distance[], refWeight, refCG, cal factors,
// resistor values, batType, batCells, WiFi creds, enableUpdate, enableOTA,
// device_Name, loadCellURL[]). Then persists the whole set via EEPROM and,
// if a model name is set, also re-saves the model JSON.
//
// Cross-module deps consumed:
//   - saveCalFactor(int)        from HX711Manager.h
//   - saveModelJson(String)     from Models.h

inline void saveParameter() {
  if (server.hasArg("nLoadcells")) nLoadcells = server.arg("nLoadcells").toInt();
  if (server.hasArg("distanceX1")) model.distance[X1] = server.arg("distanceX1").toFloat();
  if (server.hasArg("distanceX2")) model.distance[X2] = server.arg("distanceX2").toFloat();
  if (server.hasArg("distanceX3")) model.distance[X3] = server.arg("distanceX3").toFloat();
  if (server.hasArg("refWeight")) refWeight = server.arg("refWeight").toFloat();
  if (server.hasArg("refCG")) refCG = server.arg("refCG").toFloat();
  if (server.hasArg("calFactorLoadcell1")) calFactorLoadcell[LC1] = server.arg("calFactorLoadcell1").toFloat();
  if (server.hasArg("calFactorLoadcell2")) calFactorLoadcell[LC2] = server.arg("calFactorLoadcell2").toFloat();
  if (server.hasArg("calFactorLoadcell3")) calFactorLoadcell[LC3] = server.arg("calFactorLoadcell3").toFloat();
  if (server.hasArg("resistorR1")) resistor[R1] = server.arg("resistorR1").toFloat();
  if (server.hasArg("resistorR2")) resistor[R2] = server.arg("resistorR2").toFloat();
  if (server.hasArg("batType")) batType = server.arg("batType").toInt();
  if (server.hasArg("batCells")) batCells = server.arg("batCells").toInt();
  if (server.hasArg("ssid_STA")) server.arg("ssid_STA").toCharArray(ssid_STA, MAX_SSID_PW_LENGHT + 1);
  if (server.hasArg("password_STA")) server.arg("password_STA").toCharArray(password_STA, MAX_SSID_PW_LENGHT + 1);
  if (server.hasArg("ssid_AP")) server.arg("ssid_AP").toCharArray(ssid_AP, MAX_SSID_PW_LENGHT + 1);
  if (server.hasArg("password_AP")) server.arg("password_AP").toCharArray(password_AP, MAX_SSID_PW_LENGHT + 1);
  if (server.hasArg("mechanicsType")) model.mechanicsType = server.arg("mechanicsType").toInt();
  if (server.hasArg("enableUpdate")) enableUpdate = server.arg("enableUpdate").toInt();
  if (server.hasArg("enableOTA")) enableOTA = server.arg("enableOTA").toInt();
  if (server.hasArg("device_Name")) server.arg("device_Name").toCharArray(device_Name, MAX_SSID_PW_LENGHT + 1);
  if (server.hasArg("lc1_URL")) server.arg("lc1_URL").toCharArray(loadCellURL[LC1], MAX_SSID_PW_LENGHT + 1);
  if (server.hasArg("lc2_URL")) server.arg("lc2_URL").toCharArray(loadCellURL[LC2], MAX_SSID_PW_LENGHT + 1);
  if (server.hasArg("lc3_URL")) server.arg("lc3_URL").toCharArray(loadCellURL[LC3], MAX_SSID_PW_LENGHT + 1);

  EEPROM.put(P_NUMBER_LOADCELLS, nLoadcells);
  for (int i = LC1; i <= LC3; i++) {
    EEPROM.put(P_DISTANCE_X1 + (i * sizeof(float)), model.distance[i]);
    saveCalFactor(i);
  }
  EEPROM.put(P_REF_WEIGHT, refWeight);
  EEPROM.put(P_REF_CG, refCG);
  for (int i = R1; i <= R2; i++) {
    EEPROM.put(P_RESISTOR_R1 + (i * sizeof(float)), resistor[i]);
  }
  EEPROM.put(P_BAT_TYPE, batType);
  EEPROM.put(P_BATT_CELLS, batCells);
  EEPROM.put(P_SSID_STA, ssid_STA);
  EEPROM.put(P_PASSWORD_STA, password_STA);
  EEPROM.put(P_SSID_AP, ssid_AP);
  EEPROM.put(P_PASSWORD_AP, password_AP);
  EEPROM.put(P_ENABLE_UPDATE, enableUpdate);
  EEPROM.put(P_ENABLE_OTA, enableOTA);
  EEPROM.put(P_DEVICENAME, device_Name);
  EEPROM.put(P_LC1_URL, loadCellURL[LC1]);
  EEPROM.put(P_LC2_URL, loadCellURL[LC2]);
  EEPROM.put(P_LC3_URL, loadCellURL[LC3]);
  EEPROM.commit();

  if (strlen(model.name) > 0) {
    saveModelJson(model.name);
  }

  server.send(200, "text/plain", "saved");
}

// ---------- autoCalibrate ----------
//
// Runs runAutoCalibrate() in a tight loop until it returns true
// (success). The HX711Manager implementation handles the "no ref
// weight" and "loadcell unstable" cases by returning false.
//
// Cross-module deps consumed:
//   - runAutoCalibrate()        from HX711Manager.h

inline void autoCalibrate() {
  while (!runAutoCalibrate());
  server.send(200, "text/plain", "Calibration successful");
}

// ---------- runTare ----------
//
// Calls tareLoadcells(), then checks getLoadcellError() to decide
// between 200 "Tare completed" and 404 "Tare failed".
//
// Cross-module deps consumed:
//   - tareLoadcells()           from HX711Manager.h
//   - getLoadcellError()        from HX711Manager.h

inline void runTare() {
  tareLoadcells();
  if (!getLoadcellError()) {
    server.send(200, "text/plain", "Tare completed");
    return;
  }
  server.send(404, "text/plain", "404: Tare failed !");
}

// ---------- saveModel ----------
//
// Parses server.arg("modelname") plus the model-specific tunables
// (targetCGmin/max, distances, mechanicsType, virtualWeight[]) into
// the current model struct, then persists via saveModelJson().
//
// Cross-module deps consumed:
//   - saveModelJson(String)     from Models.h

inline void saveModel() {
  if (server.hasArg("modelname")) {
    if (server.hasArg("targetCGmin")) model.targetCGmin = server.arg("targetCGmin").toFloat();
    if (server.hasArg("targetCGmax")) model.targetCGmax = server.arg("targetCGmax").toFloat();
    if (server.hasArg("distanceX1")) model.distance[X1] = server.arg("distanceX1").toFloat();
    if (server.hasArg("distanceX2")) model.distance[X2] = server.arg("distanceX2").toFloat();
    if (server.hasArg("distanceX3")) model.distance[X3] = server.arg("distanceX3").toFloat();
    if (server.hasArg("mechanicsType")) model.mechanicsType = server.arg("mechanicsType").toInt();
    if (server.hasArg("virtualWeight")) {
      DynamicJsonDocument jsonDoc(JSONDOC_SIZE);
      String json = server.arg("virtualWeight");
      json.replace("%22", "\"");
      deserializeJson(jsonDoc, json);
      JsonArray virtw = jsonDoc["virtual"];
      if (virtw) {
        for (int i = 0; i < MAX_VIRTUAL_WEIGHT; i++) {
          model.virtualWeight[i].name = virtw[i][0].as<String>();
          model.virtualWeight[i].cg = virtw[i][1].as<int>();
          model.virtualWeight[i].weight = virtw[i][2].as<int>();
          model.virtualWeight[i].enabled = virtw[i][3].as<bool>();
        }
      }
    }

    if (saveModelJson(server.arg("modelname"))) {
      server.send(200, "text/plain", "saved");
      return;
    }
  }
  server.send(404, "text/plain", "404: Save model failed !");
}

// ---------- openModel ----------
//
// Loads the named model from MODEL_FILE via openModelJson().
//
// Cross-module deps consumed:
//   - openModelJson(String)     from Models.h

inline void openModel() {
  if (server.hasArg("modelname")) {
    if (openModelJson(server.arg("modelname"))) {
      server.send(200, "text/plain", "opened");
      return;
    }
  }
  server.send(404, "text/plain", "404: Open model failed !");
}

// ---------- deleteModel ----------
//
// Removes the named model from MODEL_FILE via deleteModelJson().
//
// Cross-module deps consumed:
//   - deleteModelJson(String)   from Models.h

inline void deleteModel() {
  if (server.hasArg("modelname")) {
    if (deleteModelJson(server.arg("modelname"))) {
      server.send(200, "text/plain", "deleted");
      return;
    }
  }
  server.send(404, "text/plain", "404: Delete model failed !");
}
