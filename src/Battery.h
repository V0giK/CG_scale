/*
  -------------------------------------------------------------------
                          Battery.h

  Battery state-of-charge estimation. Extracted from CG_scale.ino
  (V2.4.2) into a header-only module (V2.5.x).

  Header-only because PlatformIO 6.x on Windows has known issues with
  multiple .cpp files in src/ (SConsBuilder drops setup/loop from the
  link). See docs/REFACTOR_PLAN.md for the architectural rationale.

  Public API:
    - int percentBat(float cellVoltage): returns 0..100 for the
      currently selected battery type (uses global batType).
    - The PROGMEM percentList table is exposed via defaults.h.

  Dependencies (consumer must provide):
    - batType (uint8_t) — global, defined in CG_scale.ino
    - defaults.h — for percentList[] and DATAPOINTS_PERCENTLIST

  Behavior: byte-identical to V2.4.2.
  -------------------------------------------------------------------
*/

#pragma once

#include <Arduino.h>
#include <avr/pgmspace.h>
// defaults.h is intentionally NOT included here — CG_scale.ino already
// pulls it in before this header, and the Arduino build concatenates
// .ino + headers into a single TU. Including defaults.h twice would
// cause redefinition errors for the X1/X2/X3, LC1/LC2/LC3, etc. enums.

inline int percentBat(float cellVoltage) {
  int result = 0;
  int elementCount = DATAPOINTS_PERCENTLIST;
  byte batTypeArray = batType - 2;

  for (int i = 0; i < elementCount; i++) {
    if (pgm_read_float(&percentList[batTypeArray][i][1]) == 100) {
      elementCount = i;
      break;
    }
  }

  float cellempty = pgm_read_float(&percentList[batTypeArray][0][0]);
  float cellfull = pgm_read_float(&percentList[batTypeArray][elementCount][0]);

  if (cellVoltage >= cellfull) {
    result = 100;
  } else if (cellVoltage <= cellempty) {
    result = 0;
  } else {
    for (int i = 0; i <= elementCount; i++) {
      float curVolt = pgm_read_float(&percentList[batTypeArray][i][0]);
      if (curVolt >= cellVoltage && i > 0) {
        float lastVolt = pgm_read_float(&percentList[batTypeArray][i - 1][0]);
        float curPercent = pgm_read_float(&percentList[batTypeArray][i][1]);
        float lastPercent = pgm_read_float(&percentList[batTypeArray][i - 1][1]);
        result = float((cellVoltage - lastVolt) / (curVolt - lastVolt)) * (curPercent - lastPercent) + lastPercent;
        break;
      }
    }
  }

  return result;
}