// SPDX-License-Identifier: GPL-3.0-or-later
/*
* Copyright (C) 2023-2024 Mathieu Carbou
*/

#include "ElegantOTA.h"
#include <WiFi.h>
#include <esp_wifi.h>

#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>

#include <esp_ota_ops.h>
#include <esp_partition.h>

#include <Preferences.h>
#include "../configuration_global.h"
#include "../esp32/esp32_flash.h"

#define TAG "SafeBoot"

const unsigned int port = 80;
AsyncWebServer webServer(port);
String hostname = "MeshCom-OTA";

extern Preferences preferences;
extern s_meshcom_settings meshcom_settings;

// TM-46: volatile -- written from the async_tcp task (OTA callbacks), read
// from the Arduino loop task.
volatile bool updateInProgress = false;  // Flag to indicate if an update is in progress

// TM-46: explicit fallback-to-app rearm timestamp -- millis() at the last time
// the window was (re)armed: at boot, on every OTA abort, and on OTA end. The
// fallback fires when no update has been in progress for wait_ota_timeout ms
// *since that arm*, not "the device has been up a while".
volatile unsigned long fallback_armed_at = 0;
int wait_ota_timeout = 180 * 1000; // OTA Timeout in ms
boolean reboot_after_cancel = false; // Reboot after cancelling OTA if no update was started

// TM-46: last time upload data actually arrived. Distinguishes an active
// (still receiving chunks) upload from a stalled one -- only the latter gets
// force-aborted by the fallback-to-app watchdog in loop().
volatile unsigned long last_ota_data_millis = 0;
const unsigned long OTA_STALL_TIMEOUT_MS = 30000; // TM-46: no data for this long => abort
 
void startMDNS();
 
 
void wifiConnect() {

  // read wlan credentials from flash
  init_flash();

  const char *ssid = meshcom_settings.node_ssid;
  const char *pass = meshcom_settings.node_pwd;
  bool bWEBSERVER = meshcom_settings.node_sset2 & 0x0040;
  bool bGATEWAY = meshcom_settings.node_sset & 0x1000;
  bool bWIFIAP = meshcom_settings.node_sset2 & 0x0080;

  Serial.printf("\nNVS Flash Settings:\n");
  Serial.printf("Callsign: %s\n", meshcom_settings.node_call);
  Serial.printf("Wifi SSID: %s\n", ssid);
  Serial.printf("Webserver: %d\n", bWEBSERVER);
  Serial.printf("Gateway: %d\n", bGATEWAY);
  Serial.printf("WIFI AP: %d\n", bWIFIAP);

  

  // Set the hostname from the callsign. If the callsign is not set, use the default hostname
  if (!isNodeUnconfigured(meshcom_settings.node_call))
  {
    hostname = meshcom_settings.node_call;
  }

  // When there is no SSID or WIFI-AP is enabled, start AP
  if (strcmp(ssid, "none") == 0 || bWIFIAP)
  {
    Serial.println("\nStarting Wifi AP");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(hostname);
    delay(300);
    Serial.printf("AP IP: %s\n", WiFi.softAPIP().toString().c_str());
    // start mDNS responder
    startMDNS();
    return;
  }

   // TM-48: driver-managed join, same as production (udp_functions.cpp wifiInitOnce/wifiBegin)
   WiFi.persistent(false);
   WiFi.setAutoReconnect(true);
   WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
   WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);

   WiFi.mode(WIFI_STA);
   WiFi.disconnect(true);

   // Static IP settings
  IPAddress node_ip = IPAddress(0,0,0,0);
  IPAddress node_gw = IPAddress(0,0,0,0);
  IPAddress node_ms = IPAddress(0,0,0,0);
  IPAddress node_dns = IPAddress(0,0,0,0);

  if (strlen(meshcom_settings.node_ownip) >= 7 && strlen(meshcom_settings.node_owngw) >= 7 && strlen(meshcom_settings.node_ownms) >= 7 && bWIFIAP == false)
  {
    Serial.printf("Static IP settings:\n");
    Serial.printf("IP: %s\n", meshcom_settings.node_ownip);
    Serial.printf("GW: %s\n", meshcom_settings.node_owngw);
    Serial.printf("MS: %s\n", meshcom_settings.node_ownms);
    Serial.printf("DNS: %s\n", meshcom_settings.node_owndns);

    // Set your Static IP address
    node_ip.fromString(meshcom_settings.node_ownip);
    // Set your Gateway IP address
    node_gw.fromString(meshcom_settings.node_owngw);
    // Set your Gateway IP mask
    node_ms.fromString(meshcom_settings.node_ownms);
    // Set your DNS IP
    if (strlen(meshcom_settings.node_owndns) >= 7)
      node_dns.fromString(meshcom_settings.node_owndns);
    else
      node_dns.fromString("8.8.8.8");

    // Configures static IP address
    if (!WiFi.config(node_ip, node_gw, node_ms, node_dns))
    {
      Serial.println("[Error] STA Failed to configure static IP!");
    }
  }

   delay(500);

   // TM-48: config-only begin, let the driver pick the AP -- no own scan,
   // no BSSID/channel pin (see udp_functions.cpp wifiBegin() for the
   // production pattern this mirrors).
   // "empty" (production convention) and "none" (safeboot/flash-default
   // convention) both mean "open network".
   const char *wifi_pwd = pass;
   if (strcmp(wifi_pwd, "empty") == 0 || strcmp(wifi_pwd, "none") == 0)
     wifi_pwd = NULL;

   Serial.printf("-> try connecting to SSID: %s \n", ssid);
   WiFi.begin(ssid, wifi_pwd, 0, NULL, false); // configuration only, no connect yet

   {
     esp_err_t rc = esp_wifi_disable_pmf_config(WIFI_IF_STA);
     Serial.printf("[SAFEBOOT];wifi;pmf_off;rc;%d\n", (int)rc);
   }

   esp_wifi_connect();
   delay(500);

   Serial.println("Connecting to WiFi");

   // TM-48: this is a one-shot bootloader join, so it stays blocking, but with
   // more patience than a plain link-layer retry needs -- a WPA2/WPA3
   // transition-mode negotiation can take longer than a few seconds, and the
   // AP-mode fallback below makes the node unreachable from the LAN, so it
   // must not trigger on a merely slow join.
   int iWlanWait = 0;
   bool wifiRetried = false;

   while(WiFi.status() != WL_CONNECTED)
   {
     delay(1000);
     iWlanWait++;
     Serial.print(".");

     if(!wifiRetried && iWlanWait >= 12)
     {
       wifiRetried = true;
       Serial.println();
       Serial.println("[SAFEBOOT];wifi;retry;reason;no_connect_12s");
       esp_wifi_connect();
     }

     if(iWlanWait > 25)
     {
       // Start AP -- last resort only, after the extended join patience above
       Serial.println("\nStarting AP");
       Serial.println("[SAFEBOOT];wifi;fallback_ap;reason;join_timeout_25s");
       WiFi.mode(WIFI_AP);
       WiFi.softAP(hostname);
       delay(300);
       Serial.printf("AP IP: %s\n", WiFi.softAPIP().toString().c_str());
       return;
     }
   }

   Serial.println("\nConnected to WiFi");
   Serial.print("IP Address: ");
   Serial.println(WiFi.localIP());
   // start mDNS responder
   startMDNS();
   return;
 }
 
 
 
 // start mdns responder and set hostname and tcp service
 void startMDNS()
 {
   if (!MDNS.begin(hostname.c_str()))
   {
     Serial.println("Error setting up MDNS responder!");
   }
   Serial.println("mDNS responder started");
   if(MDNS.addService("http", "tcp", port))
   {
     Serial.println("mDNS http service added");
   }
   else
   {
     Serial.println("Error setting up mDNS service!");
   }
 }
 
 
 // set partition to ota_0 and reboot
 void setBootPartition_APP()
 {
   const esp_partition_t *partition = esp_partition_find_first(esp_partition_type_t::ESP_PARTITION_TYPE_APP, esp_partition_subtype_t::ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
   if (partition)
   {
     esp_ota_set_boot_partition(partition);
   } 
   else
   {
     Serial.println("Error setting boot partition!");
   }
 }
 
 
 // ElegantOTA Callbacks
 unsigned long ota_progress_millis = 0;
 
 void onOTAStart() {
   // Log when OTA has started
   updateInProgress = true;
   last_ota_data_millis = millis(); // TM-46: anchor the stall watchdog
   Serial.println("OTA update started!");
   Serial.println("[SAFEBOOT];ota;start");
 }

 void onOTAProgress(size_t current, size_t final) {
   // TM-46: mark that data is still arriving, independent of the print throttle below
   last_ota_data_millis = millis();
   // Log every 1 second
   if (millis() - ota_progress_millis > 1000) {
     ota_progress_millis = millis();
     Serial.printf("OTA Progress Current: %u bytes, Final: %u bytes\n", current, final);
   }
 }

 // TM-46: fired by ElegantOTA whenever it aborts the active Update session
 // (stale session, write failure, dropped connection, stalled upload).
 // Clears updateInProgress and re-arms the fallback-to-app timeout so a dead
 // client can never strand the node in safeboot forever.
 void onOTAAbort(const char *reason)
 {
   updateInProgress = false;
   fallback_armed_at = millis();
   Serial.printf("[SAFEBOOT];ota;rearm;reason;%s\n", reason);
 }

 void onOTAEnd(bool success)
 {
   // TM-46: the session is over either way -- clear the in-progress flag and
   // re-arm the fallback timeout, matching the abort cleanup above.
   updateInProgress = false;
   fallback_armed_at = millis();

   // Log when OTA has finished
   if (success)
   {
     Serial.println("OTA update finished successfully!");
     Serial.println("[SAFEBOOT];ota;end;result;success");
     // Set next boot partition
     setBootPartition_APP();
   }
   else
   {
     Serial.println("There was an error during OTA update!");
     Serial.println("[SAFEBOOT];ota;end;result;error");
   }
 }
 
 
 
 void setup() {
 
   Serial.begin(115200);
   // whait for serial
   delay(1000);
   Serial.println("\n-----------------------------");
   Serial.println("OTA UDATE started");
 
   // Connect to saved ssid or as fallback spawn an AP
   wifiConnect();
 
   // Start ElegantOTA
   ElegantOTA.clearAuth();
   ElegantOTA.setAutoReboot(true);
   ElegantOTA.begin(&webServer);
   // ElegantOTA callbacks
   ElegantOTA.onStart(onOTAStart);
   ElegantOTA.onProgress(onOTAProgress);
   ElegantOTA.onEnd(onOTAEnd);
   ElegantOTA.onAbort(onOTAAbort);
 
   // Start web server
   webServer.rewrite("/", "/update");
   webServer.onNotFound([](AsyncWebServerRequest* request) {
     request->redirect("/");
   });
 
   //endpoint for canceling the update. Only works if the update has not started yet
   webServer.on("/ota/cancel", HTTP_GET, [](AsyncWebServerRequest *request) {
     if(updateInProgress)
     {
       request->send(400, "text/plain", "OTA update in progress. Cannot cancel.");
     }
     else
     {
       request->send(200, "text/plain", "OTA update canceled.");
       reboot_after_cancel = true;
     }
   });
 
   webServer.begin();

   fallback_armed_at = millis();

 }

 void loop() {
   ElegantOTA.loop();

   // TM-46: an upload in progress but stalled (no data for OTA_STALL_TIMEOUT_MS)
   // must be aborted -- otherwise updateInProgress stays true forever and the
   // fallback-to-app timeout below can never fire again. An ACTIVE upload
   // keeps refreshing last_ota_data_millis via onOTAProgress and is never hit.
   // TM-46: SIGNED delta -- loop() races the async_tcp task: it can read
   // millis() a moment BEFORE the upload callback stores a fresh (larger)
   // last_ota_data_millis, and the unsigned difference then wraps to ~2^32,
   // aborting a healthy upload seconds after it started (measured on the
   // bench: abort;stalled in the same millisecond as the first progress
   // callback). A negative delta stays negative in signed arithmetic.
   if (updateInProgress && (long)(millis() - last_ota_data_millis) > (long)OTA_STALL_TIMEOUT_MS)
   {
     ElegantOTA.abortActiveUpdate("stalled");
   }

   // Check if OTA was started. If not, reboot to app/ota partition.
   // TM-46: this is elapsed time SINCE fallback_armed_at was last (re)armed,
   // not raw uptime -- an abort or a finished OTA re-arms it, so a fresh
   // wait_ota_timeout window always follows before this can fire. Signed
   // delta for the same cross-task reason as the stall check above.
   if((!updateInProgress && (long)(millis() - fallback_armed_at) > (long)wait_ota_timeout) || reboot_after_cancel)
   {
     Serial.println("OTA Start Timeout. Rebooting to app partition.");
     setBootPartition_APP();
     delay(1000);
     ESP.restart();
   }
 }