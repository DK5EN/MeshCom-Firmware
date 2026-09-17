#pragma once

/*
 * Fleet defaults for the radio parameters every variant used to restate.
 *
 * W7 / audit rows D6-01..03. Nine macros were written out in all 31
 * variants/<board>/configuration.h, and 238 of those 279 lines said the same
 * thing -- 433.175000 appeared 28 times, "LORA_SF 11" 31 times. A value that
 * is repeated 28 times is not a per-board setting, it is a fleet default with
 * 28 copies, and the copies are what drift: a frequency correction had to be
 * applied 28 times or it was applied inconsistently, with nothing to catch it.
 *
 * HOW THE OVERRIDE WORKS, and why the include sits at the END of each variant
 * header. Every macro here is #ifndef-guarded, and each variant includes this
 * file as its LAST line -- so a board that sets its own value has already set
 * it by the time we get here and the guard skips us. There is no precedence
 * puzzle and no include-order dependency: local wins, always, because local
 * came first. A board that differs keeps exactly one line, next to the other
 * things that make it different.
 *
 * WHAT IS DELIBERATELY NOT HERE.
 *
 *   - Anything the source presence-tests with #ifdef. 73 macros are tested
 *     that way (ENABLE_GPS, ENABLE_BMX280, ...), and for those the ABSENCE of
 *     a definition is the signal. Defining them here would switch features on
 *     for every board that had deliberately stayed silent; the opt-out would
 *     be an #undef per board, and an #undef of a macro that was never defined
 *     is legal and silent, so one typo would disable a sensor fleet-wide with
 *     no compiler warning. That half of the row is a separate step with its
 *     own per-variant assertion, not a bulk move.
 *
 *   - MODUL_HARDWARE. It is the board's identity; no value is a sensible
 *     default. A new variant that forgot the line would silently come up as
 *     an EBYTE_E22.
 *
 *   - BUTTON_PIN. It is a GPIO number. Only 13 of 31 agree, and a board that
 *     inherited a wrong pin would read a floating input as a button press
 *     (see the note on iButtonPin in init_onebutton()).
 *
 * Only macros defined by EVERY variant are here, which is what makes this
 * change presence-neutral by construction: no board gains a macro it did not
 * already have, so nothing that tests #ifdef can change its mind. The values
 * are proven unchanged per board by test/golden/variant_macros_effective.py,
 * which records what the compiler actually sees for all 32 board envs; a
 * correct hoist leaves that baseline byte-identical.
 */

#ifndef RF_FREQUENCY
#define RF_FREQUENCY 433.175000                 // Hz -- 3 nRF52 boards use the integer form
#endif

#ifndef LORA_APRS_FREQUENCY
#define LORA_APRS_FREQUENCY 433.775000          // Hz
#endif

#ifndef LORA_BANDWIDTH
#define LORA_BANDWIDTH 250                      // [0: 125 kHz, 1: 250 kHz, 2: 500 kHz]
#endif

#ifndef LORA_SF
#define LORA_SF 11                              // [SF7..SF12]
#endif

#ifndef LORA_CR
#define LORA_CR 6                               // [1: 4/5, 2: 4/6, 3: 4/7, 4: 4/8]
#endif

#ifndef LORA_PREAMBLE_LENGTH
#define LORA_PREAMBLE_LENGTH DEFAULT_PREAMPLE_LENGTH   // same for Tx and Rx
#endif

#ifndef TX_OUTPUT_POWER
#define TX_OUTPUT_POWER 22                      // dBm
#endif

#ifndef TX_POWER_MAX
#define TX_POWER_MAX 22                         // dBm, ceiling for --txpower
#endif

#ifndef TX_POWER_MIN
#define TX_POWER_MIN -9                         // dBm, floor for --txpower
#endif
