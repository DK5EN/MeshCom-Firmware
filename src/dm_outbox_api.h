// dm_outbox_api.h -- stage 1 outbox and retry ladder, core contract.
//
// docs/dm-stage1-plan-20260914.md sections 3-4. Platform-neutral core
// (dm_outbox.cpp) driven through DmOutboxEnv, installed by dm_outbox_glue.cpp
// on the firmware and by fakes in test/test_dm_outbox. Used only when
// dmRetryMode() != DM_RETRY_OFF; in off mode nothing here is called.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "dm_settings.h"

#define DM_OUTBOX_SLOTS_MAX   5     // S3 / nRF52840; classic ESP32 configures 3 at init
#define DM_OUTBOX_PAYLOAD_MAX 160
#define DM_OUTBOX_CALL_MAX    10
#define DM_OUTBOX_STEP_MS     40000UL    // attempts inside a block
#define DM_OUTBOX_BLOCK_GAP_MS 60000UL   // between blocks (on top of the step)
#define DM_OUTBOX_ECHO_GATE_MS 15000UL   // attempt 1 must be heard relayed within this to earn fresh ids

enum DmOutboxState { DMOB_FREE = 0, DMOB_LADDER = 1, DMOB_DONE_ACK = 2, DMOB_DONE_GIVEUP = 3 };

struct DmOutboxEntry
{
    uint16_t nnn;
    char     dst[DM_OUTBOX_CALL_MAX];
    char     payload[DM_OUTBOX_PAYLOAD_MAX + 1];   // stripped text, "{NNN" re-attached on transmit
    uint16_t plen;
    uint8_t  max_hop;                              // as attempt 1 carried it
    uint32_t first_id;                             // attempt 1's msg_id: the app's key for this message
    uint32_t last_id;                              // the current attempt's msg_id
    uint32_t first_ms;
    uint32_t next_ms;
    uint8_t  attempt;                              // attempts transmitted so far (1 after sendMessage)
    uint8_t  max_attempts;                         // 3 or 9
    uint8_t  state;                                // DmOutboxState
    bool     echo_seen;                            // attempt 1 heard relayed
    bool     held;                                 // a store node said :sto (stage 4) -- informational
    uint8_t  gen;
};

struct DmOutboxCounters
{
    uint32_t added, refused_full, same_id_retries, fresh_attempts, acked, gaveup, blocked_bp;
};

struct DmOutboxEnv
{
    uint32_t (*now_ms)(void);
    int      (*bp_state)(void);                                       // 0 QUIET, 1 QRS, 2 QRT
    uint32_t (*mint_id)(void);                                        // fresh msg_id (millis)
    // Enqueue one attempt into the TX ring as an ordinary slot with retransmission disabled.
    // msg_id is either e->first_id (same-id retry) or a fresh one. Returns false if the ring refused.
    bool     (*transmit)(const struct DmOutboxEntry *e, uint32_t msg_id);
    // Phone status frame for e->first_id: 0x02 acked, 0x03 failed (caller decides the held case).
    void     (*report)(const struct DmOutboxEntry *e, uint8_t status);
    void     (*log)(const char *line);                                // may be NULL
};

void dmOutboxInit(const struct DmOutboxEnv *env, uint8_t slots);

// sendMessage() after attempt 1 is enqueued: returns the slot, or -1 when full (the caller refuses the DM).
int  dmOutboxAdd(uint16_t nnn, const char *dst, const char *payload, size_t len,
                 uint8_t max_hop, uint32_t first_id, enum DmRetryMode mode);
bool dmOutboxHasRoom(void);                                          // checked BEFORE sending attempt 1

// Receive-path events
void dmOutboxOnEcho(uint32_t msg_id);                                // own frame heard relayed
bool dmOutboxOnAck(const char *from, uint16_t nnn);                  // :ackNNN from the destination; true if an entry stopped
void dmOutboxOnHeld(uint16_t nnn);                                   // :sto seen (informational, ladder continues)

// Loop task
void dmOutboxLoop(void);

// Readers
int                            dmOutboxUsed(void);
const struct DmOutboxEntry    *dmOutboxEntry(int slot);
const struct DmOutboxCounters *dmOutboxCounters(void);
uint32_t                       dmOutboxFirstIdForNnn(uint16_t nnn);  // 0 when unknown
int                            dmOutboxFormatLine(char *buf, size_t n); // "OUTBOX ..." setlog line
void                           dmOutboxReset(void);                  // tests
