/*
  -------------------------------------------------------------------
                          Models.h

  Model persistence (EEPROM + LittleFS JSON). Extracted from
  CG_scale.ino (V2.4.2) into a header-only module (V2.5.x).

  Header-only because PlatformIO 6.x on Windows has known issues with
  multiple .cpp files in src/ (SConsBuilder drops setup/loop from the
  link). See docs/REFACTOR_PLAN.md for the architectural rationale.

  Public API:
    - writeModelData(JsonObject): fills a JSON object with the current
      model's runtime values (weight, CG, distances, target CGs,
      mechanics type, virtual weights).
    - saveModelJson(String): persists the current model to LittleFS
      MODEL_FILE (creates file if missing).
    - openModelJson(String): reads the named model from JSON into the
      global Model struct and persists model name to EEPROM.
    - deleteModelJson(String): removes the named model; deletes the
      file entirely if it was the last entry.

  The HTTP wrappers (saveModel / openModel / deleteModel that read
  server.arg(...)) stay in the .ino and will move to WebApi.h in PR 3.
  Models.h provides only the JSON-storage layer.

  Documented dependencies (consumer must provide as globals in .ino):
    - model (Model struct, defined in .ino)
    - weightTotal, CG_length, CG_trans (float — read by writeModelData)
    - LittleFS, MODEL_FILE, MAX_MODELNAME_LENGHT, JSONDOC_SIZE (from
      settings_ESP8266.h; .ino must include it before this header)
    - printConsole() (Util.h — .ino must include Util.h before this)
    - EEPROM, P_MODELNAME (from defaults.h; .ino must include before this)
    - ArduinoJson types (DynamicJsonDocument, JsonObject, JsonArray)

  Behavior: byte-identical to V2.4.2.
  -------------------------------------------------------------------
*/

#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

inline void writeModelData(JsonObject object) {
  char buff[8];
  String stringBuff;

  dtostrf(weightTotal, 5, 1, buff);
  stringBuff = buff;
  stringBuff.trim();
  object["wt"] = stringBuff;
  dtostrf(CG_length, 5, 1, buff);
  stringBuff = buff;
  stringBuff.trim();
  object["cg"] = stringBuff;
  dtostrf(CG_trans, 5, 1, buff);
  stringBuff = buff;
  stringBuff.trim();
  object["cglr"] = stringBuff;
  object["x1"] = model.distance[X1];
  object["x2"] = model.distance[X2];
  object["x3"] = model.distance[X3];
  object["cgmin"] = model.targetCGmin;
  object["cgmax"] = model.targetCGmax;
  object["mType"] = model.mechanicsType;

  JsonArray virtw = object.createNestedArray("virtual");
  for (int i = 0; i < MAX_VIRTUAL_WEIGHT; i++) {
    JsonArray virtWeight = virtw.createNestedArray();
    virtWeight.add(model.virtualWeight[i].name);
    virtWeight.add(model.virtualWeight[i].cg);
    virtWeight.add(model.virtualWeight[i].weight);
    virtWeight.add(model.virtualWeight[i].enabled);
  }
}

// save model to json file
inline bool saveModelJson(String modelName) {
  if (modelName.length() > MAX_MODELNAME_LENGHT) {
    return false;
  }

  DynamicJsonDocument jsonDoc(JSONDOC_SIZE);

  if (LittleFS.exists(MODEL_FILE)) {
    // read json file
    File f = LittleFS.open(MODEL_FILE, "r");
    auto error = deserializeJson(jsonDoc, f);
    f.close();
    if (error) {
      printConsole(T_ERROR, "save JSON: " + String(error.c_str()));
      return false;
    }
    // check if model exists
    if (jsonDoc.containsKey(modelName)) {
      writeModelData(jsonDoc[modelName]);
    } else {
      // otherwise create new
      writeModelData(jsonDoc.createNestedObject(modelName));
    }
    // write to file
    if (!error) {
      f = LittleFS.open(MODEL_FILE, "w");
      serializeJson(jsonDoc, f);
      f.close();
    } else {
      printConsole(T_ERROR, "save JSON: " + String(error.c_str()));
      return false;
    }
  } else {
    // creat new json
    writeModelData(jsonDoc.createNestedObject(modelName));
    // write to file
    if (!jsonDoc.isNull()) {
      File f = LittleFS.open(MODEL_FILE, "w");
      serializeJson(jsonDoc, f);
      f.close();
    } else {
      printConsole(T_ERROR, "JSON is null ");
      return false;
    }
  }

  return true;
}

// read model data from json file
inline bool openModelJson(String modelName) {
  DynamicJsonDocument jsonDoc(JSONDOC_SIZE);

  if (LittleFS.exists(MODEL_FILE)) {
    // read json file
    File f = LittleFS.open(MODEL_FILE, "r");
    auto error = deserializeJson(jsonDoc, f);
    f.close();
    if (error) {
      printConsole(T_ERROR, "open JSON: " + String(error.c_str()));
      return false;
    }
    // check if model exists
    if (jsonDoc.containsKey(modelName)) {
      // load parameters from model
      model.distance[X1] = jsonDoc[modelName]["x1"];
      model.distance[X2] = jsonDoc[modelName]["x2"];
      model.distance[X3] = jsonDoc[modelName]["x3"];
      model.targetCGmin = jsonDoc[modelName]["cgmin"];
      model.targetCGmax = jsonDoc[modelName]["cgmax"];
      model.mechanicsType = jsonDoc[modelName]["mType"];

      JsonArray virtw = jsonDoc[modelName]["virtual"];
      if (virtw) {
        for (int i = 0; i < MAX_VIRTUAL_WEIGHT; i++) {
          model.virtualWeight[i].name = virtw[i][0].as<String>();
          model.virtualWeight[i].cg = virtw[i][1].as<int>();
          model.virtualWeight[i].weight = virtw[i][2].as<int>();
          model.virtualWeight[i].enabled = virtw[i][3].as<bool>();
        }
      }
    } else {
      printConsole(T_ERROR, "Model name not found");
      return false;
    }

    // save current model name to eeprom
    modelName.toCharArray(model.name, MAX_MODELNAME_LENGHT + 1);
    EEPROM.put(P_MODELNAME, model.name);
    EEPROM.commit();

    return true;
  }

  printConsole(T_ERROR, "Modelfile not exists");
  return false;
}

// delete model from json file
inline bool deleteModelJson(String modelName) {
  DynamicJsonDocument jsonDoc(JSONDOC_SIZE);

  if (LittleFS.exists(MODEL_FILE)) {
    // read json file
    File f = LittleFS.open(MODEL_FILE, "r");
    auto error = deserializeJson(jsonDoc, f);
    f.close();
    if (error) {
      printConsole(T_ERROR, "delete JSON: " + String(error.c_str()));
      return false;
    }
    // check if model exists
    if (jsonDoc.containsKey(modelName)) {
      jsonDoc.remove(modelName);
    } else {
      printConsole(T_ERROR, "Model name not found");
      return false;
    }
    // if no models in json, kill it
    if (jsonDoc.size() == 0) {
      LittleFS.remove(MODEL_FILE);
    } else {
      // write to file
      if (!jsonDoc.isNull()) {
        File f = LittleFS.open(MODEL_FILE, "w");
        serializeJson(jsonDoc, f);
        f.close();
      } else {
        printConsole(T_ERROR, "JSON is null ");
        return false;
      }
    }
    return true;
  }

  printConsole(T_ERROR, "Modelfile not exists");
  return false;
}