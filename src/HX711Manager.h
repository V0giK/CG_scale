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
#include <HX711_ADC.h>

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