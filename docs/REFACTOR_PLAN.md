# CG_scale — V2.5.x Refactor Plan (V2.4.2 baseline)

This document tracks the planned refactor of `CG_scale.ino` (1924 LOC) on the
**V2.4.2** code base. Previous attempt on V2.4.0 (with ESPNow) is abandoned —
that code no longer exists in `master`.

**Build baseline (V2.4.2, nodemcuv2):**
```
RAM:   35580 bytes (43.4% of 81920)
Flash: 581335 bytes (55.7% of 1044464)
Build: SUCCESS in ~52 s
```

## Function inventory (39 functions in CG_scale.ino)

| Cluster | # | Line range | Functions |
|---|---|---|---|
| System / Util | 2 | 179-261 | `resetCPU`, `printConsole` |
| Display / OLED | 3 | 258-363 | `initOLED`, `printOLED`, `printScaleOLED` |
| Battery | 1 | 196-226 | `percentBat` |
| Hardware Input | 1 | 450-480 | `handleTareBtn` |
| Loadcell / Weight | 6 | 472-558 | `saveCalFactor`, `updateLoadcells`, `tareLoadcells`, `printNewValueText`, `runAutoCalibrate`, `getLoadcellError` |
| Model Persistence | 7 | 545-1058 | `writeModelData`, `saveModelJson`, `openModelJson`, `deleteModelJson`, `saveModel`, `openModel`, `deleteModel` |
| HTTP API (server.on) | 9 | 719-1004 | `getHead`, `getValue`, `getRawValue`, `getParameter`, `getVirtualWeight`, `getWiFiNetworks`, `saveParameter`, `autoCalibrate`, `runTare` |
| Web Server / FS | 3 | 1039-1112 | `getContentType`, `handleFileRead`, `handleFileUpload` |
| OTA / HTTPS Update | 2 | 1116-1165 | `printUpdateProgress`, `httpsUpdate` |
| WiFi / Network | 1 | 1184-1214 | `waitWiFiconnected` |
| Lifecycle | 2 | 1201-1493 | `setup`, `loop` |

Global state currently in the .ino (will move to modules):
- `Model model` + `VirtualWeight` struct
- `nLoadcells`, `calFactorLoadcell[]`, `resistor[]`
- `batType`, `batCells`, `refWeight`, `refCG`, `batVolt`
- `weightTotal`, `CG_length`, `CG_trans`
- `lastTimeMenu`, `lastTimeLoadcell`, `updateMenu`, `menuPage`
- `errMsgCnt`, `updateMsg`, `wifiSTAmode`, `gitVersion`
- WiFi credentials: `device_Name`, `ssid_STA`, `password_STA`, `ssid_AP`, `password_AP`, `loadCellURL[][]`
- `HX711_ADC LoadCell[]` (global constructor array)

## Target layout (header-only modules)

PlatformIO 6.x on Windows has documented issues with multiple `.cpp` files in
`src/` (see `pio-windows-esp8266` skill). Modules are therefore **header-only**
with `inline` implementations and file-static state. Behavior is identical to
the V2.4.0 attempt; only the functions being extracted are different.

```
src/
  CG_scale.ino            # setup() + loop() + module wiring (target: <300 LOC)
  defaults.h              # unchanged — enums, EEPROM addresses, battery tables
  settings_ESP8266.h      # unchanged — pin definitions, WiFi creds, etc.

  # V2.5.x modules (each .h is self-contained, inline impl):
  Util.h                  # resetCPU(), printConsole() + boot/update helpers
  Battery.h               # percentBat() + percentList[] accessor
  Display.h               # initOLED(), printOLED(), printScaleOLED() + oledFont* helpers
  HX711Manager.h          # LoadCell[] wrapper, updateLoadcells(), tareLoadcells(),
                          # saveCalFactor(), runAutoCalibrate(), getLoadcellError(),
                          # printNewValueText(), handleTareBtn()
                          # Owns: weightTotal, CG_length, CG_trans, batVolt,
                          # nLoadcells, calFactorLoadcell[], resistor[],
                          # refWeight, refCG, lastTimeLoadcell
  Models.h                # Model struct, VirtualWeight struct, writeModelData(),
                          # save/open/deleteModelJson(), save/open/deleteModel()
                          # Owns: Model model
  WebApi.h                # all `server.on(...)` handlers (getHead, getValue, ...,
                          # saveParameter, autoCalibrate, runTare, getWiFiNetworks)
                          # Owns: HTTP route registration; calls into other modules
  WebFiles.h              # getContentType(), handleFileRead(), handleFileUpload()
                          # Owns: fsUploadFile
  OtaUpdate.h             # printUpdateProgress(), httpsUpdate(), gitVersion
                          # Owns: updateMsg, errMsgCnt, enableUpdate
  Wifi.h                  # waitWiFiconnected(), WiFi mode setup, credentials
                          # Owns: ssid_STA, password_STA, ssid_AP, password_AP,
                          # device_Name, loadCellURL[][], wifiSTAmode, lastTimeMenu,
                          # updateMenu, menuPage
  Menu.h                  # serial-console menu state machine (if extractable;
                          # currently inline in loop())
```

## Migration order (decided)

Three PRs instead of nine — pragmatic grouping by size and risk.

**PR 1 — Small utilities (low risk, easy review):**
- `Battery.h` (1 function + table)
- `Util.h` (`printConsole`, `resetCPU`)
- `Display.h` (3 functions, OLED-only)

**PR 2 — Domain logic (medium risk, biggest cluster):**
- `HX711Manager.h` (6 functions, owns LoadCell[] global as before)
- `Models.h` (structs + JSON-LittleFS plumbing)

**PR 3 — Web stack (medium-high risk, integration-heavy):**
- `Wifi.h` (credentials + station/AP setup, char arrays unchanged)
- `WebFiles.h` (FS upload/download)
- `OtaUpdate.h` (HTTPS release check)
- `WebApi.h` (route registration, thin glue)

**Final PR — `CG_scale.ino` shrinkage:**
- Only `setup()` + `loop()` + tiny glue code
- Target: <300 LOC (from 1924)

**Decisions made upfront to minimize risk:**
- `HX711_ADC LoadCell[]` stays global (no singleton wrapper — pure 1:1 extraction)
- WiFi credentials stay as `char[]` (no `Credentials` POD)
- Menu state machine stays inline in `loop()` (no `Menu.h`)

After each PR:
- `pio run -e nodemcuv2` → SUCCESS
- RAM exactly `35580` bytes
- Flash within ±200 bytes of `581335`

## What this refactor deliberately does NOT do

- No ArduinoJson v6 → v7 migration
- No `String` → `char[]` cleanup
- No algorithm changes
- No new features (no re-introduction of ESPNow)
- No library version bumps
- No AVR / WiFi-Kit-8 re-introduction (those code paths were removed on purpose)

## Open questions (to ask user before starting)

1. Module-by-module PRs (one commit-cluster per module, easy to review) or
   single mega-PR (easier to revert, but harder to review)?
2. Should `HX711Manager` keep the global `HX711_ADC LoadCell[]` or wrap it
   in a class-like singleton struct (more idiomatic, but more churn)?
3. WiFi credentials currently `char[]` — leave as-is, or introduce a tiny
   `Credentials` POD type (e.g. for OTA URL display consistency)?
4. Keep the Menu code in `loop()` as-is, or extract a small `Menu.h`?

Once answered, start with step 1 (Battery.h).