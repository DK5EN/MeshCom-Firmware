#include "configuration.h"
#include "country_profile.h"

// C5 carve-out of the country table out of lora_setchip.cpp; see
// country_profile.h. The case bodies are moved unchanged apart from the
// assignment target (meshcom_settings.node_* -> out.*) and `break;` becoming
// `return true;`.

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

            out.track_freq = 999;

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
