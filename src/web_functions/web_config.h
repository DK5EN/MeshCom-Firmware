#pragma once

// FORMAL-02 9g: config export/import glue of the web server (web_config.cpp)
#define WEB_TIMEOUT_TIME 2000             // Define timeout time in milliseconds (example: 2000ms = 2s)

void sub_config_download(void);           // GET /config.json
void sub_config_upload(long content_length); // POST /config: restore and reboot once
