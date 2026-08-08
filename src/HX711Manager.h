/*
  -------------------------------------------------------------------
                          HX711Manager.h

  Load cell (HX711) hardware interface + tare button. Extracted from
  CG_scale.ino (V2.4.2) into a header-only module (V2.5.x).

  Header-only because PlatformIO 6.x on Windows has known issues with
  multiple .cpp files in src/ (SConsBuilder drops setup/loop from the
  link). See docs/REFACTOR_PLAN.md for the architectural rationale.

  Public API:
    - handleTareBtn(): debounces the hardware TARE button and calls
      tareLoadcells() on long press. Only compiled if PIN_TARE_BUTTON
      is defined (wrapped in #ifdef by the caller).
    - saveCalFactor(int nLC): persists the loadcell's calibration
      factor to EEPROM.
    - updateLoadcells(): drives HX711_ADC::update() on each enabled cell.
    - tareLoadcells(): zeroes each local cell (skipping remote cells
      that have a loadCellURL set).
    - printNewValueText(): serial-console prompt for auto-calibration.
    - runAutoCalibrate(): computes new cal factors from ref weight +
      ref CG, saves to EEPROM. Touches model.distance[].
    - getLoadcellError(): polls each cell for TARE timeout, accumulates
      into errMsg[] and prints to console. Returns true on error.

  Documented dependencies (consumer must provide as globals in .ino):
    - LoadCell[] (HX711_ADC array, defined in .ino)
    - nLoadcells (uint8_t, count of active cells)
    - calFactorLoadcell[] (float[3])
    - weightLoadCell[] (float[3])
    - weightTotal, CG_length, CG_trans (float — shared with Models/Display)
    - refWeight, refCG (float — ref weight for auto-cal)
    - model (Model struct — for distance[3] in runAutoCalibrate)
    - loadCellURL[] (char[3][33] — non-empty means remote loadcell)
    - errMsgCnt (int) + errMsg[] (String[]) — used by getLoadcellError,
      also written from getHead (webapi) and others
    - PIN_TARE_BUTTON (int — only needed if handleTareBtn is compiled)
    - EEPROM, P_LOADCELL1_CALIBRATION_FACTOR (EEPROM addresses from
      defaults.h; .ino must include defaults.h before this header)
    - printOLED (Display.h) — used by handleTareBtn

  Behavior: byte-identical to V2.4.2.
  -------------------------------------------------------------------
*/

#pragma once

#include <Arduino.h>
#include <EEPROM.h>
#include <ESP8266HTTPClient.h>
#include <HX711_ADC.h>
#include <WiFiClient.h>

// Forward declarations — these functions call each other and must be
// declared before their first use, even though they're inline.
inline void tareLoadcells();
inline void saveCalFactor(int nLC);

inline void handleTareBtn() {
  static unsigned long lastTaraBtn = 0;
  if ((millis() - lastTaraBtn) > 20) {
    lastTaraBtn = millis();
    static int tareBtnCnt = 0;
    if (digitalRead(PIN_TARE_BUTTON)) {
      tareBtnCnt = 0;
    } else {
      tareBtnCnt++;
      if (tareBtnCnt > 10) {
        printOLED("TARE ==>", "  tare load cells ...", "");
        // avoid keybounce
        tareBtnCnt = -1000;
        tareLoadcells();
        delay(2000);
      }
    }
  }
}

// save calibration factor
inline void saveCalFactor(int nLC) {
  LoadCell[nLC].setCalFactor(calFactorLoadcell[nLC]);
  EEPROM.put(P_LOADCELL1_CALIBRATION_FACTOR + (nLC * sizeof(float)),
             calFactorLoadcell[nLC]);
  EEPROM.commit();
}

inline void updateLoadcells() {
  for (int i = LC1; i <= LC3; i++) {
    if (i < nLoadcells) {
      LoadCell[i].update();
    }
  }
}

inline void tareLoadcells() {
  for (int i = LC1; i <= LC3; i++) {
    if (i < nLoadcells) {
      if (strlen(loadCellURL[i]) == 0) {
        LoadCell[i].tare();
      }
    }
  }
}

inline void printNewValueText() {
  Serial.print(F("Set new value:"));
}

// run auto calibration
inline bool runAutoCalibrate() {
  Serial.print(F("\nAutocalibration is running"));
  for (int i = 0; i <= 20; i++) {
    Serial.print(F("."));
    delay(100);
  }
  // calculate weight
  float toWeightLoadCell[] = {0, 0, 0};
  toWeightLoadCell[LC2] = ((refCG - model.distance[X1]) * refWeight) / model.distance[X2];
  toWeightLoadCell[LC1] = refWeight - toWeightLoadCell[LC2];
  if (nLoadcells == 3) {
    toWeightLoadCell[LC1] = toWeightLoadCell[LC1] / 2;
    toWeightLoadCell[LC3] = toWeightLoadCell[LC1];
  }
  // calculate calibration factors
  for (int i = LC1; i <= LC3; i++) {
    calFactorLoadcell[i] = calFactorLoadcell[i] / (toWeightLoadCell[i] / weightLoadCell[i]);
    saveCalFactor(i);
  }

  // finish
  Serial.println(F("done"));
  return true;
}

// check if a loadcell has error
inline bool getLoadcellError() {
  bool err = false;

  for (int i = LC1; i <= LC3; i++) {
    if (i < nLoadcells) {
      if (LoadCell[i].getTareTimeoutFlag()) {
        String msg = "ERROR: Timeout TARE Lc" + String(i + 1);
        errMsg[++errMsgCnt] = msg + "\n";
        printConsole(T_ERROR, msg);
        err = true;
      }
    }
  }

  return err;
}

// ---------- pollLoadcells ----------
//
// Called from loop() once per UPDATE_INTERVAL_LOADCELL after
// getLoadcellError() and the battery-voltage read. Walks LC1..LC3
// (limited by nLoadcells), and for each cell either:
//   - reads weight directly from the local HX711 and applies
//     SMOOTHING_LOADCELL exponential smoothing, OR
//   - if a loadCellURL is set for that cell, fetches the remote
//     /getRawValue JSON, parses the "&"-separated fields, and uses
//     the remote weight. Also picks up the remote battery voltage
//     (subString[3]) when both batteries are in percent mode and the
//     remote battery is lower — that's the "lowest wins" semantics
//     to surface a low remote battery on the local display.
//
// Errors are accumulated into errMsg[]/errMsgCnt (cross-cutting
// state, read by getHead()).
//
// Cross-module deps consumed:
//   - LoadCell[]                from CG_scale.ino (global constructor)
//   - loadCellURL[][]           from CG_scale.ino (stayed there per
//                               PR 3a step 1 rationale)
//   - errMsg[], errMsgCnt       from CG_scale.ino (cross-cutting)
//   - batType, batVolt          from CG_scale.ino (writes batVolt
//                               only in the remote-cell branch)
//   - B_VOLT                    from defaults.h
//   - nLoadcells                from CG_scale.ino
//   - LC1/LC2/LC3,              from defaults.h
//     SMOOTHING_LOADCELL
//   - WiFi, HTTPClient          from ESP8266WiFi.h / ESP8266HTTPClient.h
//   - printConsole(T_*, String) from Util.h
//
// Note: the HTTP fetch block keeps its original parsing style
// (subString[] array + while-loop over '&'). It works and is
// byte-identical to the original loop() block — refactor it for
// readability in a later PR, not here.

inline void pollLoadcells() {
  for (int i = LC1; i <= LC3; i++) {
    if (i < nLoadcells) {
      if (strlen(loadCellURL[i]) == 0) {
        weightLoadCell[i] = LoadCell[i].getData();

        weightLoadCell[i] = weightLoadCell[i] + SMOOTHING_LOADCELL * (lastWeightLoadCell[i] - weightLoadCell[i]);
        lastWeightLoadCell[i] = weightLoadCell[i];
      } else {
        WiFiClient client;
        HTTPClient http;

        http.begin(client,
                   "http://" + String(loadCellURL[i]) + "/getRawValue");
        http.setTimeout(2000);
        int httpCode = http.GET();

        if (httpCode == HTTP_CODE_OK) {
          const String &txt = http.getString();

          int delimiterStartIndex = 0;
          int delimiterEndIndex = 0;
          String subString[10];
          int subStringCount = 0;
          while (delimiterEndIndex > -1) {
            delimiterEndIndex = txt.indexOf('&', delimiterStartIndex);
            subString[subStringCount] = txt.substring(delimiterStartIndex, delimiterEndIndex);
            ++subStringCount;
            delimiterStartIndex = delimiterEndIndex + 1;
          }

          weightLoadCell[i] = subString[LC1].toFloat();

          float extBatVolt = subString[3].toFloat();
          if (batType > B_VOLT && batVolt > extBatVolt) {
            batVolt = extBatVolt;
          }
        } else {
          String msg = "ERROR: Lc" + String(i + 1) + " no data";
          errMsg[++errMsgCnt] = msg + "\n";
          printConsole(T_ERROR, msg);
        }

        http.end();
      }
    }
  }
}