// Settings.h — V2.5.x Final PR (step 4 of 7)
//
// Owns the EEPROM read sequence at boot. Extracted from CG_scale.ino's
// setup() as a header-only module — see docs/REFACTOR_PLAN.md.
//
// Header-only rationale: same as the other 11 modules in this project
// — see the pio-windows-esp8266 skill. PIO 6.x on Windows drops
// setup()/loop() when a second .cpp appears in src/.
//
// Cross-module dependencies (READ from other modules, must be defined
// in CG_scale.ino BEFORE this header is included):
//   - printConsole(int, String)       from Util.h (called for the
//                                     "open last model" log message)
//   - model.name                      from Models.h / CG_scale.ino
//                                     (this is WRITTEN here, not read;
//                                     loadSettings() reads the EEPROM
//                                     name into it directly)
//   - openModelJson(String)           from Models.h (called if the
//                                     stored name fails to load)
//   - saveModelJson(String)           from Models.h (called to seed
//                                     DEFAULT_NAME on first boot)
//   - DEFAULT_NAME                    from defaults.h
//
// Settings.h writes the following globals (all cross-cutting state
// owned elsewhere):
//   - nLoadcells, calFactorLoadcell[], resistor[]
//                                     — written but conceptually
//                                     HX711Manager.h domain
//   - batType, batCells               — Battery.h domain
//   - refWeight, refCG                — stays in CG_scale.ino (no
//                                     current module home)
//   - ssid_STA, password_STA,         — Wifi.h domain
//     ssid_AP, password_AP,
//     device_Name
//   - loadCellURL[][]                 — stays in CG_scale.ino (see
//                                     PR 3a step 1 rationale)
//   - enableUpdate                    — OtaUpdate.h domain
//   - enableOTA                       — stays in CG_scale.ino
//   - model.name                      — Models.h domain
//
// What lives here:
//   - loadSettings(): walks EEPROM addresses in the order defined in
//                     defaults.h, populates all tunables. Uses the
//                     "0xFF = empty slot" sentinel pattern that the
//                     original setup() used.
//
// What stays in CG_scale.ino:
//   - Serial.begin / print banner / LittleFS.begin / EEPROM.begin —
//     pre-EEPROM-read setup glue (no settings involved).
//   - The "open last model" block (openModelJson → saveModelJson →
//     openModelJson fallback to DEFAULT_NAME) — uses models, not
//     settings; stays after loadSettings() returns.

#pragma once

#include <Arduino.h>
#include <EEPROM.h>

// ---------- loadSettings ----------
//
// Reads every persisted setting from EEPROM using the address
// constants from defaults.h. Mirrors the original setup() block:
// "if (EEPROM.read(addr) != 0xFF) then EEPROM.get(addr, var);" pattern.
//
// On first boot (or after factory reset) the cells are 0xFF and the
// defaults stay in place.

inline void loadSettings() {
  if (EEPROM.read(P_NUMBER_LOADCELLS) != 0xFF) {
    nLoadcells = EEPROM.read(P_NUMBER_LOADCELLS);
  }

  for (int i = LC1; i <= LC3; i++) {
    if (EEPROM.read(P_DISTANCE_X1 + (i * sizeof(float))) != 0xFF) {
      EEPROM.get(P_DISTANCE_X1 + (i * sizeof(float)), model.distance[i]);
    }

    if (EEPROM.read(P_LOADCELL1_CALIBRATION_FACTOR + (i * sizeof(float))) != 0xFF) {
      EEPROM.get(P_LOADCELL1_CALIBRATION_FACTOR + (i * sizeof(float)), calFactorLoadcell[i]);
    }
  }

  if (EEPROM.read(P_BAT_TYPE) != 0xFF) {
    batType = EEPROM.read(P_BAT_TYPE);
  }

  if (EEPROM.read(P_BATT_CELLS) != 0xFF) {
    batCells = EEPROM.read(P_BATT_CELLS);
  }

  if (EEPROM.read(P_REF_WEIGHT) != 0xFF) {
    EEPROM.get(P_REF_WEIGHT, refWeight);
  }

  if (EEPROM.read(P_REF_CG) != 0xFF) {
    EEPROM.get(P_REF_CG, refCG);
  }

  for (int i = R1; i <= R2; i++) {
    if (EEPROM.read(P_RESISTOR_R1 + (i * sizeof(float))) != 0xFF) {
      EEPROM.get(P_RESISTOR_R1 + (i * sizeof(float)), resistor[i]);
    }
  }

  if (EEPROM.read(P_SSID_STA) != 0xFF) {
    EEPROM.get(P_SSID_STA, ssid_STA);
  }

  if (EEPROM.read(P_PASSWORD_STA) != 0xFF) {
    EEPROM.get(P_PASSWORD_STA, password_STA);
  }

  if (EEPROM.read(P_SSID_AP) != 0xFF) {
    EEPROM.get(P_SSID_AP, ssid_AP);
  }

  if (EEPROM.read(P_PASSWORD_AP) != 0xFF) {
    EEPROM.get(P_PASSWORD_AP, password_AP);
  }

  if (EEPROM.read(P_MODELNAME) != 0xFF) {
    EEPROM.get(P_MODELNAME, model.name);
  }

  if (EEPROM.read(P_ENABLE_UPDATE) != 0xFF) {
    EEPROM.get(P_ENABLE_UPDATE, enableUpdate);
  }

  if (EEPROM.read(P_ENABLE_OTA) != 0xFF) {
    EEPROM.get(P_ENABLE_OTA, enableOTA);
  }

  if (EEPROM.read(P_DEVICENAME) != 0xFF) {
    EEPROM.get(P_DEVICENAME, device_Name);
  } else {
    strcpy(device_Name, ssid_AP);
  }

  if (EEPROM.read(P_LC1_URL) != 0xFF) {
    EEPROM.get(P_LC1_URL, loadCellURL[LC1]);
  }

  if (EEPROM.read(P_LC2_URL) != 0xFF) {
    EEPROM.get(P_LC2_URL, loadCellURL[LC2]);
  }

  if (EEPROM.read(P_LC3_URL) != 0xFF) {
    EEPROM.get(P_LC3_URL, loadCellURL[LC3]);
  }
}
