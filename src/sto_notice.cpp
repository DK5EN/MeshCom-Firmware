// sto_notice.cpp -- STUB by the orchestrator so sibling writers link; owner A
// replaces every body (docs/dm-stage4-plan-20260914.md).
#include "sto_notice.h"
#include <string.h>
#include <stdio.h>

int  stoNoticeBuild(char *buf, size_t n, const char *sender, uint16_t nnn, const char *dst)
{ (void)sender; (void)nnn; (void)dst; if(buf && n) buf[0]=0; return 0; }
bool stoNoticeParse(const char *payload, uint16_t *nnn, char *dst) { (void)payload; (void)nnn; (void)dst; return false; }
bool stoHolderNote(uint32_t msg_id, const char *holder, uint16_t nnn, uint32_t now_ms) { (void)msg_id; (void)holder; (void)nnn; (void)now_ms; return false; }
const char *stoHolder(uint32_t msg_id) { (void)msg_id; return ""; }
void stoHolderClear(uint32_t msg_id) { (void)msg_id; }
void stoHolderReset(void) {}
