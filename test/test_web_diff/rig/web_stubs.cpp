// Recording stubs of the functions the web request path calls through external linkage in the real build
// (FORMAL-02 web diff). One definition for every entry point under test, so the frozen copy and the live
// code leave the same trace for the same call. The page and body handlers that depend on the unit's own
// body readers are in stubs/web_handler_stubs.h instead.
#include "web_rig.h"

#include <string>

#include <web_nodefunctioncalls.h>
#include <web_setup.h>
#include <backpressure.h>
#include <web_txbuf.h>

// webFunctionCall: record name/parameter, answer by a fixed rule (the known actions succeed)
void webFunctionCall(funCallStruct *d)
{
    webRigTrace("webFunctionCall name='%s' param='%s'", webRigEsc(d->functionName.c_str()).c_str(),
                webRigEsc(d->functionParameter.c_str()).c_str());
    static const char *const known[] = {"sendpos", "reboot", "otaupdate", "updcheck", "updinstall", "updapply"};
    d->returnCode = WF_RETURNCODE_FAIL;
    for (const char *k : known)
        if (d->functionName.equals(k))
            d->returnCode = WF_RETURNCODE_OKAY;
}

// webSetup_setParam / getParam: record, read-back = what was set (so JSON escaping of hostile values is exercised)
void webSetup_setParam(setupStruct *d)
{
    webRigTrace("webSetup_setParam name='%s' value='%s'", webRigEsc(d->paramName.c_str()).c_str(),
                webRigEsc(d->paramValue.c_str()).c_str());
    d->returnCode = d->paramName.length() == 0 ? WS_RETURNCODE_UNKNOWN : WS_RETURNCODE_OKAY;
    d->returnValue = d->paramValue;
}

void webSetup_getParam(setupStruct *d)
{
    webRigTrace("webSetup_getParam name='%s'", webRigEsc(d->paramName.c_str()).c_str());
    d->returnCode = d->paramName.length() == 0 ? WS_RETURNCODE_UNKNOWN : WS_RETURNCODE_OKAY;
    d->returnValue = String("v:") + d->paramName;
}

bool hasMsgFromPhone = false;
void setMsgOrigin(MsgOrigin o) { webRigTrace("setMsgOrigin %d", (int)o); }

// sendMessage: the BP outcome depends on the text, so all four branches of send_message() are reachable
int sendMessage(char *text, int len)
{
    webRigTrace("sendMessage len=%d text='%s'", len, webRigEsc(text).c_str());
    const std::string t(text);
    if (t.find("pad") != std::string::npos)
        return BP_SEND_REFUSED;
    if (t.find('z') != std::string::npos)
        return BP_SEND_INVALID;
    if (len >= 100)
        return BP_SEND_DROPPED;
    return BP_SEND_OK;
}

void msgstorePurgeAll() { webRigTrace("msgstorePurgeAll"); }
bool msgstorePurge(long slot)
{
    webRigTrace("msgstorePurge slot=%ld", slot);
    return slot != 0;
}
bool msgstoreDeliverNow(long slot)
{
    webRigTrace("msgstoreDeliverNow slot=%ld", slot);
    return slot != 0;
}

bool fwNetSessionActive() { return webRigAuSession(); }
void webTxBeginChunked(CommonWebClient &) { webRigTrace("webTxBeginChunked"); }
String getTimeString() { return String("00:00:00"); }
