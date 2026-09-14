// msgstore.cpp -- STUB written by the orchestrator so the page and command
// writers link during the stage 3 wave. Owner A replaces every body below
// with the real core (docs/dm-stage3-wave-plan-20260914.md).
#include "msgstore_api.h"
#include <string.h>
#include <stdio.h>

static const struct MsgStoreEnv *s_env = NULL;
static enum MsgStoreMode s_mode = MSGSTORE_OFF;
static uint8_t  s_slots = MSGSTORE_SLOTS_DEFAULT;
static uint16_t s_hold_h = MSGSTORE_HOLD_DEFAULT_H;
static char     s_list[MSGSTORE_LIST_MAX * MSGSTORE_CALL_MAX] = {0};
static struct MsgStoreCounters s_cnt = {0, 0, 0, 0, 0, 0, 0, 0, 0};

void msgstoreInit(const struct MsgStoreEnv *env) { s_env = env; }
void msgstoreConfigure(enum MsgStoreMode mode, uint8_t slots, uint16_t hold_hours)
{
    s_mode = mode;
    s_slots = (slots < 1) ? 1 : (slots > MSGSTORE_SLOTS_MAX ? MSGSTORE_SLOTS_MAX : slots);
    s_hold_h = (hold_hours < 1) ? 1 : (hold_hours > MSGSTORE_HOLD_MAX_H ? MSGSTORE_HOLD_MAX_H : hold_hours);
}
void msgstoreSetList(const char *csv) { if(csv) { strncpy(s_list, csv, sizeof(s_list) - 1); s_list[sizeof(s_list) - 1] = 0; } else s_list[0] = 0; }
enum MsgStoreMode msgstoreMode(void) { return s_mode; }
uint8_t  msgstoreSlots(void) { return s_slots; }
uint16_t msgstoreHoldHours(void) { return s_hold_h; }
const char *msgstoreListCsv(void) { return s_list; }
const char *msgstoreModeName(enum MsgStoreMode mode)
{
    switch(mode) { case MSGSTORE_OWN: return "own"; case MSGSTORE_LIST: return "list"; case MSGSTORE_HEARD: return "heard"; default: return "off"; }
}
const char *msgstoreStateName(uint8_t state)
{
    switch(state) { case MSGSTORE_HELD: return "HELD"; case MSGSTORE_ARMED: return "ARMED"; case MSGSTORE_LADDER: return "LADDER"; case MSGSTORE_COOLDOWN: return "COOLDOWN"; default: return "FREE"; }
}
bool msgstoreEligible(const char *dst) { (void)dst; return false; }
int  msgstoreStore(const char *src, const char *dst, uint16_t nnn, const char *payload, size_t len) { (void)src; (void)dst; (void)nnn; (void)payload; (void)len; return -1; }
void msgstoreOnAck(const char *acker, const char *sender, uint16_t nnn) { (void)acker; (void)sender; (void)nnn; }
void msgstorePresence(const char *call) { (void)call; }
void msgstoreOnPeerDelivery(const char *src, uint16_t nnn) { (void)src; (void)nnn; }
void msgstoreLoop(void) {}
bool msgstorePurge(int slot) { (void)slot; return false; }
void msgstorePurgeAll(void) {}
bool msgstoreDeliverNow(int slot) { (void)slot; return false; }
int  msgstoreUsed(void) { return 0; }
const struct MsgStoreEntry *msgstoreEntry(int slot) { (void)slot; return NULL; }
const struct MsgStoreCounters *msgstoreCounters(void) { return &s_cnt; }
uint8_t  msgstoreActionsLastHour(void) { return 0; }
uint32_t msgstoreNextActionInMs(void) { return 0; }
int msgstoreFormatLine(char *buf, size_t n)
{
    if(buf == NULL || n == 0) return 0;
    int len = snprintf(buf, n, "MBOX mode=%s used=%d/%u", msgstoreModeName(s_mode), msgstoreUsed(), (unsigned)s_slots);
    return (len < 0) ? 0 : ((size_t)len >= n ? (int)(n - 1) : len);
}
