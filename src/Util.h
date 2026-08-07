/*
  -------------------------------------------------------------------
                          Util.h

  System helpers and console logging. Extracted from CG_scale.ino
  (V2.4.2) into a header-only module (V2.5.x).

  Header-only because PlatformIO 6.x on Windows has known issues with
  multiple .cpp files in src/ (SConsBuilder drops setup/loop from the
  link). See docs/REFACTOR_PLAN.md for the architectural rationale.

  Public API:
    - void resetCPU(): empty in V2.4.2 (kept as a stub for API stability).
    - char *TimeToString(unsigned long t): formats millis() as "HH:MM:SS.mmm".
      Returns a pointer to a static buffer (NOT thread-safe; do not call
      from multiple places in a single expression).
    - void printConsole(int t, String msg): logs a timestamped message
      to Serial with a category prefix (BOOT/RUN/ERROR/WIFI/UPDATE/HTTPS).

  Dependencies (consumer must provide):
    - T_BOOT, T_RUN, T_ERROR, T_WIFI, T_UPDATE, T_HTTPS (int enums from
      defaults.h — already included by CG_scale.ino before this header).

  Behavior: byte-identical to V2.4.2.
  -------------------------------------------------------------------
*/

#pragma once

#include <Arduino.h>

inline void resetCPU() {
}

inline char *TimeToString(unsigned long t) {
  static char str[15];
  int h = t / 3600000;
  t = t % 3600000;
  int m = t / 60000;
  t = t % 60000;
  int s = t / 1000;
  int ms = t % 1000;
  sprintf(str, "%02d:%02d:%02d.%03d", h, m, s, ms);
  return str;
}

inline void printConsole(int t, String msg) {
  Serial.print(TimeToString(millis()));
  Serial.print(" [");
  switch (t) {
    case T_BOOT:
      Serial.print("BOOT");
      break;
    case T_RUN:
      Serial.print("RUN");
      break;
    case T_ERROR:
      Serial.print("ERROR");
      break;
    case T_WIFI:
      Serial.print("WIFI");
      break;
    case T_UPDATE:
      Serial.print("UPDATE");
      break;
    case T_HTTPS:
      Serial.print("HTTPS");
      break;
  }
  Serial.print("] ");
  Serial.println(msg);
}