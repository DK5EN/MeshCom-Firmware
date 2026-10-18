/*
This file contains all web-based remotely callable functions
*/
#include "web_nodefunctioncalls.h"
#include <command_functions.h>
#include <loop_functions.h>
#include <loop_functions_extern.h>
#include <string> 
#ifdef ESP32
#include <esp_partition.h> // updapply: Safeboot partition present
#include "esp32/fw_update_net.h" // AU-09 (#1187): FwNetStatus for the update callfunctions
#endif



void webFunctionCall(funCallStruct* functionData) {
    bool bPhoneReady = (isPhoneReady == 1);

    //Serial.printf("##### Executing: %s with param: %s\n", functionData->functionName, functionData->functionParameter);
    if(functionData->functionName.compareTo("sendpos")==0) {
        functionData->returnCode = WF_RETURNCODE_OKAY;
        if(bDisplayTrack) {
            commandAction((char*)"--sendtrack", bPhoneReady);
            //Serial.println("SendTrack");
        } else {
            commandAction((char*)"--sendpos", bPhoneReady);
            //Serial.println("SendPos");
        }
        functionData->returnCode = WF_RETURNCODE_OKAY;
        return;
    } else
    if(functionData->functionName.compareTo("reboot")==0) {
                commandAction((char*)"--reboot", bPhoneReady);
    }
    #ifdef ESP32
    if(functionData->functionName.compareTo("otaupdate")==0) {
                commandAction((char*)"--ota-update", bPhoneReady);
    }
    #endif
    #ifdef ESP32
    // AU-09 (#1187) / AU web GUI: firmware update actions of the Info page row, routed through "--update <verb>".
    // The caller (web_functions.cpp call_function) has already required the request header X-MC: 1 for every upd*.
    // OKAY when the job started (or the image is already staged), FAIL otherwise.
    if(functionData->functionName.compareTo("updcheck")==0) {
        // the BLE guard ("Pause Bluetooth and check") is campaign section 7 / W4: answer FAIL until it exists
        if(functionData->functionParameter.equals("ble")) {
            // TODO(W4): accept only after lowheap_phone with a phone connected, set the BLE guard, 5 min cooldown
            Serial.printf("[AU];web;ble;not yet\n");
            functionData->returnCode = WF_RETURNCODE_FAIL;
            return;
        }
        // GitHub allows 60 unauthenticated API calls per hour per public IP: 60 s between manual checks
        static uint32_t s_lastCheckMs = 0;
        static bool s_checked = false;
        const uint32_t nowMs = millis();
        if(s_checked && (uint32_t)(nowMs - s_lastCheckMs) < 60000u) {
            functionData->returnCode = WF_RETURNCODE_FAIL;
            return;
        }
        commandAction((char*)"--update check", bPhoneReady);
        FwNetStatus au;
        fwNetGetStatus(au);
        const bool started = (au.state == FWS_BUSY);
        if(started) {
            s_lastCheckMs = nowMs;
            s_checked = true;
        }
        functionData->returnCode = started ? WF_RETURNCODE_OKAY : WF_RETURNCODE_FAIL;
        return;
    } else
    if(functionData->functionName.compareTo("updinstall")==0) {
        commandAction((char*)"--update install", bPhoneReady);
        FwNetStatus au;
        fwNetGetStatus(au);
        const bool already = au.staged && au.availTag[0] != '\0' && strcmp(au.stagedTag, au.availTag) == 0;
        functionData->returnCode = (au.state == FWS_BUSY || already) ? WF_RETURNCODE_OKAY : WF_RETURNCODE_FAIL;
        return;
    } else
    if(functionData->functionName.compareTo("updapply")==0) {
        // validate first, answer the real result, reboot after the answer left (auRequestApplyReboot, 500 ms)
        FwNetStatus au;
        fwNetGetStatus(au);
        FwStageRecord rec;
        if(au.state == FWS_BUSY || !fwNetLoadRecord(rec) || !fwNetSafebootCapable() ||
           esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, "safeboot") == nullptr) {
            functionData->returnCode = WF_RETURNCODE_FAIL;
            return;
        }
        if(!fwNetMarkHandover(rec.tag)) {   // before the reboot, as in --update apply
            functionData->returnCode = WF_RETURNCODE_FAIL;
            return;
        }
        Serial.printf("[AU];handover;%s\n", rec.tag);
        auRequestApplyReboot();
        functionData->returnCode = WF_RETURNCODE_OKAY;
        return;
    }
    #endif


    //if nothiung matched, then the function is not known.
    functionData->returnCode = WF_RETURNCODE_FAIL;
    return;
}