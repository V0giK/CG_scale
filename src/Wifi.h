// Wifi.h — V2.5.x PR 3a step 1
//
// Owns the WiFi credentials, the loadCellURL remote-scale URLs, and the
// WiFi mode state (STA vs AP fallback). Extracted from CG_scale.ino as
// a header-only module — see docs/REFACTOR_PLAN.md.
//
// Header-only rationale: same as Battery.h, Util.h, Display.h,
// HX711Manager.h, Models.h — see the pio-windows-esp8266 skill. PIO 6.x
// on Windows drops setup()/loop() when a second .cpp appears in src/.
//
// Cross-module dependencies (READ from other modules, must be defined
// in CG_scale.ino BEFORE this header is included):
//   - printConsole(int, String)         from Util.h
//   - printOLED(const String&, ...)     from Display.h (called from setupWifi()
//                                       to show the WiFi status screen)
//   - nLoadcells                        from CG_scale.ino (read here)
//   - loadCellURL[][]                   from CG_scale.ino — STAYS in the .ino,
//                                       not Wifi.h, because Display.h and
//                                       HX711Manager.h both read it and are
//                                       included BEFORE Wifi.h. Moving it here
//                                       would break the include order. Wifi.h
//                                       reads it through header-concat.
//   - ip[]                              from settings_ESP8266.h (static AP
//                                       subnet; apIP is constructed locally
//                                       inside setupWifi() so it doesn't need
//                                       to be a .ino-global)
//
// What lives here:
//   - WiFi credentials (device_Name, ssid_*, password_*, loadCellURL[][])
//   - wifiSTAmode flag
//   - waitWiFiconnected(): the STA connect wait loop
//   - setupWifi(): full WiFi init block formerly inline in setup()
//                  (STA connect → AP fallback → mDNS → printOLED status)
//
// What stays in CG_scale.ino:
//   - EEPROM.get() calls in setup() that populate ssid_STA/password_STA/...
//     (they write into these variables; the variables are extern to the .ino
//     and visible through header concatenation)

#pragma once

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>

// ---------- WiFi credentials & URL state ----------
//
// MAX_SSID_PW_LENGHT comes from settings_ESP8266.h (must be included before
// this header from CG_scale.ino).

char device_Name[MAX_SSID_PW_LENGHT + 1] = SSID_AP;
char ssid_STA[MAX_SSID_PW_LENGHT + 1] = SSID_STA;
char password_STA[MAX_SSID_PW_LENGHT + 1] = PASSWORD_STA;
char ssid_AP[MAX_SSID_PW_LENGHT + 1] = SSID_AP;
char password_AP[MAX_SSID_PW_LENGHT + 1] = PASSWORD_AP;
bool wifiSTAmode = true;

// ---------- Forward declaration ----------
//
// setupWifi() and waitWiFiconnected() reference each other
// (setupWifi() calls waitWiFiconnected()). inline functions need a
// forward declaration when they call each other.

inline void waitWiFiconnected();

// ---------- waitWiFiconnected ----------
//
// Polls WiFi.status() every 500 ms until connected, breaks on
// WL_NO_SSID_AVAIL / WL_CONNECT_FAILED / TIMEOUT_CONNECT.
// Same logic as the original CG_scale.ino implementation.

inline void waitWiFiconnected() {
  long timeoutWiFi = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    if (WiFi.status() == WL_NO_SSID_AVAIL) {
      printConsole(T_ERROR, "\nWifi: No SSID available");
      break;
    } else if (WiFi.status() == WL_CONNECT_FAILED) {
      printConsole(T_ERROR, "\nWifi: Connection failed");
      break;
    } else if ((millis() - timeoutWiFi) > TIMEOUT_CONNECT) {
      printConsole(T_ERROR, "\nWifi: Timeout");
      break;
    }
  }
}

// ---------- setupWifi ----------
//
// Formerly inline in setup() at lines ~798–849. Behaviour is identical:
//
//   1. Set WiFi persistent off, mode STA, begin(ssid_STA, password_STA)
//   2. waitWiFiconnected()
//   3. If single-loadcell AND still not connected, retry with AP creds
//      (legacy fallback for hardware variants with only one physical cell)
//   4. If still not connected after step 3, switch to AP mode and create
//      a soft access point with the static apIP from settings_ESP8266.h
//   5. Register mDNS hostname (only if ENABLE_MDNS)
//   6. Print the WiFi status screen to the OLED
//
// Cross-module dependencies consumed by setupWifi():
//   - printConsole(T_*, String)   from Util.h
//   - printOLED(...)              from Display.h
//   - nLoadcells                  from CG_scale.ino (read here)
//   - ip[]                        from settings_ESP8266.h (static AP subnet)

inline void setupWifi() {
  printConsole(T_BOOT, "Wifi: STA mode - connecting with: " + String(ssid_STA));

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid_STA, password_STA);

  waitWiFiconnected();

  if (nLoadcells == 1 && WiFi.status() != WL_CONNECTED) {
    WiFi.begin(ssid_AP, password_AP);
    waitWiFiconnected();
  }

  if (WiFi.status() != WL_CONNECTED) {
    wifiSTAmode = false;
    printConsole(T_BOOT, "Wifi: AP mode - create access point: " + String(ssid_AP));
    WiFi.mode(WIFI_AP);
    IPAddress apIP(ip[0], ip[1], ip[2], ip[3]);
    WiFi.softAPConfig(apIP, apIP,
                      IPAddress(255, 255, 255, 0));
    WiFi.softAP(ssid_AP, password_AP);
    printConsole(T_RUN, "Wifi: Connected, IP: " + String(WiFi.softAPIP().toString()));
  } else {
    printConsole(T_RUN, "Wifi: Connected, IP: " + String(WiFi.localIP().toString()));
  }

  String hostname = "disabled";
#if ENABLE_MDNS
  hostname = device_Name;
  hostname.replace(" ", "");
  hostname.toLowerCase();
  if (!MDNS.begin(hostname, WiFi.localIP())) {
    hostname = "mDNS failed";
    printConsole(T_ERROR, "Wifi: " + hostname);
  } else {
    hostname += ".local";
    printConsole(T_RUN, "Wifi hostname: " + hostname);
  }
#endif

  if (wifiSTAmode) {
    printOLED("WiFi: " + String(ssid_STA),
              "Host: " + String(hostname),
              "IP: " + WiFi.localIP().toString());
  } else {
    printOLED("WiFi: " + String(ssid_AP),
              "Host: " + String(hostname),
              "IP: " + WiFi.softAPIP().toString());
  }
}
