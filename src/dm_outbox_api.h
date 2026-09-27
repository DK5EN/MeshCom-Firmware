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
#define DM_OUTBOX_STEP_MS     40000UL    // spacing between attempts (the 40 s step, continued)

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
    uint8_t  max_attempts;                         // always 4 (mode 3; mode 9 retired)
    uint8_t  state;                                // DmOutboxState
    bool     echo_seen;                            // attempt 1 heard relayed (informational only)
    bool     held;                                 // a store node said :sto (stage 4) -- informational
    uint8_t  gen;
};

struct DmOutboxCounters
{
    // same_id_retries: kept for the OUTBOX setlog line's format; every
    // retry attempt now carries its own XOR id (src/pn_retry.h), never
    // first_id again, so this always reads 0. fresh_attempts counts
    // attempts 2..max_attempts (the XOR-variant transmissions).
    uint32_t added, refused_full, same_id_retries, fresh_attempts, acked, gaveup, blocked_bp;
};

struct DmOutboxEnv
{
    uint32_t (*now_ms)(void);
    int      (*bp_state)(void);                                       // 0 QUIET, 1 QRS, 2 QRT
    // Enqueue one attempt into the TX ring as an ordinary slot with retransmission disabled.
    // msg_id is e->first_id for attempt 1 or its XOR retry variant (src/pn_retry.h) for
    // attempts 2..max_attempts. Returns false if the ring refused.
    bool     (*transmit)(const struct DmOutboxEntry *e, uint32_t msg_id);
    // Phone status frame for e->first_id: 0x02 acked, 0x03 failed (caller decides the held case).
    void     (*report)(const struct DmOutboxEntry *e, uint8_t status);
    void     (*log)(const char *line);                                // may be NULL
};

void dmOutboxInit(const struct DmOutboxEnv *env, uint8_t slots);

// Firmware-side wiring (src/dm_outbox_glue.cpp): installs the real
// DmOutboxEnv and calls dmOutboxInit() with the board's slot count (5 on
// ESP32-S3/nRF52840, 3 on classic ESP32). Every board -- unlike msgstore's
// store-node role, the outbox is the sender side and compiles everywhere.
// Not called from anywhere in this wave's files; the boot-time call site is
// the orchestrator's (see docs/dm-stage1-plan-20260914.md section 6).
void dmOutboxGlueInit(void);

// sendMessage() after attempt 1 is enqueued: returns the slot, or -1 when full (the caller refuses the DM).
int  dmOutboxAdd(uint16_t nnn, const char *dst, const char *payload, size_t len,
                 uint8_t max_hop, uint32_t first_id, enum DmRetryMode mode);
bool dmOutboxHasRoom(void);                                          // checked BEFORE sending attempt 1

// Receive-path events
void dmOutboxOnEcho(uint32_t msg_id);                                // own frame heard relayed (matched via pnRetryCore, informational only)
bool dmOutboxOnAck(const char *from, uint16_t nnn);                  // :ackNNN from the destination; true if an entry stopped
void dmOutboxOnHeld(uint16_t nnn);                                   // :sto seen (informational, ladder continues)

// Loop task
void dmOutboxLoop(void);

// Readers
int                            dmOutboxUsed(void);
const struct DmOutboxEntry    *dmOutboxEntry(int slot);
const struct DmOutboxCounters *dmOutboxCounters(void);
uint32_t                       dmOutboxFirstIdForNnn(uint16_t nnn);  // 0 when unknown
// F5 (fable-dm-stage1-verdict-20260914.md): the current attempt's id, read
// BEFORE dmOutboxOnAck() frees the entry, so a call site can also stop a
// still-queued XOR-variant ring slot (findAndStopRingSlot(), lora_functions.cpp
// -- the outbox's own first_id-based stop never covers attempt 2+, which
// always carries an XOR retry id, src/pn_retry.h). 0 when unknown.
uint32_t                       dmOutboxLastIdForNnn(uint16_t nnn);
int                            dmOutboxFormatLine(char *buf, size_t n); // "OUTBOX ..." setlog line
void                           dmOutboxReset(void);                  // tests
