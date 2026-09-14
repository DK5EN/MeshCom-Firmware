// dm_outbox.cpp -- STUB by the orchestrator so siblings link; owner A replaces
// every body (docs/dm-stage1-plan-20260914.md).
#include "dm_outbox_api.h"
#include <string.h>
#include <stdio.h>

static struct DmOutboxCounters s_cnt = {0, 0, 0, 0, 0, 0, 0};

void dmOutboxInit(const struct DmOutboxEnv *env, uint8_t slots) { (void)env; (void)slots; }
int  dmOutboxAdd(uint16_t nnn, const char *dst, const char *payload, size_t len, uint8_t max_hop, uint32_t first_id, enum DmRetryMode mode)
{ (void)nnn; (void)dst; (void)payload; (void)len; (void)max_hop; (void)first_id; (void)mode; return -1; }
bool dmOutboxHasRoom(void) { return false; }
void dmOutboxOnEcho(uint32_t msg_id) { (void)msg_id; }
bool dmOutboxOnAck(const char *from, uint16_t nnn) { (void)from; (void)nnn; return false; }
void dmOutboxOnHeld(uint16_t nnn) { (void)nnn; }
void dmOutboxLoop(void) {}
int  dmOutboxUsed(void) { return 0; }
const struct DmOutboxEntry *dmOutboxEntry(int slot) { (void)slot; return NULL; }
const struct DmOutboxCounters *dmOutboxCounters(void) { return &s_cnt; }
uint32_t dmOutboxFirstIdForNnn(uint16_t nnn) { (void)nnn; return 0; }
int  dmOutboxFormatLine(char *buf, size_t n) { if(!buf || !n) return 0; int l = snprintf(buf, n, "OUTBOX used=0"); return l < 0 ? 0 : ((size_t)l >= n ? (int)(n - 1) : l); }
void dmOutboxReset(void) {}
