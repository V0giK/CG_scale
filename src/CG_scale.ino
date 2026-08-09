/*
  ------------------------------------------------------------------
                            CG scale
                      (c) 2019-2020 by M. Lehmann
                      (c) 2025 by T. Schwartau
  ------------------------------------------------------------------
*/
#define CGSCALE_VERSION "2.4.0"
/*

  ******************************************************************
  history:
   V2.4.0  03.01.25     Restructured files to use Platformio
                        Configured Platformio
  V2.3.4  26.09.24     settings.html text fix SPIFFS to LittleFS
                       Release 2.3.4
  V2.3.3  26.09.24     fix Litt.leFS
  V2.3.2  26.09.24     Change from SPIFFS to LittleFS
  V2.3.1  26.09.24     Compiler Errors fixes
  V2.3    01.03.22     Up to three ESPs can be linked via WLAN. Useful for landing gear scales on engine models
  V2.22   28.11.20     fixed RAM problems with JSON
  V2.21   27.11.20     bug fixed: recompiled, binary file incorrect
  V2.2    01.11.20     Virtual weights built in
  V2.12   07.10.20     bug fixed: LR value was displayed in the wrong display position
                       Voltage for specified battery types deleted
  V2.11   18.08.20     code is now compatible with standard OLED displays
                       and original code base (default pw length = 32)
  V2.1    18.07.20     added support for ESP8266 based Wifi Kit 8
  (by Pulsar07/           (https://heltec.org/project/wifi-kit-8/)
   R.Stransky             is a ESP8266 with
                            a build in OLED 128x32
                            battery connector with charging management
                            reset and GPIO0 button
                        support for a tare button (PIN_TARE_BUTTON)
                        bug fixed: wifi password now with up to 64 chars
                        bug fixed: wifi data (ssid/passwd) with special
                          character (e.g. +) is now supported
                        for specified battery type, voltage is displayed
                        using uncompressed html files makes WEB GUI much faster
  V2.01   29.01.20      small bug fixes with AVR
  V2.0    26.01.20      Webpage rewritten, no bootstrap framework needed
                        add translation to webpage (en, de)
                        optimized for measuring with landinggears
                        updated to ArduinoJson V6
                        firmware update over web interface
  V1.2.1  31.03.19      small bug fixed
                        values in model database are rounded
                        mDNS and OTA did not work in AP mode
  V1.2    23.02.19      Add OTA (over the air update)
                        mDNS default enabled
                        add percentlists for many battery types
                        memory optimization
  V1.1    02.02.19      Supports ESP8266, webpage integrated, STA and AP mode
  V1.0    12.01.19      first release


  ******************************************************************

  Software License Agreement (BSD License)

  Copyright (c) 2019-2020, Michael Lehmann
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:
  1. Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.
  2. Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.
  3. Neither the name of the copyright holders nor the
  names of its contributors may be used to endorse or promote products
  derived from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ''AS IS'' AND ANY
  EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/



// Required libraries, can be installed from the library manager
#include <HX711_ADC.h>  // library for the HX711 24-bit ADC for weight scales (https://github.com/olkal/HX711_ADC)
#include <U8g2lib.h>    // Universal 8bit Graphics Library (https://github.com/olikraus/u8g2/)

// built-in libraries
#include <EEPROM.h>
#include <Wire.h>

#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ElegantOTA.h>
#include <LittleFS.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>

#include "settings_ESP8266.h"

// HX711 constructor array (dout pin, sck pint):
HX711_ADC LoadCell[]{HX711_ADC(PIN_LOADCELL1_DOUT, PIN_LOADCELL1_PD_SCK),
                     HX711_ADC(PIN_LOADCELL2_DOUT, PIN_LOADCELL2_PD_SCK),
                     HX711_ADC(PIN_LOADCELL3_DOUT, PIN_LOADCELL3_PD_SCK)};

ESP8266WebServer server(80);
WiFiClientSecure httpsClient;

#include "defaults.h"

struct VirtualWeight {
  String name;
  float cg;
  float weight;
  bool enabled = false;
};

struct Model {
  float distance[3] = {DISTANCE_X1, DISTANCE_X2, DISTANCE_X3};
  char name[MAX_MODELNAME_LENGHT + 1] = "";
  float targetCGmin = 0;
  float targetCGmax = 0;
  uint8_t mechanicsType = 0;
  VirtualWeight virtualWeight[MAX_VIRTUAL_WEIGHT];
};

Model model;

// load default values
uint8_t nLoadcells = NUMBER_LOADCELLS;
float calFactorLoadcell[] = {LOADCELL1_CALIBRATION_FACTOR, LOADCELL2_CALIBRATION_FACTOR, LOADCELL3_CALIBRATION_FACTOR};
float resistor[] = {RESISTOR_R1, RESISTOR_R2};
uint8_t batType = BAT_TYPE;
uint8_t batCells = BAT_CELLS;
float refWeight = REF_WEIGHT;
float refCG = REF_CG;
char loadCellURL[3][MAX_SSID_PW_LENGHT + 1] = {"", "", ""};
bool enableOTA = ENABLE_OTA;
float weightLoadCell[] = {0, 0, 0};
float lastWeightLoadCell[] = {0, 0, 0};
float weightTotal = 0;
float CG_length = 0;
float CG_trans = 0;
float batVolt = 0;
unsigned long lastTimeMenu = 0;
unsigned long lastTimeLoadcell = 0;
bool updateMenu = true;
int menuPage = 0;
String errMsg[5];
int errMsgCnt = 0;
const uint8_t *oledFontBig;
const uint8_t *oledFontLarge;
const uint8_t *oledFontNormal;
const uint8_t *oledFontSmall;

// System helpers + console logging — see Util.h
#include "Util.h"

// Battery module — see Battery.h
#include "Battery.h"

// OLED display rendering — see Display.h
#include "Display.h"

// HX711 loadcell hardware + tare button — see HX711Manager.h
#include "HX711Manager.h"

// Model persistence (EEPROM + LittleFS JSON) — see Models.h
#include "Models.h"

// WiFi credentials, mode, and setupWifi() — see Wifi.h
#include "Wifi.h"

// LittleFS-backed web file serving (MIME / GET / POST upload) — see WebFiles.h
#include "WebFiles.h"

// OTA update probe + progress screen — see OtaUpdate.h
#include "OtaUpdate.h"

// HTTP GET handlers (state-to-client) — see WebApi.h
#include "WebApi.h"

// HTTP GET handlers moved to WebApi.h — see getHead(), getValue(),
// getRawValue(), getParameter(), getVirtualWeight(), getWiFiNetworks()

// HTTP handlers moved to WebApi.h — see getHead(), getValue(),
// getRawValue(), getParameter(), getVirtualWeight(), getWiFiNetworks()
// (READ) and saveParameter(), autoCalibrate(), runTare(), saveModel(),
// openModel(), deleteModel() (WRITE).

// Serial console menu (input parser + page renderer) — see Menu.h
#include "Menu.h"

// EEPROM settings load — see Settings.h
#include "Settings.h"

// CG (centre-of-gravity) calculation — see Calc.h
#include "Calc.h"

// Web file serving moved to WebFiles.h — see getContentType(),
// handleFileRead(), handleFileUpload()

// OTA update probe + progress screen (updateMsg, gitVersion, enableUpdate,
// printUpdateProgress, httpsUpdate) moved to OtaUpdate.h

void setup() {
  // init serial
  Serial.begin(115200);
  Serial.println();
  delay(1000);

  printConsole(T_BOOT, "startup CG scale V" + String(CGSCALE_VERSION));

  LittleFS.begin();
  EEPROM.begin(EEPROM_SIZE);
  printConsole(T_BOOT, "init filesystem");

  // read settings from eeprom — see Settings.h
  loadSettings();

  printConsole(T_BOOT, "open last model");
  if (!openModelJson(model.name)) {
    saveModelJson(DEFAULT_NAME);
    openModelJson(DEFAULT_NAME);
  }

  // init OLED display
  initOLED();

  // init & tare Loadcells — see HX711Manager.h initLoadcells()
  initLoadcells();

  // stabilize scale values
  while (millis() < STABILISINGTIME) {
    updateLoadcells();
  }

  tareLoadcells();
  getLoadcellError();

  // WiFi init moved to Wifi.h — see setupWifi()
  setupWifi();

  delay(3000);

  // HTTP route registration — see WebApi.h setupWebApi()
  setupWebApi();

  server.begin();
  printConsole(T_RUN, "Webserver is up and running");

  if (enableOTA) {
    // ArduinoOTA setup — see OtaUpdate.h setupArduinoOTA()
    setupArduinoOTA();
  }

  httpsClient.setInsecure();
  if (enableUpdate) {
    httpsUpdate(PROBE_UPDATE);
  }

}

void loop() {

#if ENABLE_MDNS
  MDNS.update();
#endif

  if (enableOTA) {
    ArduinoOTA.handle();
  }
  server.handleClient();

#ifdef PIN_TARE_BUTTON
  handleTareBtn();
#endif

  updateLoadcells();

  // update loadcell values
  if ((millis() - lastTimeLoadcell) > UPDATE_INTERVAL_LOADCELL) {
    lastTimeLoadcell = millis();

    errMsgCnt = 0;
    getLoadcellError();

    // read battery voltage — stays in .ino (Battery domain, writes batVolt)
    if (batType > B_OFF) {
      batVolt = (analogRead(VOLTAGE_PIN) / 1024.0) * V_REF * ((resistor[R1] + resistor[R2]) / resistor[R2]) / 1000.0;
#if ENABLE_PERCENTLIST
      if (batType > B_VOLT) {
        batVolt = percentBat(batVolt / batCells);
      }
#endif
    }

    // get Loadcell weights — moved to HX711Manager.h pollLoadcells()
    pollLoadcells();
  }

  // update display and serial menu
  if ((millis() - lastTimeMenu) > UPDATE_INTERVAL_OLED_MENU) {
    lastTimeMenu = millis();

    // total model weight + CG — see Calc.h
    calcCG();

    printScaleOLED();

    // serial connection — see Menu.h
    if (Serial) {
      if (Serial.available() > 0) {
        handleMenuInput();
        Serial.readString();
      }
      if (!updateMenu) return;
      renderMenuPage();
    } else {
      updateMenu = true;
    }
  }
}
