
#include "ElegantOTA.h"
#include "ota.h"

ElegantOTAClass::ElegantOTAClass(){}

void ElegantOTAClass::begin(ELEGANTOTA_WEBSERVER *server, const char * username, const char * password){
  _server = server;

  setAuth(username, password);

  #if defined(TARGET_RP2040)
    if (!__isPicoW) {
      ELEGANTOTA_DEBUG_MSG("RP2040: Not a Pico W, skipping OTA setup\n");
      return;
    }
  #endif

  #if ELEGANTOTA_USE_ASYNC_WEBSERVER == 1
    _server->on("/update", HTTP_GET, [&](AsyncWebServerRequest *request){
      if(_authenticate && !request->authenticate(_username.c_str(), _password.c_str())){
        return request->requestAuthentication();
      }
      #if defined(ASYNCWEBSERVER_VERSION) && ASYNCWEBSERVER_VERSION_MAJOR > 2  // This means we are using recommended fork of AsyncWebServer
        AsyncWebServerResponse *response = request->beginResponse(200, "text/html", ota_html, ota_html_len);
      #else
        AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", ota_html, ota_html_len);
      #endif
      //response->addHeader("Content-Encoding", "gzip");
      response->addHeader("Server","ESP Async Web Server");
      request->send(response);
    });
  #else
    _server->on("/update", HTTP_GET, [&](){
      if (_authenticate && !_server->authenticate(_username.c_str(), _password.c_str())) {
        return _server->requestAuthentication();
      }
      //_server->sendHeader("Content-Encoding", "gzip");
      _server>sendHeader("Server","ESP Async Web Server");
      _server->send_P(200, "text/html", (const char*)ota_html, ota_html_len);
    });
  #endif

  #if ELEGANTOTA_USE_ASYNC_WEBSERVER == 1
    _server->on("/ota/start", HTTP_GET, [&](AsyncWebServerRequest *request) {
      if (_authenticate && !request->authenticate(_username.c_str(), _password.c_str())) {
        return request->requestAuthentication();
      }

      // Get header x-ota-mode value, if present
      OTA_Mode mode = OTA_MODE_FIRMWARE;
      // Get mode from arg
      if (request->hasParam("mode")) {
        String argValue = request->getParam("mode")->value();
        if (argValue == "fs") {
          Serial.print("OTA Mode: Filesystem\n");
          mode = OTA_MODE_FILESYSTEM;
        } else {
          Serial.print("OTA Mode: Firmware\n");
          mode = OTA_MODE_FIRMWARE;
        }
      }

      #if UPDATE_DEBUG == 1
        // Serial output must be active to see the callback serial prints
        Serial.setDebugOutput(true);
      #endif

      // TM-46: a prior upload that stalled or dropped its connection can leave
      // Update.begin() still open. Clean that up before starting fresh instead
      // of letting the begin() below fail with a stale-session 400.
      abortActiveUpdate("stale_session");

      // TM-46: bump the session generation -- a disconnect captured under an
      // older generation (e.g. a prior upload's connection that AsyncTCP only
      // now gets around to reporting) can then be told apart from one
      // belonging to this fresh session.
      _updateGeneration++;

      // TM-49: a new session starts unverified, with no inherited error text.
      // Only a completed, MD5-checked image may set the flag back to true.
      _ota_image_valid = false;
      _update_error_str = "";

      // Pre-OTA update callback
      if (preUpdateCallback != NULL) preUpdateCallback();

      // Start update process
      #if defined(ESP8266)
        uint32_t update_size = mode == OTA_MODE_FILESYSTEM ? ((size_t)FS_end - (size_t)FS_start) : ((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000);
        if (mode == OTA_MODE_FILESYSTEM) {
          close_all_fs();
        }
        Update.runAsync(true);
        if (!Update.begin(update_size, mode == OTA_MODE_FILESYSTEM ? U_FS : U_FLASH)) {
          ELEGANTOTA_DEBUG_MSG("Failed to start update process\n");
          // Save error to string
          StreamString str;
          Update.printError(str);
          _update_error_str = str.c_str();
          _update_error_str.concat("\n");
          ELEGANTOTA_DEBUG_MSG(_update_error_str.c_str());
        }
      #elif defined(ESP32)
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, mode == OTA_MODE_FILESYSTEM ? U_SPIFFS : U_FLASH)) {
          Serial.print("Failed to start update process\n");
          // Save error to string
          StreamString str;
          Update.printError(str);
          _update_error_str = str.c_str();
          _update_error_str.concat("\n");
          Serial.println(_update_error_str.c_str());
        }     
        // Get file MD5 hash from arg
        if (request->hasParam("hash")) {
          String hash = request->getParam("hash")->value();
          Serial.print(String("MD5 from client: "+hash+"\n").c_str());
          if (!Update.setMD5(hash.c_str())) {
            Serial.print("ERROR: MD5 hash not valid\n");
            return request->send(400, "text/plain", "MD5 parameter invalid");
          }
        }   
      #endif

      return request->send((Update.hasError()) ? 400 : 200, "text/plain", (Update.hasError()) ? _update_error_str.c_str() : "OK");
    });
  #else
    _server->on("/ota/start", HTTP_GET, [&]() {
      if (_authenticate && !_server->authenticate(_username.c_str(), _password.c_str())) {
        return _server->requestAuthentication();
      }

      // TM-49: same fail-closed reset as the async /ota/start above.
      _ota_image_valid = false;
      _update_error_str = "";

      // Get header x-ota-mode value, if present
      OTA_Mode mode = OTA_MODE_FIRMWARE;
      // Get mode from arg
      if (_server->hasArg("mode")) {
        String argValue = _server->arg("mode");
        if (argValue == "fs") {
          ELEGANTOTA_DEBUG_MSG("OTA Mode: Filesystem\n");
          mode = OTA_MODE_FILESYSTEM;
        } else {
          ELEGANTOTA_DEBUG_MSG("OTA Mode: Firmware\n");
          mode = OTA_MODE_FIRMWARE;
        }
      }

      // Get file MD5 hash from arg
      if (_server->hasArg("hash")) {
        String hash = _server->arg("hash");
        ELEGANTOTA_DEBUG_MSG(String("MD5: "+hash+"\n").c_str());
        if (!Update.setMD5(hash.c_str())) {
          ELEGANTOTA_DEBUG_MSG("ERROR: MD5 hash not valid\n");
          return _server->send(400, "text/plain", "MD5 parameter invalid");
        }
      }

      #if UPDATE_DEBUG == 1
        // Serial output must be active to see the callback serial prints
        Serial.setDebugOutput(true);
      #endif

      // Pre-OTA update callback
      if (preUpdateCallback != NULL) preUpdateCallback();

      // Start update process
      #if defined(ESP8266)
        uint32_t update_size = mode == OTA_MODE_FILESYSTEM ? ((size_t)FS_end - (size_t)FS_start) : ((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000);
        if (mode == OTA_MODE_FILESYSTEM) {
          close_all_fs();
        }
        Update.runAsync(true);
        if (!Update.begin(update_size, mode == OTA_MODE_FILESYSTEM ? U_FS : U_FLASH)) {
          ELEGANTOTA_DEBUG_MSG("Failed to start update process\n");
          // Save error to string
          StreamString str;
          Update.printError(str);
          _update_error_str = str.c_str();
          _update_error_str.concat("\n");
          ELEGANTOTA_DEBUG_MSG(_update_error_str.c_str());
        }
      #elif defined(ESP32)  
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, mode == OTA_MODE_FILESYSTEM ? U_SPIFFS : U_FLASH)) {
          ELEGANTOTA_DEBUG_MSG("Failed to start update process\n");
          // Save error to string
          StreamString str;
          Update.printError(str);
          _update_error_str = str.c_str();
          _update_error_str.concat("\n");
          ELEGANTOTA_DEBUG_MSG(_update_error_str.c_str());
        }
      #elif defined(TARGET_RP2040)
        uint32_t update_size = 0;
        // Gather FS Size
        if (mode == OTA_MODE_FILESYSTEM) {
          update_size = ((size_t)&_FS_end - (size_t)&_FS_start);
          SPIFFS.end();
        } else {
          FSInfo i;
          SPIFFS.begin();
          SPIFFS.info(i);
          update_size = i.totalBytes - i.usedBytes;
        }
        // Start update process
        if (!Update.begin(update_size, mode == OTA_MODE_FILESYSTEM ? U_FS : U_FLASH)) {
          ELEGANTOTA_DEBUG_MSG("Failed to start update process because there is not enough space\n");
          _update_error_str = "Not enough space";
          return _server->send(400, "text/plain", _update_error_str.c_str());
        }
      #endif

      return _server->send((Update.hasError()) ? 400 : 200, "text/plain", (Update.hasError()) ? _update_error_str.c_str() : "OK");
    });
  #endif

  #if ELEGANTOTA_USE_ASYNC_WEBSERVER == 1
    _server->on("/ota/upload", HTTP_POST, [&](AsyncWebServerRequest *request) {
        if(_authenticate && !request->authenticate(_username.c_str(), _password.c_str())){
          return request->requestAuthentication();
        }

        // TM-49: gate on the verified-image flag, not on `!Update.hasError()`.
        // An upload that died before its `final` frame leaves hasError() false
        // while nothing was ever verified -- that must not reboot into a
        // half-written app image.
        if (!_ota_image_valid) {
          abortActiveUpdate("incomplete_upload");
          if (_update_error_str.isEmpty()) {
            _update_error_str = "Upload incomplete: image never verified\n";
          }
        }

        // Post-OTA update callback
        if (postUpdateCallback != NULL) {
          Serial.println("Calling postUpdateCallback");
          postUpdateCallback(_ota_image_valid);
        }

        // Set reboot flag
        if (_ota_image_valid) {
          if (_auto_reboot) {
            _reboot_request_millis = millis();
            _reboot = true;
          }
        }

        delay(100);
        Serial.println("Sending response");
        request->send((!_ota_image_valid) ? 400 : 200, "text/plain", (!_ota_image_valid) ? _update_error_str.c_str() : "OK");

    }, [&](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        //Upload handler chunks in data
        if(_authenticate){
            if(!request->authenticate(_username.c_str(), _password.c_str())){
                return request->requestAuthentication();
            }
        }

        if (!index) {
          // Reset progress size on first frame
          _current_progress_size = 0;
          // TM-46: a client that vanishes mid-transfer (dropped TCP connection)
          // must not leave Update() running forever -- abort on disconnect.
          // Capture (by value) the generation this upload belongs to: AsyncTCP
          // can deliver a killed client's disconnect late, after a fresh
          // /ota/start has already superseded it: only abort if the captured
          // generation is still the active one, else it's a stale event for a
          // session that is already gone -- ignore it, don't kill the new one.
          uint32_t gen = _updateGeneration;
          request->onDisconnect([this, gen]() {
            if (gen == _updateGeneration) {
              abortActiveUpdate("client_disconnected");
            } else {
              Serial.printf("[SAFEBOOT];ota;disconnect_ignored;gen;%u/%u\n",
                             (unsigned)gen, (unsigned)_updateGeneration);
            }
          });
        }

        // Write chunked data to the free sketch space
        if(len){
            if (Update.write(data, len) != len) {
                abortActiveUpdate("write_failed");
                return request->send(400, "text/plain", "Failed to write chunked data to free space");
            }
            _current_progress_size += len;
            // Progress update callback
            if (progressUpdateCallback != NULL) progressUpdateCallback(_current_progress_size, request->contentLength());
        }
            
        if (final) { // if the final flag is set then this is the last frame of data
          Serial.println("Final frame received");
            if (!Update.end(true)) { //true to set the size to the current progress
                // Save error to string
                StreamString str;
                Update.printError(str);
                _update_error_str = str.c_str();
                _update_error_str.concat("\n");
                Serial.println(_update_error_str.c_str());
            } else {
                // TM-49: end(true) succeeded -- length and the client-supplied
                // MD5 (set in /ota/start) both check out. This is the only
                // place that may clear the fail-closed gate.
                _ota_image_valid = Update.isFinished();
                Serial.printf("[SAFEBOOT];ota;verify;result;%s\n",
                              _ota_image_valid ? "ok" : "unfinished");
            }
        }else{
            return;
        }
    });
  #else
    _server->on("/ota/upload", HTTP_POST, [&](){
      if (_authenticate && !_server->authenticate(_username.c_str(), _password.c_str())) {
        return _server->requestAuthentication();
      }
      // TM-49: same fail-closed gate as the async path above.
      if (!_ota_image_valid) {
        abortActiveUpdate("incomplete_upload");
        if (_update_error_str.isEmpty()) {
          _update_error_str = "Upload incomplete: image never verified\n";
        }
      }
      // Post-OTA update callback
      if (postUpdateCallback != NULL) postUpdateCallback(_ota_image_valid);
      _server->sendHeader("Connection", "close");
      _server->send((!_ota_image_valid) ? 400 : 200, "text/plain", (!_ota_image_valid) ? _update_error_str.c_str() : "OK");
      // Set reboot flag
      if (_ota_image_valid) {
        if (_auto_reboot) {
          _reboot_request_millis = millis();
          _reboot = true;
        }
      }
    }, [&](){
      // Actual OTA Download
      HTTPUpload& upload = _server->upload();
      if (upload.status == UPLOAD_FILE_START) {
        // Check authentication
        if (_authenticate && !_server->authenticate(_username.c_str(), _password.c_str())) {
          ELEGANTOTA_DEBUG_MSG("Authentication Failed on UPLOAD_FILE_START\n");
          return;
        }
        Serial.printf("Update Received: %s\n", upload.filename.c_str());
        _current_progress_size = 0;
      } else if (upload.status == UPLOAD_FILE_WRITE) {
          if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            #if UPDATE_DEBUG == 1
              Update.printError(Serial);
            #endif
          }

          _current_progress_size += upload.currentSize;
          // Progress update callback
          if (progressUpdateCallback != NULL) progressUpdateCallback(_current_progress_size, upload.totalSize);
      } else if (upload.status == UPLOAD_FILE_END) {
          if (Update.end(true)) {
              // TM-49: see the async path -- only a verified image clears the gate.
              _ota_image_valid = Update.isFinished();
              ELEGANTOTA_DEBUG_MSG(String("Update Success: "+String(upload.totalSize)+"\n").c_str());
          } else {
              ELEGANTOTA_DEBUG_MSG("[!] Update Failed\n");
              // Store error to string
              StreamString str;
              Update.printError(str);
              _update_error_str = str.c_str();
              _update_error_str.concat("\n");
              ELEGANTOTA_DEBUG_MSG(_update_error_str.c_str());
          }

          #if UPDATE_DEBUG == 1
            Serial.setDebugOutput(false);
          #endif
      } else {
        ELEGANTOTA_DEBUG_MSG(String("Update Failed Unexpectedly (likely broken connection): status="+String(upload.status)+"\n").c_str());
      }
    });
  #endif
}

void ElegantOTAClass::setAuth(const char * username, const char * password){
  _username = username;
  _password = password;
  _authenticate = _username.length() && _password.length();
}

void ElegantOTAClass::clearAuth(){
  _authenticate = false;
}

void ElegantOTAClass::setAutoReboot(bool enable){
  _auto_reboot = enable;
}

void ElegantOTAClass::loop() {
  // Check if 2 seconds have passed since _reboot_request_millis was set
  if (_reboot && millis() - _reboot_request_millis > 2500) {
    _reboot = false;
    Serial.println("Rebooting...\n");
    #if defined(ESP8266) || defined(ESP32)
      ESP.restart();
    #elif defined(TARGET_RP2040)
      rp2040.reboot();
    #endif
  }
}

void ElegantOTAClass::onStart(std::function<void()> callable){
    preUpdateCallback = callable;
}

void ElegantOTAClass::onProgress(std::function<void(size_t current, size_t final)> callable){
    progressUpdateCallback= callable;
}

void ElegantOTAClass::onEnd(std::function<void(bool success)> callable){
    postUpdateCallback = callable;
}

void ElegantOTAClass::onAbort(std::function<void(const char* reason)> callable){
    abortUpdateCallback = callable;
}

// TM-46: centralizes every "give up on the current Update session" path
// (stale session on a new /ota/start, a write failure, a dropped connection,
// a stalled upload) so each one gets the same cleanup and the same log line.
void ElegantOTAClass::abortActiveUpdate(const char* reason){
    if (Update.isRunning()) {
        // TM-49: an aborted, still-running session can never be a verified
        // image. Deliberately NOT cleared when the Update object has already
        // finished: a disconnect event can land after a successful
        // Update.end(true), and that one must not retract a valid verdict.
        _ota_image_valid = false;
        Update.abort();
        Serial.printf("[SAFEBOOT];ota;abort;reason;%s\n", reason);
        if (abortUpdateCallback != NULL) abortUpdateCallback(reason);
    }
}


ElegantOTAClass ElegantOTA;
