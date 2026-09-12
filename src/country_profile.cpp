#include "configuration.h"
#include "country_profile.h"

// C5 carve-out of the country table out of lora_setchip.cpp; see
// country_profile.h. The case bodies are moved unchanged apart from the
// assignment target (meshcom_settings.node_* -> out.*) and `break;` becoming
// `return true;`.

// RF-08 (BACKLOG), resolved as "not a defect": country 5 ("868") has no real
// APRS/track sub-channel to assign, unlike every other code (Poland, case 15,
// is the other literal, but a real one: 434.855). 999 stands in for "none",
// and is safe *only* because it is out of every actually-shipped radio's
// tunable range, not because anything here gates it.
//
// track mode is reachable on a country-5 node: --track has no country guard
// (bDisplayTrack in loop_functions.cpp toggles unconditionally), so
// out.track_freq does reach meshcom_settings.node_track_freq
// (lora_setchip.cpp:207) and then lora_setchip_aprs()'s ESP32 branch, which
// reads it straight into `rf_freq` (lora_setchip.cpp:~456) and hands it to
// lora_setchip_new() -> radio.setFrequency(rf_freq). It stops there: every
// RadioLib driver this firmware actually instantiates rejects 999 MHz before
// any register write --
//   SX1278::setFrequency (SX127X)                 137 ..  525 MHz
//   SX1262::setFrequency (SX1262X/E22/V3/V4/...)   150 ..  960 MHz
//   SX1268::setFrequency (SX126X/E22)              410 ..  810 MHz
// (RadioLib RADIOLIB_CHECK_RANGE in each modules/SX12{6,7}x/*.cpp) -- 999 is
// above all three ceilings, so setFrequency() returns
// RADIOLIB_ERR_INVALID_FREQUENCY, lora_setchip_new() returns false
// (lora_setchip.cpp:~506-509), lora_setchip_aprs() propagates that, and the
// caller in lora_functions.cpp (~:1888) rolls the TX back -- nothing is ever
// written to an antenna. An EXTERNAL_RADIO board never even gets that far:
// lora_setchip_new()'s EXTERNAL_RADIO branch returns before touching rf_freq
// at all. The nRF52 path is unaffected regardless (lora_setchip.cpp:387
// hardcodes LORA_APRS_FREQUENCY and never reads node_track_freq).
//
// Confirmed 2026-09-12 against the RadioLib version vendored for every board
// this firmware ships (heltec_wifi_lora_32_V3 checked; SX1278/SX1262/SX1268
// share the same range macro across envs). If a future RadioLib version, a
// new chip family, or a change to the EXTERNAL_RADIO bridge removes that
// range check, this sentinel stops being safe -- re-derive the guard before
// touching this value, do not just trust the comment.
#define TRACK_FREQ_NONE_SENTINEL 999 // no APRS/track frequency defined for this region

bool countryProfile(int iCtry, CountryProfile &out)
{
    switch (iCtry)
    {
        case 1:  // UK ... 
            
            #if defined(BOARD_RAK4630) || defined(USE_HELTEC_T114) || defined(BOARD_T_ECHO)
                out.freq = 439912500;
                out.bw = 0;
                out.cr = 1;
            #else
                out.freq = 439.9125;
                out.bw = 125.0;
                out.cr = 6;
            #endif

            out.sf = 10;

            out.track_freq = LORA_APRS_FREQUENCY;
            
            out.preamble = 8;

            return true;

        case 2:  // ON
            out.freq = RF_FREQUENCY;

            #if defined(BOARD_RAK4630) || defined(USE_HELTEC_T114) || defined(BOARD_T_ECHO)
                out.bw = 0;
                out.cr = 2;
            #else
                out.bw = 125.0;
                out.cr = 6;
            #endif

            out.sf = 10;

            out.track_freq = LORA_APRS_FREQUENCY;

            out.preamble = 8;

            return true;

        case 4:  // LA
            out.freq = RF_FREQUENCY;

            #if defined(BOARD_RAK4630) || defined(USE_HELTEC_T114) || defined(BOARD_T_ECHO)
                out.freq = 433925000;
                out.bw = 0;
                out.cr = 2;
            #else
                out.freq = 433.9250;
                out.bw = 125.0;
                out.cr = 6;
            #endif

            out.sf = 10;

            out.track_freq = LORA_APRS_FREQUENCY;

            out.preamble = 8;

            return true;

            case 5:  // 868 ... 

            #if defined(BOARD_RAK4630) || defined(USE_HELTEC_T114) || defined(BOARD_T_ECHO)
                out.freq = 869525000;
                out.bw = 1;
                out.cr = 2;
            #else
                out.freq = 869.525;
                out.bw = 250.0;
                out.cr = 6;
            #endif

            out.sf = LORA_SF;

            out.track_freq = TRACK_FREQ_NONE_SENTINEL;

            out.preamble = 8;

            return true;

        case 6:  // 915 ...

            #if defined(BOARD_RAK4630) || defined(USE_HELTEC_T114) || defined(BOARD_T_ECHO)
                out.freq = 906875000;
                out.bw = 1;
                out.cr = 2;
            #else
                out.freq = 906.875;
                out.bw = 250.0;
                out.cr = 6;
            #endif

            out.sf = LORA_SF;

            out.track_freq = LORA_APRS_FREQUENCY;
            
            out.preamble = 8;

            return true;


        case 8:  // EU Preabble 8 ... 
            out.freq = RF_FREQUENCY;

            #if defined(BOARD_RAK4630) || defined(USE_HELTEC_T114) || defined(BOARD_T_ECHO)
                out.bw = 1;
                out.cr = 2;
            #else
                out.bw = 250.0;
                out.cr = 6;
            #endif

            out.sf = LORA_SF;

            out.track_freq = LORA_APRS_FREQUENCY;

            out.preamble = 8;

            return true;

        case 9:  // UK8 ... 
            
            #if defined(BOARD_RAK4630) || defined(USE_HELTEC_T114) || defined(BOARD_T_ECHO)
                out.freq = 439912500;
                out.bw = 0;
                out.cr = 1;
            #else
                out.freq = 439.9125;
                out.bw = 125.0;
                out.cr = 6;
            #endif

            out.sf = 10;

            out.track_freq = LORA_APRS_FREQUENCY;
            
            out.preamble = 8;

            return true;

        case 10:  // US ... 
            #if defined(BOARD_RAK4630) || defined(USE_HELTEC_T114) || defined(BOARD_T_ECHO)
                out.freq = 433175000;
                out.bw = 1;
                out.cr = 2;
            #else
                out.freq = 433.175;
                out.bw = 250.0;
                out.cr = 6;
            #endif

            out.sf = 11;

            out.track_freq = LORA_APRS_FREQUENCY;
            
            out.preamble = 8;

            return true;

        case 11:  // VR2 ... 
            #if defined(BOARD_RAK4630) || defined(USE_HELTEC_T114) || defined(BOARD_T_ECHO)
                out.freq = 435775000;
                out.bw = 1;
                out.cr = 2;
            #else
                out.freq = 435.775;
                out.bw = 250.0;
                out.cr = 6;
            #endif

            out.sf = 11;

            out.track_freq = LORA_APRS_FREQUENCY;
            
            out.preamble = 8;

            return true;

        case 12:  // 435 ... 
            #if defined(BOARD_RAK4630) || defined(USE_HELTEC_T114) || defined(BOARD_T_ECHO)
                out.freq = 435750000;
                out.bw = 1;
                out.cr = 2;
            #else
                out.freq = 435.750;
                out.bw = 250.0;
                out.cr = 6;
            #endif
            out.sf = 11;

            out.track_freq = LORA_APRS_FREQUENCY;
            
            out.preamble = 8;
            return true;
        case 13:  // 436 ... 
            #if defined(BOARD_RAK4630) || defined(USE_HELTEC_T114) || defined(BOARD_T_ECHO)
                out.freq = 436250000;
                out.bw = 1;
                out.cr = 2;
            #else
                out.freq = 436.250;
                out.bw = 250.0;
                out.cr = 6;
            #endif
            out.sf = 11;

            out.track_freq = LORA_APRS_FREQUENCY;
            
            out.preamble = 8;
            return true;
        case 14:  // 442 ... 
            #if defined(BOARD_RAK4630) || defined(USE_HELTEC_T114) || defined(BOARD_T_ECHO)
                out.freq = 442000000;
                out.bw = 1;
                out.cr = 2;
            #else
                out.freq = 442.000;
                out.bw = 250.0;
                out.cr = 6;
            #endif
            out.sf = 11;

            out.track_freq = LORA_APRS_FREQUENCY;
            
            out.preamble = 8;
            return true;

        case 15:  // PL
            out.freq = RF_FREQUENCY;

            #if defined(BOARD_RAK4630) || defined(USE_HELTEC_T114) || defined(BOARD_T_ECHO)
                out.bw = 1;
                out.cr = 2;
            #else
                out.bw = 250.0;
                out.cr = 6;
            #endif

            out.sf = LORA_SF;

            out.track_freq = 434.855; // LORA_APRS_FREQUENCY for Poland

            out.preamble = 8;

            return true;

        case 7:  // MAN ... manual
            // Not a table entry: it validates what is already stored instead
            // of assigning literals, so the caller keeps it. Without this
            // case the `default` below would swallow code 7 and hand back the
            // EU profile, silently turning manual mode into EU.
            return false;

        default:    // EU
            out.freq = RF_FREQUENCY;

            #if defined(BOARD_RAK4630) || defined(USE_HELTEC_T114) || defined(BOARD_T_ECHO)
                out.bw = 1;
                out.cr = 2;
            #else
                out.bw = 250.0;
                out.cr = 6;
            #endif

            out.sf = LORA_SF;

            out.track_freq = LORA_APRS_FREQUENCY;
            
            out.preamble = LORA_PREAMBLE_LENGTH;

            return true;
    }

    return true;
}
