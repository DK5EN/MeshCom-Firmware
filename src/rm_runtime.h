// rm_runtime.h -- loop-task side of the authenticated remote management DM ("RM1").
// docs/concept-open-issues-20261004.md sections 6.2-6.5 (issue #1189).
//
// The protocol core (remote_cmd.h) only decides; this file drains the 2-slot queue that OnRxDone
// fills (rm_queue.h), runs the verified command through ONE literal console string from a fixed
// table (never the received text), persists the counter high-water mark, answers with a tagged
// DM and, for "reboot", restarts a few seconds after the reply was queued.
//
// Both entry points run in the loop task only (crypto, flash write and sendMessage() are not
// allowed in RX context).
#ifndef RM_RUNTIME_H
#define RM_RUNTIME_H

#include <stdint.h>

// Verdict counters since boot (printed by --info). One per RmVerdict class; ok counts commands that
// passed the check, whether or not their execution then succeeded. rej_disabled counts frames
// that were queued while RM was on and found RM off at drain time (toggle race only: with RM off
// the receive hook does not queue, the DM is ordinary text). rej_format counts rmParse failures;
// those do NOT advance the core's 3-in-90-s lockout (the core never sees an unparsable frame) --
// accepted: they cannot authenticate and change no state.
struct RmStats
{
    uint32_t ok;
    uint32_t cached;
    uint32_t sync;
    uint32_t rej_format;
    uint32_t rej_tag;
    uint32_t rej_replay;
    uint32_t rej_blocked;
    uint32_t rej_rate;
    uint32_t rej_lockout;
    uint32_t rej_disabled;
};

extern RmStats g_rmStats;

// Boot: loads the persisted high-water mark (counters_store.h) into the protocol state. Call once
// after the settings load. rmDrain() calls it lazily if the boot call was missed.
void rmInit(void);

// Loop task, once per pass: handles at most ONE queued RM1 frame and the deferred reboot.
void rmDrain(void);

#endif // RM_RUNTIME_H
