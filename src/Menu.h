// Menu.h — V2.5.x Final PR (step 7 of 7)
//
// Owns the serial-console menu state machine. Extracted from
// CG_scale.ino's loop() as a header-only module — see docs/REFACTOR_PLAN.md.
//
// Header-only rationale: same as the other 11 modules in this project
// — see the pio-windows-esp8266 skill. PIO 6.x on Windows drops
// setup()/loop() when a second .cpp appears in src/.
//
// This is the ONE module that intentionally breaks the "conservative
// 1:1 extraction" rule: the original loop() had two 100+-line switch
// statements inline. Extracting them as plain helpers would leave
// .ino at ~270 LOC, which is still way over the plan's <300 target.
// The minimal two-helper split (handleMenuInput / renderMenuPage)
// below gets us to ~100 LOC. No generic page-table abstraction is
// introduced — that would be a separate refactor.
//
// Cross-module dependencies (READ from other modules, must be defined
// in CG_scale.ino BEFORE this header is included):
//   - updateMenu, menuPage             from CG_scale.ino
//   - Serial                           from Arduino.h
//   - printConsole(T_*, String)        from Util.h
//   - printNewValueText()              from HX711Manager.h
//   - saveCalFactor(int)               from HX711Manager.h
//   - runAutoCalibrate()               from HX711Manager.h
//   - resetCPU()                       from Util.h
//   - WiFi.printDiag/scanNetworks/    from ESP8266WiFi.h
//     SSID/RSSI/encryptionType,
//     softAPgetStationNum
//   - wifiSTAmode                      from Wifi.h / CG_scale.ino
//   - nLoadcells, calFactorLoadcell[],  from HX711Manager.h / .ino
//     resistor[], weightLoadCell[],
//     weightTotal, CG_length, CG_trans,
//     batVolt, batType, batCells,
//     refWeight, refCG
//   - model.distance[], model.name     from Models.h
//   - errMsg[], errMsgCnt              from CG_scale.ino
//   - battTypName[], NUMBER_BAT_TYPES  from defaults.h
//   - CGSCALE_VERSION, B_OFF, B_VOLT,  from defaults.h
//     LC1..LC3, X1..X3, R1..R2,
//     MENU_* constants
//   - EEPROM.put/EEPROM.commit         from EEPROM.h
//   - P_NUMBER_LOADCELLS, P_DISTANCE_X1,
//     P_REF_WEIGHT, P_REF_CG,
//     P_LOADCELL1_CALIBRATION_FACTOR,
//     P_RESISTOR_R1, P_BAT_TYPE,
//     P_BATT_CELLS                      from defaults.h
//   - MODEL_FILE                        from defaults.h
//   - LittleFS.exists/remove           from LittleFS.h
//   - resetCPU()                       from Util.h
//
// What lives here:
//   - handleMenuInput(): reads Serial if input is available, dispatches
//                        the "you are on this menu page and gave me
//                        input" branch — writes the new value to the
//                        global, persists to EEPROM, jumps back to home.
//   - renderMenuPage(): prints the current menu page to Serial. Called
//                       only when updateMenu is true (initial entry
//                       into a page, or after input).
//
// What stays in CG_scale.ino loop():
//   - The `if (Serial)` guard around the menu (so the menu is only
//     active when Serial is connected)
//   - The `if (!updateMenu) return;` short-circuit (early-return from
//     loop(), original behavior — intentional "skip the rest of loop()
//     until the menu repaints")
//   - The Serial.readString() flush after input parsing
//   - The `else { updateMenu = true; }` fallback for "Serial went away"
//
// Behavior is byte-identical to the original loop() block — same
// switch order, same fall-through semantics, same global writes
// in the same order, same EEPROM persistence pattern.

#pragma once

#include <Arduino.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>

// ---------- handleMenuInput ----------
//
// Called from loop() once Serial shows available bytes. Reads the
// current value from Serial (parseInt/parseFloat/Single char for
// Y/N prompts), writes it to the matching global, persists to EEPROM
// if needed, and resets menuPage to 0 (back to home).
//
// Special cases:
//   - MENU_HOME: this is the menu-listing page. The user types the
//     number of the page they want. handleMenuInput reads that number
//     into menuPage and sets updateMenu=true so the next render
//     shows the new page.
//   - MENU_AUTO_CALIBRATE / MENU_RESET_DEFAULT: read a single char
//     ('J') to confirm. If user does not confirm, page resets to home.
//   - default: user typed something meaningless — flush the buffer,
//     reset to home.

inline void handleMenuInput() {
  switch (menuPage) {
    case MENU_HOME:
      menuPage = Serial.parseInt();
      updateMenu = true;
      break;
    case MENU_LOADCELLS:
      nLoadcells = Serial.parseInt();
      EEPROM.put(P_NUMBER_LOADCELLS, nLoadcells);
      EEPROM.commit();
      menuPage = 0;
      updateMenu = true;
      break;
    case MENU_DISTANCE_X1 ... MENU_DISTANCE_X3:
      model.distance[menuPage - MENU_DISTANCE_X1] = Serial.parseFloat();
      EEPROM.put(P_DISTANCE_X1 + ((menuPage - MENU_DISTANCE_X1) * sizeof(float)), model.distance[menuPage - MENU_DISTANCE_X1]);
      EEPROM.commit();
      menuPage = 0;
      updateMenu = true;
      break;
    case MENU_REF_WEIGHT:
      refWeight = Serial.parseFloat();
      EEPROM.put(P_REF_WEIGHT, refWeight);
      EEPROM.commit();
      menuPage = 0;
      updateMenu = true;
      break;
    case MENU_REF_CG:
      refCG = Serial.parseFloat();
      EEPROM.put(P_REF_CG, refCG);
      EEPROM.commit();
      menuPage = 0;
      updateMenu = true;
      break;
    case MENU_AUTO_CALIBRATE:
      if (Serial.read() == 'J') {
        runAutoCalibrate();
      }
      menuPage = 0;
      updateMenu = true;
      break;
    case MENU_LOADCELL1_CALIBRATION_FACTOR ... MENU_LOADCELL3_CALIBRATION_FACTOR:
      calFactorLoadcell[menuPage - MENU_LOADCELL1_CALIBRATION_FACTOR] = Serial.parseFloat();
      saveCalFactor(menuPage - MENU_LOADCELL1_CALIBRATION_FACTOR);
      menuPage = 0;
      updateMenu = true;
      break;
    case MENU_RESISTOR_R1 ... MENU_RESISTOR_R2:
      resistor[menuPage - MENU_RESISTOR_R1] = Serial.parseFloat();
      EEPROM.put(P_RESISTOR_R1 + ((menuPage - MENU_RESISTOR_R1) * sizeof(float)), resistor[menuPage - MENU_RESISTOR_R1]);
      EEPROM.commit();
      menuPage = 0;
      updateMenu = true;
      break;
    case MENU_BATTERY_MEASUREMENT:
      batType = Serial.parseInt();
      EEPROM.put(P_BAT_TYPE, batType);
      EEPROM.commit();
      menuPage = 0;
      updateMenu = true;
      break;
    case MENU_BATTERY_CELLS:
      batCells = Serial.parseInt();
      EEPROM.put(P_BATT_CELLS, batCells);
      EEPROM.commit();
      menuPage = 0;
      updateMenu = true;
      break;
    case MENU_RESET_DEFAULT:
      if (Serial.read() == 'J') {
        // reset eeprom
        for (int i = 0; i < EEPROM_SIZE; i++) {
          EEPROM.write(i, 0xFF);
        }
        Serial.end();
        EEPROM.commit();
        if (LittleFS.exists(MODEL_FILE)) {
          LittleFS.remove(MODEL_FILE);
        }
        resetCPU();
      }
      menuPage = 0;
      updateMenu = true;
      break;
    default:
      Serial.readString();
      menuPage = 0;
      updateMenu = true;
      break;
  }
  Serial.readString();
}

// ---------- renderMenuPage ----------
//
// Prints the current menu page to Serial. Each page typically shows
// the current value(s) and a prompt for the new value. menuPage is
// set to 0 (home) by handleMenuInput() after a successful edit, so
// the next render shows the home menu listing again.
//
// Special cases:
//   - MENU_HOME: lists every menu page with its number and current
//     value (the "main menu" page).
//   - MENU_SHOW_ACTUAL / MENU_WIFI_INFO: show live data, set
//     updateMenu = false so the page does not re-print every loop().
inline void renderMenuPage() {
  switch (menuPage) {
    case MENU_HOME: {
      Serial.print(F(
          "\n\n********************************************\nCG scale by "
          "M.Lehmann - V"));
      Serial.print(CGSCALE_VERSION);
      Serial.print(F("\n\n"));

      Serial.print(MENU_LOADCELLS);
      Serial.print(F("  - Set number of load cells ("));
      Serial.print(nLoadcells);
      Serial.print(F(")\n"));

      for (int i = X1; i <= X3; i++) {
        Serial.print(MENU_DISTANCE_X1 + i);
        Serial.print(F("  - Set distance X"));
        Serial.print(i + 1);
        Serial.print(F(" ("));
        Serial.print(model.distance[i]);
        Serial.print(F("mm)\n"));
      }

      Serial.print(MENU_REF_WEIGHT);
      Serial.print(F("  - Set reference weight ("));
      Serial.print(refWeight);
      Serial.print(F("g)\n"));

      Serial.print(MENU_REF_CG);
      Serial.print(F("  - Set reference CG ("));
      Serial.print(refCG);
      Serial.print(F("mm)\n"));

      Serial.print(MENU_AUTO_CALIBRATE);
      Serial.print(F("  - Start autocalibration\n"));

      for (int i = LC1; i <= LC3; i++) {
        Serial.print(MENU_LOADCELL1_CALIBRATION_FACTOR + i);
        if ((MENU_LOADCELL1_CALIBRATION_FACTOR + i) < 10)
          Serial.print(F(" "));
        Serial.print(F(" - Set calibration factor of load cell "));
        Serial.print(i + 1);
        Serial.print(F(" ("));
        Serial.print(calFactorLoadcell[i]);
        Serial.print(F(")\n"));
      }

      for (int i = R1; i <= R2; i++) {
        Serial.print(MENU_RESISTOR_R1 + i);
        Serial.print(F(" - Set value of resistor R"));
        Serial.print(i + 1);
        Serial.print(F(" ("));
        Serial.print(resistor[i]);
        Serial.print(F("ohm)\n"));
      }

      Serial.print(MENU_BATTERY_MEASUREMENT);
      Serial.print(F(" - Set battery type ("));
      Serial.print(battTypName[batType]);
      Serial.print(F(")\n"));

      Serial.print(MENU_BATTERY_CELLS);
      Serial.print(F(" - Set number of battery cells ("));
      Serial.print(batCells);
      Serial.print(F(")\n"));

      Serial.print(MENU_SHOW_ACTUAL);
      Serial.print(F(" - Show actual values\n"));

      Serial.print(MENU_WIFI_INFO);
      Serial.print(F(" - Show WiFi network info\n"));

      Serial.print(MENU_RESET_DEFAULT);
      Serial.print(F(" - Reset to factory defaults\n"));

      Serial.print(F("\n"));
      for (int i = 1; i <= errMsgCnt; i++) {
        Serial.print(errMsg[i]);
      }

      Serial.print(F("\nPlease choose the menu number:"));

      updateMenu = false;
      break;
    }
    case MENU_LOADCELLS:
      Serial.print(F("\n\nNumber of load cells: "));
      Serial.println(nLoadcells);
      printNewValueText();
      updateMenu = false;
      break;
    case MENU_DISTANCE_X1 ... MENU_DISTANCE_X3:
      Serial.print("\n\nDistance X");
      Serial.print(menuPage - MENU_DISTANCE_X1 + 1);
      Serial.print(F(": "));
      Serial.print(model.distance[menuPage - MENU_DISTANCE_X1]);
      Serial.print(F("mm\n"));
      printNewValueText();
      updateMenu = false;
      break;
    case MENU_REF_WEIGHT:
      Serial.print(F("\n\nReference weight: "));
      Serial.print(refWeight);
      Serial.print(F("g\n"));
      printNewValueText();
      updateMenu = false;
      break;
    case MENU_REF_CG:
      Serial.print(F("\n\nReference CG: "));
      Serial.print(refCG);
      Serial.print(F("mm\n"));
      printNewValueText();
      updateMenu = false;
      break;
    case MENU_AUTO_CALIBRATE:
      Serial.print(
          F("\n\nPlease put the reference weight on the scale.\nStart auto "
            "calibration (J/N)?\n"));
      updateMenu = false;
      break;
    case MENU_LOADCELL1_CALIBRATION_FACTOR ... MENU_LOADCELL3_CALIBRATION_FACTOR:
      Serial.print("\n\nCalibration factor of load cell ");
      Serial.print(menuPage - MENU_LOADCELL1_CALIBRATION_FACTOR + 1);
      Serial.print(F(": "));
      Serial.println(
          calFactorLoadcell[menuPage - MENU_LOADCELL1_CALIBRATION_FACTOR]);
      printNewValueText();
      updateMenu = false;
      break;
    case MENU_RESISTOR_R1 ... MENU_RESISTOR_R2:
      Serial.print(F("\n\nValue of resistor R"));
      Serial.print(menuPage - MENU_RESISTOR_R1 + 1);
      Serial.print(F(": "));
      Serial.println(resistor[menuPage - MENU_RESISTOR_R1]);
      printNewValueText();
      updateMenu = false;
      break;
    case MENU_BATTERY_MEASUREMENT: {
      Serial.print(F("\n\nBattery type: "));
      Serial.println(battTypName[batType]);
      for (int i = 0; i < NUMBER_BAT_TYPES; i++) {
        Serial.print(i);
        Serial.print(" = ");
        Serial.println(battTypName[i]);
      }
      printNewValueText();
      updateMenu = false;
      break;
    }
    case MENU_BATTERY_CELLS:
      Serial.print(F("\n\nBattery cells: "));
      Serial.println(batCells);
      printNewValueText();
      updateMenu = false;
      break;
    case MENU_SHOW_ACTUAL:
      for (int i = LC1; i <= LC3; i++) {
        if (i < nLoadcells) {
          Serial.print(F("Lc"));
          Serial.print(i + 1);
          Serial.print(F(": "));
          Serial.print(weightLoadCell[i]);
          Serial.print(F("g  "));
        }
      }
      Serial.print(F("Total weight: "));
      Serial.print(weightTotal);
      Serial.print(F("g  CG length: "));
      Serial.print(CG_length);
      if (nLoadcells == 3) {
        Serial.print(F("mm  CG trans: "));
        Serial.print(CG_trans);
        Serial.print(F("mm"));
      }
      if (batType > B_OFF) {
        Serial.print(F("  Battery:"));
        Serial.print(batVolt);
        if (batType == B_VOLT) {
          Serial.print(F("V"));
        } else {
          Serial.print(F("%"));
        }
      }
      Serial.println();
      break;
    case MENU_WIFI_INFO: {
      Serial.println(
          "\n\n********************************************\nWiFi network "
          "information\n");

      Serial.println("# Current WiFi status:");
      WiFi.printDiag(Serial);
      if (wifiSTAmode == false) {
        Serial.print("Connected clients: ");
        Serial.println(WiFi.softAPgetStationNum());
      }

      Serial.println("\n# Available WiFi networks:");
      int wifiCnt = WiFi.scanNetworks();
      if (wifiCnt == 0) {
        Serial.println("no networks found");
      } else {
        for (int i = 0; i < wifiCnt; ++i) {
          Serial.print(i + 1);
          Serial.print(": ");
          Serial.print(WiFi.SSID(i));
          Serial.print(" (");
          Serial.print(WiFi.RSSI(i));
          Serial.print("dBm) ");
          switch (WiFi.encryptionType(i)) {
            case ENC_TYPE_WEP:
              Serial.print("WEP");
              break;
            case ENC_TYPE_TKIP:
              Serial.print("WPA");
              break;
            case ENC_TYPE_CCMP:
              Serial.print("WPA2");
              break;
            case ENC_TYPE_AUTO:
              Serial.print("Auto");
              break;
          }
          Serial.println("");
        }
      }
    }
      updateMenu = false;
      break;
    case MENU_RESET_DEFAULT:
      Serial.print(F("\n\nReset to factory defaults (J/N)?\n"));
      updateMenu = false;
      break;
  }
}
