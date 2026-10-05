/*
This file contains all web-based remotely callable functions
*/
#include "web_nodefunctioncalls.h"
#include <command_functions.h>
#include <loop_functions.h>
#include <loop_functions_extern.h>
#include <string> 
#ifdef ESP32
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
    // AU-09 (#1187): firmware update actions of the info page banner, routed through "--update <verb>".
    // Return OKAY when the job started (or the image is already staged), FAIL otherwise. "updapply"
    // reboots into Safeboot on success, so its answer is never read; with nothing staged it fails.
    if(functionData->functionName.compareTo("updcheck")==0) {
        commandAction((char*)"--update check", bPhoneReady);
        FwNetStatus au;
        fwNetGetStatus(au);
        functionData->returnCode = (au.state == FWS_BUSY) ? WF_RETURNCODE_OKAY : WF_RETURNCODE_FAIL;
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
        FwNetStatus au;
        fwNetGetStatus(au);
        if(!au.staged || au.state == FWS_BUSY) {
            functionData->returnCode = WF_RETURNCODE_FAIL;
            return;
        }
        commandAction((char*)"--update apply", bPhoneReady);
        functionData->returnCode = WF_RETURNCODE_OKAY;
        return;
    }
    #endif


    //if nothiung matched, then the function is not known.
    functionData->returnCode = WF_RETURNCODE_FAIL;
    return;
}