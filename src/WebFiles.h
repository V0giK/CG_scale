// WebFiles.h — V2.5.x PR 3a step 2
//
// Owns the LittleFS-backed web file serving: MIME-type lookup, GET read,
// and POST upload. Extracted from CG_scale.ino as a header-only module
// — see docs/REFACTOR_PLAN.md.
//
// Header-only rationale: same as Battery.h, Util.h, Display.h,
// HX711Manager.h, Models.h, Wifi.h — see the pio-windows-esp8266 skill.
// PIO 6.x on Windows drops setup()/loop() when a second .cpp appears
// in src/.
//
// Cross-module dependencies (READ from other modules, must be defined
// in CG_scale.ino BEFORE this header is included):
//   - server (ESP8266WebServer)    from CG_scale.ino (line ~115). All three
//                                  functions call into server.* methods.
//   - MODEL_FILE                   from defaults.h (compared against the
//                                  upload filename in handleFileUpload)
//
// What lives here:
//   - fsUploadFile                 file-static (was a .ino-global, but only
//                                  handleFileUpload touched it — so it
//                                  becomes private to the module)
//   - getContentType(String)       MIME-type lookup
//   - handleFileRead(String)       GET a file from LittleFS (with .gz
//                                  fallback)
//   - handleFileUpload()           POST a file to LittleFS (model upload)
//
// What stays in CG_scale.ino:
//   - server.on("/settings.html", HTTP_POST, ..., handleFileUpload)  (line ~795)
//     — route registration is WebApi territory, will move in PR 3b
//   - handleFileRead(server.uri()) call inside the not-found handler
//     (line ~798) — same reason, stays until PR 3b

#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <ESP8266WebServer.h>

// ---------- file-static upload handle ----------
//
// Was a .ino-global (line ~118) used only inside handleFileUpload().
// file-static gives it proper module-private state without leaking into
// the .ino symbol table.

static File fsUploadFile;

// ---------- getContentType ----------
//
// Pure MIME-type lookup by file extension. No external dependencies.

inline String getContentType(String filename) {
  if (filename.endsWith(".html"))
    return "text/html";
  else if (filename.endsWith(".png"))
    return "image/png";
  else if (filename.endsWith(".css"))
    return "text/css";
  else if (filename.endsWith(".js"))
    return "application/javascript";
  else if (filename.endsWith(".map"))
    return "application/json";
  else if (filename.endsWith(".ico"))
    return "image/x-icon";
  else if (filename.endsWith(".gz"))
    return "application/x-gzip";
  return "text/plain";
}

// ---------- handleFileRead ----------
//
// GET: stream a file from LittleFS to the client. Folder requests get
// redirected to index.html. .gz-precompressed files are preferred when
// both exist (smaller payload, same content-type negotiation).
//
// Returns true if the file was sent, false if it doesn't exist
// (caller typically falls through to a 404).

inline bool handleFileRead(String path) {
  // If a folder is requested, send the index file
  if (path.endsWith("/")) {
    path += "index.html";
  }

  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";

  // If the file exists, either as a compressed archive, or normal
  if (LittleFS.exists(pathWithGz) || LittleFS.exists(path)) {
    if (LittleFS.exists(pathWithGz)) {
      path += ".gz";
    }
    File file = LittleFS.open(path, "r");
    server.streamFile(file, contentType);
    file.close();
    return true;
  }

  return false;
}

// ---------- handleFileUpload ----------
//
// POST: write an uploaded file to LittleFS. Currently only MODEL_FILE
// is accepted — any other filename gets a 500 and the upload aborts.
// On successful end-of-upload, redirects the client to /settings.html.

inline void handleFileUpload() {
  HTTPUpload &upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (!filename.startsWith("/")) {
      filename = "/" + filename;
    }

    if (filename != MODEL_FILE) {
      server.send(500, "text/plain", "wrong file !");
      return;
    }

    // Open the file for writing in LittleFS (create if it doesn't exist)
    fsUploadFile = LittleFS.open(filename, "w");
    filename = String();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    // Write the received bytes to the file
    fsUploadFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    // If the file was successfully created
    if (fsUploadFile) {
      fsUploadFile.close();
      // Redirect the client to the success page
      server.sendHeader("Location", "/settings.html");
      server.send(303);
    } else {
      server.send(500, "text/plain", "500: couldn't create file");
    }
  }
}
