// dm_outbox.cpp -- stage 1 outbox and retry ladder core.
//
// docs/dm-stage1-plan-20260914.md sections 3-4 and the wave brief's Core
// section. Platform-neutral (stdint/string/stdio only, same discipline as
// src/msgstore.cpp), driven entirely through the DmOutboxEnv installed by
// dmOutboxInit() -- src/dm_outbox_glue.cpp wires the firmware, fakes in
// test/test_dm_outbox drive it natively.
//
// Same env-struct / gen-counter / snapshot-before-callback pattern as
// msgstore.cpp: dmOutboxLoop() can call out (env->transmit()/env->report())
// while a receive-path hook (dmOutboxOnAck/OnEcho/OnHeld, called from the
// LORA task on the real firmware) mutates the very same entry concurrently.
// entryTouch() bumps a per-entry `gen` on every mutation; dmOutboxLoop()
// snapshots gen before calling out and refuses to apply its own bookkeeping
// afterwards if gen (or state) moved -- "no resurrection" of an entry a
// hook already finished with.
#include "dm_outbox_api.h"

#include <string.h>
#include <stdio.h>

static const struct DmOutboxEnv *s_env   = NULL;
static uint8_t                   s_slots = 0;

static struct DmOutboxEntry    s_entries[DM_OUTBOX_SLOTS_MAX];
static struct DmOutboxCounters s_cnt = {0, 0, 0, 0, 0, 0, 0};

// F7-style (msgstore.cpp precedent): count a blocked-by-bp tick once per
// blocked *episode* (a run of due-but-refused loop calls), not once per
// call.
static bool s_blocked_episode_active = false;

// ---------------------------------------------------------------- helpers

static uint32_t nowMs(void)
{
    return (s_env != NULL && s_env->now_ms != NULL) ? s_env->now_ms() : 0;
}

static void entryTouch(int i)
{
    s_entries[i].gen++;
}

static void copyCall(char *dst, size_t dstsz, const char *src)
{
    if(dstsz == 0)
        return;
    if(src == NULL)
    {
        dst[0] = 0;
        return;
    }
    size_t i = 0;
    for(; i + 1 < dstsz && src[i] != 0; i++)
        dst[i] = src[i];
    dst[i] = 0;
}

// Schedule, decision 2 (docs/dm-stage1-plan-20260914.md section 8 / wave
// brief Core section): attempt k (2..max_attempts) is due at
// first_ms + offset[k]. Block gap (STEP + BLOCK_GAP) is already baked into
// the jump from attempt 3->4 and 6->7 below -- no separate "is this a block
// boundary" branch is needed at call sites.
//
// (dm_outbox_api.h also defines DM_OUTBOX_ECHO_GATE_MS, 15 s, as an earlier
// design's separate echo-decision deadline; the wave brief's concrete
// offset table below supersedes it -- attempt 2 is always due at STEP
// (40 s) and the echo_seen flag it reads by then is whatever OnEcho()
// recorded up to that moment. The constant is left in the header, unused
// here, rather than edited out of a contract file this wave may only add
// to.)
static uint32_t offsetForAttemptMs(uint8_t attempt, uint8_t max_attempts)
{
    static const uint32_t k3[2] = {40000UL, 80000UL};
    static const uint32_t k9[8] = {40000UL, 80000UL, 180000UL, 220000UL,
                                    260000UL, 360000UL, 400000UL, 440000UL};

    if(attempt < 2)
        attempt = 2;

    uint8_t idx = (uint8_t)(attempt - 2);

    if(max_attempts <= 3)
    {
        if(idx > 1)
            idx = 1;
        return k3[idx];
    }

    if(idx > 7)
        idx = 7;
    return k9[idx];
}

// ---------------------------------------------------------------- lifecycle

void dmOutboxInit(const struct DmOutboxEnv *env, uint8_t slots)
{
    s_env   = env;
    s_slots = (slots < 1) ? 1 : (slots > DM_OUTBOX_SLOTS_MAX ? DM_OUTBOX_SLOTS_MAX : slots);
}

void dmOutboxReset(void)
{
    memset(s_entries, 0, sizeof(s_entries));
    memset(&s_cnt, 0, sizeof(s_cnt));
    s_blocked_episode_active = false;
    // s_env / s_slots deliberately untouched: board slot count is a
    // boot-time constant (set once via dmOutboxInit()), not test state.
}

// ------------------------------------------------------------------- add

int dmOutboxAdd(uint16_t nnn, const char *dst, const char *payload, size_t len,
                 uint8_t max_hop, uint32_t first_id, enum DmRetryMode mode)
{
    uint8_t max_attempts;
    switch(mode)
    {
        case DM_RETRY_3: max_attempts = 3; break;
        case DM_RETRY_9: max_attempts = 9; break;
        default:         return -1;   // DM_RETRY_OFF: the caller never enqueues here
    }

    int slot = -1;
    for(int i = 0; i < s_slots; i++)
    {
        if(s_entries[i].state == DMOB_FREE)
        {
            slot = i;
            break;
        }
    }

    if(slot < 0)
    {
        s_cnt.refused_full++;
        return -1;
    }

    struct DmOutboxEntry *e = &s_entries[slot];

    e->nnn = nnn;
    copyCall(e->dst, sizeof(e->dst), dst);

    memset(e->payload, 0, sizeof(e->payload));
    size_t plen = (len > DM_OUTBOX_PAYLOAD_MAX) ? DM_OUTBOX_PAYLOAD_MAX : len;
    if(payload != NULL && plen > 0)
        memcpy(e->payload, payload, plen);
    e->plen = (uint16_t)plen;

    e->max_hop = max_hop;
    e->first_id = first_id;
    e->last_id  = first_id;

    uint32_t now = nowMs();
    e->first_ms = now;
    e->next_ms  = now + offsetForAttemptMs(2, max_attempts);   // == DM_OUTBOX_STEP_MS

    e->attempt      = 1;   // sendMessage() already sent attempt 1 outside the outbox
    e->max_attempts = max_attempts;
    e->state        = DMOB_LADDER;
    e->echo_seen    = false;
    e->held         = false;

    entryTouch(slot);
    s_cnt.added++;
    return slot;
}

bool dmOutboxHasRoom(void)
{
    for(int i = 0; i < s_slots; i++)
        if(s_entries[i].state == DMOB_FREE)
            return true;
    return false;
}

// ------------------------------------------------------------- receive path

void dmOutboxOnEcho(uint32_t msg_id)
{
    if(msg_id == 0)
        return;

    for(int i = 0; i < s_slots; i++)
    {
        if(s_entries[i].state != DMOB_LADDER)
            continue;
        if(s_entries[i].first_id != msg_id && s_entries[i].last_id != msg_id)
            continue;

        s_entries[i].echo_seen = true;
        entryTouch(i);
    }
}

bool dmOutboxOnAck(const char *from, uint16_t nnn)
{
    if(from == NULL)
        return false;

    for(int i = 0; i < s_slots; i++)
    {
        if(s_entries[i].state != DMOB_LADDER)
            continue;
        if(s_entries[i].nnn != nnn)
            continue;
        if(strcmp(s_entries[i].dst, from) != 0)
            continue;

        // -> DONE_ACK, freed immediately: unlike dmOutboxLoop()'s give-up
        // path (which defers freeing to the next tick, see below), nothing
        // else in this same call can still be reading this slot. gen is
        // bumped either way, so a dmOutboxLoop() tick whose transmit()
        // callback synchronously triggered this ack (the reentrancy case
        // the test suite exercises) sees state != LADDER / gen changed on
        // return and skips its own bookkeeping -- no resurrection.
        s_entries[i].state = DMOB_FREE;
        entryTouch(i);
        s_cnt.acked++;

        // Hooks decision (wave brief, hook 2): do NOT call env->report()
        // here. The existing :ackNNN site in lora_functions.cpp already
        // emits the 0x02 phone status frame for msg_counter == first_id on
        // every ack it handles; calling report() again here would duplicate
        // that frame. This function's job is only to stop the ladder and
        // count the outcome.
        return true;
    }

    return false;
}

void dmOutboxOnHeld(uint16_t nnn)
{
    for(int i = 0; i < s_slots; i++)
    {
        if(s_entries[i].state == DMOB_FREE)
            continue;
        if(s_entries[i].nnn != nnn)
            continue;

        s_entries[i].held = true;
        entryTouch(i);
    }
}

// ------------------------------------------------------------------- loop

void dmOutboxLoop(void)
{
    if(s_env == NULL)
        return;

    uint32_t now = nowMs();

    // Free entries dmOutboxLoop() itself gave up on a PREVIOUS tick. Deferred
    // (rather than freed the same tick the give-up happens) so the give-up
    // outcome -- state, counters, the report() call -- is fully settled
    // before the slot disappears; see the give-up branch below.
    for(int i = 0; i < s_slots; i++)
    {
        if(s_entries[i].state == DMOB_DONE_GIVEUP)
        {
            s_entries[i].state = DMOB_FREE;
            entryTouch(i);
        }
    }

    // Decision 4: the TX ring is the only transmit queue -- one attempt
    // enqueued per call at most. Pick the due LADDER entry with the
    // smallest next_ms, same "earliest wins" selection as msgstoreLoop().
    int      candidate      = -1;
    uint32_t candidate_next = 0;

    for(int i = 0; i < s_slots; i++)
    {
        if(s_entries[i].state != DMOB_LADDER)
            continue;
        // millis-wrap-safe due test.
        if((int32_t)(now - s_entries[i].next_ms) < 0)
            continue;
        if(candidate < 0 || (int32_t)(s_entries[i].next_ms - candidate_next) < 0)
        {
            candidate      = i;
            candidate_next = s_entries[i].next_ms;
        }
    }

    if(candidate < 0)
    {
        s_blocked_episode_active = false;   // nothing due, no episode in progress
        return;
    }

    if(s_env->bp_state == NULL || s_env->bp_state() != 0)
    {
        // F7-style: count once per blocked episode, do not advance next_ms
        // -- the same due candidate is retried next tick once bp clears.
        if(!s_blocked_episode_active)
        {
            s_cnt.blocked_bp++;
            s_blocked_episode_active = true;
        }
        return;
    }

    s_blocked_episode_active = false;

    // Echo gate, decision 1: only attempt 2's id choice depends on
    // echo_seen; attempts 3.. always mint fresh.
    uint8_t  next_attempt = (uint8_t)(s_entries[candidate].attempt + 1);
    bool     same_id;
    uint32_t id;

    if(next_attempt == 2 && !s_entries[candidate].echo_seen)
    {
        id      = s_entries[candidate].first_id;
        same_id = true;
    }
    else
    {
        id      = (s_env->mint_id != NULL) ? s_env->mint_id() : 0;
        same_id = false;
    }

    // Snapshot before the callback (msgstore.cpp F4 precedent): transmit()
    // reads dst/payload/max_hop/nnn, none of which a hook ever mutates, but
    // handing it a stack copy keeps that invariant even if that changes,
    // and decouples the callback from the live array entirely.
    uint8_t              gen_before = s_entries[candidate].gen;
    struct DmOutboxEntry snapshot   = s_entries[candidate];

    bool ok = (s_env->transmit != NULL) && s_env->transmit(&snapshot, id);
    if(!ok)
        return;   // ring refused -- retry next tick, nothing advances

    if(s_entries[candidate].state != DMOB_LADDER || s_entries[candidate].gen != gen_before)
        return;   // a hook (ack) settled this slot during transmit() -- leave it as the hook left it

    s_entries[candidate].last_id = id;
    s_entries[candidate].attempt = next_attempt;

    if(same_id)
        s_cnt.same_id_retries++;
    else
        s_cnt.fresh_attempts++;

    if(next_attempt >= s_entries[candidate].max_attempts)
    {
        s_entries[candidate].state = DMOB_DONE_GIVEUP;
        entryTouch(candidate);
        s_cnt.gaveup++;

        // F4 (fable-dm-stage1-verdict-20260914.md): report() now runs for
        // EVERY give-up, held or not -- dm_outbox_api.h's report() contract
        // already says "caller decides the held case", but this function
        // used to decide it here instead, by skipping the call outright.
        // That left the glue's stage 0 counters (dmstat_giveup/
        // dmstat_giveup_held) uncounted for every held give-up, even though
        // OUTBOX give= (s_cnt.gaveup above) always counted it. The glue is
        // still the one that must never put a 0x03 on the wire for a held
        // message (stage 4 decision 3) -- it does that by checking e->held
        // itself before building the phone frame.
        if(s_env->report != NULL)
            s_env->report(&s_entries[candidate], 0x03);
    }
    else
    {
        s_entries[candidate].next_ms = s_entries[candidate].first_ms +
                                        offsetForAttemptMs((uint8_t)(next_attempt + 1),
                                                            s_entries[candidate].max_attempts);
        entryTouch(candidate);
    }
}

// ----------------------------------------------------------------- readers

int dmOutboxUsed(void)
{
    int n = 0;
    for(int i = 0; i < s_slots; i++)
        if(s_entries[i].state != DMOB_FREE)
            n++;
    return n;
}

const struct DmOutboxEntry *dmOutboxEntry(int slot)
{
    if(slot < 0 || slot >= DM_OUTBOX_SLOTS_MAX)
        return NULL;
    if(s_entries[slot].state == DMOB_FREE)
        return NULL;
    return &s_entries[slot];
}

const struct DmOutboxCounters *dmOutboxCounters(void)
{
    return &s_cnt;
}

uint32_t dmOutboxFirstIdForNnn(uint16_t nnn)
{
    for(int i = 0; i < s_slots; i++)
    {
        if(s_entries[i].state == DMOB_FREE)
            continue;
        if(s_entries[i].nnn == nnn)
            return s_entries[i].first_id;
    }
    return 0;
}

uint32_t dmOutboxLastIdForNnn(uint16_t nnn)
{
    for(int i = 0; i < s_slots; i++)
    {
        if(s_entries[i].state == DMOB_FREE)
            continue;
        if(s_entries[i].nnn == nnn)
            return s_entries[i].last_id;
    }
    return 0;
}

int dmOutboxFormatLine(char *buf, size_t n)
{
    if(buf == NULL || n == 0)
        return 0;

    int len = snprintf(buf, n,
        "OUTBOX used=%u/%u add=%u full=%u same=%u fresh=%u ack=%u give=%u bp=%u",
        (unsigned)dmOutboxUsed(), (unsigned)s_slots,
        (unsigned)s_cnt.added, (unsigned)s_cnt.refused_full,
        (unsigned)s_cnt.same_id_retries, (unsigned)s_cnt.fresh_attempts,
        (unsigned)s_cnt.acked, (unsigned)s_cnt.gaveup, (unsigned)s_cnt.blocked_bp);

    if(len < 0)
        return 0;
    return ((size_t)len >= n) ? (int)(n - 1) : len;
}
