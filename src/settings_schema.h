/**
 * settings_schema: the settings_store::FieldDescriptor table for
 * s_meshcom_settings -- W3 step 2 of the D1-04 target architecture
 * (docs/BACKLOG.md, "D1-04 target architecture: schema-driven settings").
 *
 * This header declares the descriptor accessors plus one extra X-macro list,
 * SETTINGS_PERSIST_ONLY_LIST(X). The .cpp expands TWO field lists into one
 * table:
 *
 *   1. CFG_FIELD_LIST(X)          -- config_json.h, the fields that are both
 *                                     persisted AND exported/imported as JSON.
 *   2. SETTINGS_PERSIST_ONLY_LIST(X) -- defined below, the fields that are
 *                                     persisted (ESP32 NVS today) but were
 *                                     deliberately left out of the JSON
 *                                     export/import table -- flash/firmware
 *                                     bookkeeping and T-Deck-only device UI
 *                                     state. Still need a schema row so a
 *                                     future keyed store can persist them at
 *                                     all; config_json.h's own "deliberately
 *                                     NOT in the file, although they are
 *                                     persisted" comment is what names them.
 *
 * Both lists use EXACTLY the CFG_FIELD_LIST row shape:
 *
 *   X(key, type, member, lo, hi, esc, has_esc)
 *
 * so a single X-macro definition in settings_schema.cpp expands either list
 * the same way. The "esc"/"has_esc" pair IS read, and must be: FieldDescriptor
 * has no escape-sentinel field, so settings_schema.cpp folds has_esc into
 * has_range instead (`(lo <= hi) && !has_esc`). Dropping it as "unused" -- the
 * obvious-looking simplification -- would make settings_store::decode() clamp
 * node_power's -20 "not set" sentinel up to the board minimum on first load.
 * The full reasoning is on the SETTINGS_SCHEMA_ROW macro in the .cpp; do not
 * simplify that expression without reading it.
 *
 * Source of the 17 rows in SETTINGS_PERSIST_ONLY_LIST: doc
 * docs/d1-04-settings-field-triage-20260912.md, S4(b) (25 fields persisted in
 * ESP32 NVS, absent from CFG_FIELD_LIST), filtered to
 *   - classified PERSIST in S3 there (all 25 are), MINUS
 *   - the 6 remaining S4(c) fields (classified RUNTIME despite an NVS slot:
 *     the last-sensor-reading caches) -- these must NOT gain a schema row,
 *     see the comment block below, MINUS
 *   - the 2 Arduino String fields (node_audio_start, node_audio_msg) --
 *     settings_store has no String type and writing a heap pointer into a
 *     settings record is exactly the landmine docs/BACKLOG.md's "step 6"
 *     note warns about, see the comment block below.
 * 25 - 6 - 2 = 17 rows, matching settings_schema.cpp's row count.
 *
 * node_msgid and node_ackid were originally counted among the S4(c)
 * exclusions (8 of them) but are RESTORED here (Fable verdict 2026-09-12,
 * Finding 3): the triage conflated "deliberately not exported" (correct --
 * config_json.h explains why a JSON restore must never rewind these
 * counters) with "deliberately not persisted" (wrong -- the raw-struct blob
 * on nRF52 persisted every field, so dropping them from the schema was a
 * behavioural regression, not a carried-over exclusion). See the comment
 * block below for the corrected reasoning and src/loop_functions.cpp:3331
 * for the effect (msg_id built from a counter that now restarts at 0 every
 * reboot, replaying ids into every neighbour's dedup ring).
 *
 * Key spelling: each row uses the field's ESP32 NVS key from
 * src/esp32/esp32_flash.cpp's save_settings()/init_flash(), so a future
 * NVS-driven path and this schema-driven file store address the same field
 * by the same name (same rule CFG_FIELD_LIST already follows for the export
 * table). A member with no NVS key spelling of its own (none of the 17 rows
 * below has this case -- every one of them already has an ESP32 NVS key)
 * would use its member name verbatim, per the same rule.
 *
 * CFG_CHR: mapped by settings_schema.cpp to settings_store::FieldType::U8,
 * not ::STRING -- a size-1 STRING has no room for a NUL terminator; see the
 * top comment of settings_schema.cpp for the full reasoning (settings_store.h
 * itself already resolves this: "U8 ... covers CFG_CHR's single char").
 */
#ifndef _SETTINGS_SCHEMA_H_
#define _SETTINGS_SCHEMA_H_

#include <config_json.h>
#include <settings_store.h>

#include <stddef.h>

namespace settings_schema {

// The full descriptor table (CFG_FIELD_LIST rows followed by
// SETTINGS_PERSIST_ONLY_LIST rows, in that order). Lives in flash
// (settings_schema.cpp declares it `static const`), not RAM.
const settings_store::FieldDescriptor *fields();

// Number of rows fields() points at. Platform-dependent: CFG_FIELD_LIST_PLATFORM
// and SETTINGS_PERSIST_ONLY_LIST's own platform block both branch on
// `#ifdef ESP32`, and the T-Deck-only rows further branch on
// BOARD_T_DECK/BOARD_T_DECK_PLUS/BOARD_T_DECK_PRO -- so fieldCount() differs
// between nRF52, a non-T-Deck ESP32 board and a T-Deck board.
size_t fieldCount();

/* X(key, type, member, lo, hi, esc, has_esc) -- same row shape as
 * CFG_FIELD_LIST (config_json.h). Persisted-but-not-exported fields only;
 * see the file header comment above for how this list was derived and the
 * file header comment above the platform block below for the two field
 * classes this list deliberately excludes. */
#define SETTINGS_PERSIST_ONLY_LIST(X)                                                            \
    /* Flash/firmware-layout bookkeeping (config_json.h:128-130). Both        \
     * platforms; must persist to detect a stale layout / trigger a pending   \
     * wipe across a reboot; deliberately excluded from JSON export/import so \
     * a restored backup can never fake them. */                             \
    X("node_fversion",  CFG_INT, node_fversion,   CFG_NORANGE, CFG_NOESC)                        \
    X("node_mversion",  CFG_INT, node_mversion,   CFG_NORANGE, CFG_NOESC)                        \
    X("node_fwversion", CFG_STR, node_fwversion,  CFG_NORANGE, CFG_NOESC)                        \
    /* one-shot "wipe at next boot" trigger (config_json.h:131-132); NVS key  \
     * is "node_cflash", member is node_cleanflash. */                       \
    X("node_cflash",    CFG_INT, node_cleanflash, CFG_NORANGE, CFG_NOESC)                        \
    /* Running message-id / ack-id counters (both platforms). Persisted so   \
     * a reboot does not replay msg_ids the mesh has already seen; NOT in    \
     * CFG_FIELD_LIST on purpose (config_json.h:362-367) -- restoring a      \
     * backup must never rewind them. See the comment block below. */       \
    X("node_msgid",     CFG_INT, node_msgid,      CFG_NORANGE, CFG_NOESC)                        \
    X("node_ackid",     CFG_INT, node_ackid,      CFG_NORANGE, CFG_NOESC)                        \
    SETTINGS_PERSIST_ONLY_LIST_PLATFORM(X)

/* Platform-only persisted-but-not-exported fields: the 11 T-Deck device
 * UI/behaviour rows from the triage's S4(b) list that are NOT already
 * covered by the 4 "both platforms" rows above. These 11 struct members
 * exist ONLY when BOARD_T_DECK / BOARD_T_DECK_PLUS / BOARD_T_DECK_PRO is
 * defined (src/esp32/esp32_flash.h:224-240's own `#if` guard) -- narrower
 * than plain `#ifdef ESP32`, unlike CFG_FIELD_LIST_PLATFORM's ESP32 block
 * (node_disp_rot and the spectrum-scan fields, which have no such further
 * guard and exist on every ESP32 board). Two of the 13 T-Deck fields named
 * in the triage, node_audio_start and node_audio_msg, are Arduino `String`
 * members (esp32_flash.h:226-227) and are excluded here on purpose:
 * settings_store::FieldType has no String type, and writing a heap pointer
 * (a String's internal buffer pointer) into a settings record via
 * offsetof/sizeof would silently serialise raw pointer bytes instead of
 * string content -- exactly "A landmine for step 6" that docs/BACKLOG.md
 * already records. Giving them a schema row needs settings_store to grow a
 * bounded-String encoding first; that is not this wave's file set. */
#ifdef ESP32
    #if defined(BOARD_T_DECK) || defined(BOARD_T_DECK_PLUS) || defined(BOARD_T_DECK_PRO)
        #define SETTINGS_PERSIST_ONLY_LIST_PLATFORM(X)                                           \
            X("node_map",      CFG_INT,  node_map,             CFG_NORANGE, CFG_NOESC)          \
            X("node_kblock",   CFG_BOOL, node_keyboardlock,     CFG_NORANGE, CFG_NOESC)          \
            X("node_bllock",   CFG_BOOL, node_backlightlock,    CFG_NORANGE, CFG_NOESC)          \
            X("node_kllock",   CFG_BOOL, node_kbllightlock,     CFG_NORANGE, CFG_NOESC)          \
            X("node_modus",    CFG_INT,  node_modus,            CFG_NORANGE, CFG_NOESC)          \
            X("node_mute",     CFG_BOOL, node_mute,             CFG_NORANGE, CFG_NOESC)          \
            X("node_perflash", CFG_BOOL, node_persist_to_flash, CFG_NORANGE, CFG_NOESC)          \
            X("node_persd",    CFG_BOOL, node_persist_to_sd,    CFG_NORANGE, CFG_NOESC)          \
            X("node_immsave",  CFG_BOOL, node_immediate_save,   CFG_NORANGE, CFG_NOESC)          \
            X("node_kblsync",  CFG_BOOL, node_kbl_sync,         CFG_NORANGE, CFG_NOESC)          \
            X("node_wifion",   CFG_BOOL, node_wifion,           CFG_NORANGE, CFG_NOESC)
    #else
        #define SETTINGS_PERSIST_ONLY_LIST_PLATFORM(X)
    #endif
#else
    #define SETTINGS_PERSIST_ONLY_LIST_PLATFORM(X)
#endif

/* Deliberately NOT given a schema row, although each has an ESP32 NVS slot
 * today (triage doc S4(c)): node_temp, node_hum, node_press, node_temp2,
 * node_gas_res, node_co2 -- the last live sensor readings, not configuration.
 * config_json.cpp already calls them "not configuration" for the export
 * table, and the same is true here: two schema-encoded dumps of an
 * unchanged node would differ solely because a sensor ticked between them.
 *
 * node_msgid / node_ackid are NOT in this exclusion list -- they DO get a
 * descriptor, in SETTINGS_PERSIST_ONLY_LIST above, so a schema-driven store
 * persists them the same way the raw-struct blob always did. What they are
 * still excluded from is CFG_FIELD_LIST, the JSON export/import table
 * (config_json.h:362-367): config_json.cpp's reasoning there -- that
 * *importing* a foreign value would rewind the counter and collide with the
 * dedup ring of every neighbour -- is a reason to keep them out of a
 * restorable backup file, not a reason to stop writing them to flash on
 * every boot. Those are two different questions; treating them as one and
 * dropping the descriptor entirely was the regression Fable Finding 3
 * (2026-09-12) caught, since the nRF52's previous raw-struct persistence
 * covered every field including these two. */

} // namespace settings_schema

#endif
