/**
 * TM-32: plausibility check of the radio settings after they were loaded from
 * flash (upstream #661, #57). The load paths validate the struct's markers and
 * size (N-12) but not its content: a struct that passes the markers and carries
 * an out-of-range value went straight into radio.setOutputPower() & co.
 *
 * Pure C++, no Arduino dependency -- unit-tested natively (test_settings_sanitize).
 * Sentinels the firmware relies on ("not set": power -20, freq/bw/sf/cr 0) are
 * preserved; only values that are neither a sentinel nor in range are reset to
 * the sentinel, so the existing default logic resolves them.
 */
#pragma once

#include <stddef.h>

struct RadioLimits
{
    int   power_min;        // TX_POWER_MIN
    int   power_max;        // TX_POWER_MAX
    float freq_min;         // plausible band edges in the platform's unit
    float freq_max;         //   (MHz on ESP32, Hz on nRF52)
    int   bw_style;         // 0: kHz values 125/250/500 (ESP32)  1: index 0..2 (nRF52)
    int   cr_style;         // 0: 5..8 (ESP32)                     1: index 1..4 (nRF52)
    int   country_count;    // max_country (exclusive upper bound)
};

struct RadioParams
{
    int   power;
    float freq;
    float bw;
    int   sf;
    int   cr;
    int   country;
};

/* Called once per corrected field with the field name and both values as text. */
typedef void (*sanitize_log_fn)(const char *field, const char *old_value, const char *new_value);

/* Returns the number of fields that had to be corrected. */
int sanitize_radio_params(RadioParams &p, const RadioLimits &lim, sanitize_log_fn log);

/* Makes sure a fixed-size char array is NUL-terminated (a corrupt flash image
 * can lose the terminator; strlen()/printf on it then reads past the field).
 * Returns true if a terminator had to be written. */
bool sanitize_cstring(char *s, size_t n);

/* CS-01: max_hop_text is loaded from flash on both platforms now (ESP32 NVS key
 * "max_hop_text", nRF52 as part of the struct), so an old file or a wiped key
 * hands over a 0 and a corrupt one anything at all. Resets both to the
 * compile-time default (see maxhop.h). Returns true if the value was corrected. */
bool sanitize_max_hop_text(int &v, sanitize_log_fn log);

/* #1132: resolves the stored TX power to the value the radio and the app should
 * use. Both "not set" sentinels count: 0 (structs written before v4.35p and the
 * compat merge) and -20 (default since upstream 50c1ce59). Anything else is
 * returned unchanged; range clamping stays in getPower(). */
int resolve_tx_power(int stored, int board_default);

/* Nachbarschaftsmatrix Stufe 2 (feature-neighbour-matrix): node_sset4 0x0010,
 * 0x0020, 0x0040 and 0x0080 collide with upstream's KISS/TCP feature on the
 * same field (enable/TX/RxMeta/auth) -- a node moving between a KISS build and
 * this branch's build reinterprets the bits (proved on the bench: a KISS
 * build started KISS by itself with TX allowed after booting on a node that
 * had the NBR bits set). Fixed 2026-09-25 by moving the four colliding bits
 * six positions up, into the range upstream's --setlog off and_mask
 * (0x00007FFB) already excludes: 0x0010->0x0400, 0x0020->0x0800,
 * 0x0040->0x1000, 0x0080->0x2000. --nbrreport's 0x0100/0x0200 do not collide
 * and stay put. See docs/nbr-stage2-campaign.md "Bit layout since 2026-09-25".
 *
 * Migrates an EXISTING node_sset4 value loaded from flash: if any bit in
 * 0x00F0 is set, that nibble is cleared and reappears shifted left by 6 (so
 * 0x0010->0x0400 etc); all other bits, including one already migrated or
 * never set, pass through unchanged. Idempotent -- applying it twice is the
 * same as applying it once, since the moved-to range (0x0400-0x2000) is
 * disjoint from the moved-from range (0x0010-0x0080) it tests.
 *
 * This migration is a bridge for nodes that were already flashed with the
 * OLD bits (DK5EN-1, DK5EN-98) and MUST be deleted before this branch ever
 * merges upstream's KISS/TCP feature -- once that happens, 0x0010-0x0080
 * belong to KISS and migrating them would corrupt KISS settings instead. Each
 * call site carries a compile-time #error against KISS_TCP_PORT
 * (src/configuration_global.h on upstream/dev) as a tripwire for that.
 *
 * One residue case changes RF behaviour: a node that ran a KISS build with
 * `--kiss meta on` (0x0040) and is then flashed with this branch comes up
 * with --nbrrelay on's cancel bit (0x1000). Accepted: only bench nodes move
 * between the two builds; `--nbrrelay off` clears it. */
int nbr_sset4_migrate_legacy_bits(int sset4);
