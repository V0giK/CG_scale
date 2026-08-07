/*
  -------------------------------------------------------------------
                          Display.h

  OLED display rendering. Extracted from CG_scale.ino (V2.4.2) into a
  header-only module (V2.5.x).

  Header-only because PlatformIO 6.x on Windows has known issues with
  multiple .cpp files in src/ (SConsBuilder drops setup/loop from the
  link). See docs/REFACTOR_PLAN.md for the architectural rationale.

  Public API:
    - void initOLED(): boots the display, sets fonts, draws splash.
    - void printOLED(String aLine1, String aLine2, String aLine3):
      3-line generic text renderer.
    - void printScaleOLED(): renders weight / CG / battery / errors.

  Documented dependencies (consumer must provide as globals in .ino):
    - oledDisplay (U8G2 instance, from settings_ESP8266.h)
    - oledFontBig / Large / Normal / Small (const uint8_t*, set by initOLED)
    - DISPLAY_WIDTH / DISPLAY_HEIGHT (from settings_ESP8266.h)
    - printConsole() (from Util.h — .ino must include Util.h before this)
    - nLoadcells, batType, batVolt, weightLoadCell[], weightTotal,
      CG_length, CG_trans, loadCellURL[] (HX711Manager / Battery / Wifi
      globals — will move into their respective modules in PR 2)
    - errMsgCnt, errMsg[] (OtaUpdate / WebApi globals — see PR 3)
    - CGImage, batteryImage, weightImage, CGtransImage (PROGMEM bitmaps
      from defaults.h)

  Behavior: byte-identical to V2.4.2.
  -------------------------------------------------------------------
*/

#pragma once

#include <Arduino.h>
#include <U8g2lib.h>

// initOLED() must be callable from .ino; uses macros DISPLAY_WIDTH/HEIGHT
// and references printConsole(), oledDisplay, oledFont*, CGImage, etc.
// (declared in the including .ino).

inline void initOLED() {
  oledDisplay.begin();
  printConsole(T_BOOT, "init OLED display: " + String(DISPLAY_WIDTH) +
                           String("x") + String(DISPLAY_HEIGHT));

#if DISPLAY_HEIGHT > 32
  oledFontBig = u8g2_font_helvR18_tn;
  oledFontLarge = u8g2_font_helvR12_tr;
  oledFontNormal = u8g2_font_helvR10_tr;
  oledFontSmall = u8g2_font_5x7_tr;
#elif DISPLAY_HEIGHT <= 32
  oledFontBig = u8g2_font_helvR14_tr;
  oledFontLarge = u8g2_font_helvR10_tr;
  oledFontNormal = u8g2_font_6x12_tr;
  oledFontSmall = u8g2_font_5x7_tr;
#endif

  oledDisplay.firstPage();
  do {
    oledDisplay.setFont(oledFontLarge);
#if DISPLAY_HEIGHT <= 32
    oledDisplay.drawXBMP(0, 0, 18, 18, CGImage);
#else
    oledDisplay.drawXBMP(20, 12, 18, 18, CGImage);
#endif
    oledDisplay.setFont(oledFontLarge);

#if DISPLAY_HEIGHT <= 32
    oledDisplay.setCursor(25, 12);
#else
    oledDisplay.setCursor(45, 28);
#endif
    oledDisplay.print(F("CG scale"));

    oledDisplay.setFont(oledFontSmall);
#if DISPLAY_HEIGHT <= 32
    oledDisplay.setCursor(90, 12);
    oledDisplay.print(F("v"));
#else
    oledDisplay.setCursor(35, 46);
    oledDisplay.print(F("Version: "));
#endif
    oledDisplay.print(CGSCALE_VERSION);
#if DISPLAY_HEIGHT <= 32
    oledDisplay.setCursor(15, 22);
#else
    oledDisplay.setCursor(20, 55);
#endif
    oledDisplay.print(F("(c) 2020 M.Lehmann"));

#if DISPLAY_HEIGHT <= 32
    oledDisplay.setCursor(15, 31);
#else
    oledDisplay.setCursor(20, 64);
#endif
    oledDisplay.print(F("(c) 2025 T.Schwartau"));

  } while (oledDisplay.nextPage());
}

inline void printOLED(String aLine1, String aLine2, String aLine3) {
  int ylineHeight = DISPLAY_HEIGHT / 3;

  oledDisplay.firstPage();
  do {
    oledDisplay.setFont(oledFontNormal);
    oledDisplay.setCursor(0, ylineHeight * 1);
    oledDisplay.print(aLine1);
    oledDisplay.setCursor(0, ylineHeight * 2);
    oledDisplay.print(aLine2);
    oledDisplay.setCursor(0, DISPLAY_HEIGHT);
    oledDisplay.print(aLine3);
  } while (oledDisplay.nextPage());
}

inline void printScaleOLED() {
  // print to display
  char buff[8];
  int pos_weightTotal = 7;
  int pos_CG_length = 28;
  if (nLoadcells == 2) {
    pos_weightTotal = 17;
    pos_CG_length = 45;
    if (batType == 0) {
      pos_weightTotal = 12;
      pos_CG_length = 40;
    }
  }

  oledDisplay.firstPage();
  do {
    if (errMsgCnt == 0) {
      // print battery
      if (batType > B_OFF) {
        oledDisplay.drawXBMP(88, 1, 12, 6, batteryImage);
        if (batType == B_VOLT) {
          dtostrf(batVolt, 2, 2, buff);
        } else {
          dtostrf(batVolt, 3, 0, buff);
          oledDisplay.drawBox(89, 2, (batVolt / (100 / 8)), 4);
        }
        oledDisplay.setFont(oledFontSmall);
        oledDisplay.setCursor(123 - oledDisplay.getStrWidth(buff), 7);
        oledDisplay.print(buff);
        if (batType == B_VOLT) {
          oledDisplay.print(F("V"));
        } else {
          oledDisplay.print(F("%"));
        }
      }

      if (DISPLAY_HEIGHT <= 32 && (strlen(loadCellURL[LC1]) || strlen(loadCellURL[LC2]) || strlen(loadCellURL[LC3]))) {
        oledDisplay.setFont(oledFontBig);
        float weight = 0;
        for (int i = LC1; i <= LC3; i++) {
          if (i < nLoadcells) {
            if (strlen(loadCellURL[i]) == 0) {
              weight = weightLoadCell[i];
            }
          }
        }
        dtostrf(weight, 5, 1, buff);
        oledDisplay.setCursor(80 - oledDisplay.getStrWidth(buff), 28);
        oledDisplay.print(buff);
        oledDisplay.print(F(" g"));
      } else {
        // print total weight
        if (nLoadcells == 1) {
          oledDisplay.setFont(oledFontBig);
          dtostrf(weightTotal, 5, 1, buff);
#if DISPLAY_HEIGHT <= 32
          oledDisplay.setCursor(80 - oledDisplay.getStrWidth(buff), 28);
#else
        oledDisplay.drawXBMP(2, pos_weightTotal, 18, 18, weightImage);
        oledDisplay.setCursor(93 - oledDisplay.getStrWidth(buff), pos_weightTotal + 17);
#endif
          oledDisplay.print(buff);
          oledDisplay.print(F(" g"));
        } else {
          oledDisplay.setFont(oledFontNormal);
          dtostrf(weightTotal, 5, 1, buff);
#if DISPLAY_HEIGHT <= 32
          oledDisplay.setCursor(1, 18);
          oledDisplay.print(F("M  = "));
#else
        oledDisplay.drawXBMP(2, pos_weightTotal, 18, 18, weightImage);
        oledDisplay.setCursor(93 - oledDisplay.getStrWidth(buff),
                              pos_weightTotal + 17);
#endif
          oledDisplay.print(buff);
          oledDisplay.print(F(" g"));

          // print CG longitudinal axis
          dtostrf(CG_length, 5, 1, buff);
#if DISPLAY_HEIGHT <= 32
          oledDisplay.setCursor(1, 32);
          oledDisplay.print(F("CG = "));
#else
        oledDisplay.drawXBMP(2, pos_CG_length, 18, 18, CGImage);
        oledDisplay.setCursor(93 - oledDisplay.getStrWidth(buff),
                              pos_CG_length + 16);
#endif
          oledDisplay.print(buff);
          oledDisplay.print(F(" mm"));
        }

        // print CG transverse axis
        if (nLoadcells == 3) {
#if DISPLAY_HEIGHT <= 32
          oledDisplay.setCursor(78, 32);
          oledDisplay.print(F("LR="));
          dtostrf(CG_trans, 3, 0, buff);
#else
        dtostrf(CG_trans, 5, 1, buff);
        oledDisplay.drawXBMP(2, 47, 18, 18, CGtransImage);
        oledDisplay.setCursor(93 - oledDisplay.getStrWidth(buff), 64);
#endif
          oledDisplay.print(buff);
          oledDisplay.print(F(" mm"));
        }
      }
    } else {
      oledDisplay.setFont(oledFontSmall);
      for (int i = 1; i <= errMsgCnt; i++) {
        oledDisplay.setCursor(0, 7 * i);
        oledDisplay.print(errMsg[i]);
      }
    }
  } while (oledDisplay.nextPage());
}