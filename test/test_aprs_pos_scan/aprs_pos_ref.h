// Reference for test_aprs_pos_scan: decodeAPRSPOS(const char *) and its tag helper as they stood before
// B-APRS-POS-01 (src/aprs_functions.cpp at fork-dev cdfc76ea plus B-APRS-PATH-01), verbatim, renamed refDecodeAPRSPOS /
// refExtractTag. It calls the unchanged initAPRSPOS() and CheckGroup(String) of the firmware.
#ifndef APRS_POS_REF_H
#define APRS_POS_REF_H
#include <stdio.h>
#include <string.h>
#include <Arduino.h>
#include <aprs_functions.h>

enum RefTagType
{
    REF_TAG_INT,
    REF_TAG_INT_AUTOBASE,
    REF_TAG_FLOAT,
};

static void refExtractTag(const char *PayloadBuffer, size_t PayloadLen, unsigned int istarttext, char tagchar, RefTagType type, void *target)
{
    char decode_text[8];
    memset(decode_text, 0x00, sizeof(decode_text));
    unsigned int ipt = 0;

    for(unsigned int itxt=istarttext; itxt<PayloadLen; itxt++)
    {
        if(PayloadBuffer[itxt] == '/' && PayloadBuffer[itxt+1] == tagchar && PayloadBuffer[itxt+2] == '=')
        {
            for(unsigned int id=itxt+3; id<PayloadLen; id++)
            {
                // ENDE
                if(PayloadBuffer[id] == '/' || PayloadBuffer[id] == ' ' || id == PayloadLen || ipt > 6)
                {
                    switch(type)
                    {
                        case REF_TAG_INT:          sscanf(decode_text, "%d", (int *)target);   break;
                        case REF_TAG_INT_AUTOBASE: sscanf(decode_text, "%i", (int *)target);   break;
                        case REF_TAG_FLOAT:        sscanf(decode_text, "%f", (float *)target); break;
                    }
                    break;
                }

                if(ipt < 7)
                {
                    decode_text[ipt]=PayloadBuffer[id];
                    ipt++;
                }
            }

            break;
        }
    }
}

// Payload is copied once into a stack buffer (no String churn per packet). Same
// semantics as the String version: a ' ' is appended and bytes past the end read 0.
static uint16_t refDecodeAPRSPOS(const char *Payload, struct aprsPosition &aprspos)
{
    initAPRSPOS(aprspos);

    char decode_text[25];
    unsigned int itxt=0;
    int istarttext = 0;

    char PayloadBuffer[MC_PAYLOAD_LEN + 3];   // +1 appended ' ', +2 lookahead reads past the end
    memset(PayloadBuffer, 0x00, sizeof(PayloadBuffer));
    size_t PayloadLen = Payload ? strnlen(Payload, MC_PAYLOAD_LEN) : 0;
    memcpy(PayloadBuffer, Payload, PayloadLen);
    PayloadBuffer[PayloadLen++] = ' ';

    memset(decode_text, 0x00, sizeof(decode_text));
    int ipt=0;

    for(itxt=0; itxt<PayloadLen; itxt++)
    {
        if(PayloadBuffer[itxt] == 'N' || PayloadBuffer[itxt] == 'S' || ipt > 10)
        {
            // ipt>10 alone is an overrun brake, not a hemisphere match --
            // only accept the byte at the cut-off as lat_c/aprs_group when it
            // really is 'N'/'S'. Otherwise reject the position (keep the
            // initAPRSPOS() defaults) instead of reading a fabricated
            // hemisphere/group byte and an 11-digit fantasy lat.
            if(PayloadBuffer[itxt] != 'N' && PayloadBuffer[itxt] != 'S')
                return 0x00;

            decode_text[ipt]=0x00;

            sscanf(decode_text, "%lf", &aprspos.lat);

            aprspos.lat_c = PayloadBuffer[itxt];
            itxt++;
            aprspos.aprs_group = PayloadBuffer[itxt];

            istarttext = itxt+1;    // Char-Symbol 1
            break;
        }
        else
        {
            if(ipt < 11)
            {
                decode_text[ipt]=PayloadBuffer[itxt];
                ipt++;
            }
        }
    }

    memset(decode_text, 0x00, sizeof(decode_text));
    ipt=0;

    for(itxt=istarttext; itxt<PayloadLen; itxt++)
    {
        if(PayloadBuffer[itxt] == 'W' || PayloadBuffer[itxt] == 'E' || ipt > 10)
        {
            // Same overrun-vs-hemisphere check as the latitude loop above.
            if(PayloadBuffer[itxt] != 'W' && PayloadBuffer[itxt] != 'E')
                return 0x00;

            decode_text[ipt]=0x00;

            sscanf(decode_text, "%lf", &aprspos.lon);
            aprspos.lon_c = PayloadBuffer[itxt];
            itxt++;
            aprspos.aprs_symbol = PayloadBuffer[itxt];

            istarttext = itxt+1;    // Char-Symbol 2
            break;
        }
        else
        {
            if(ipt < 11)
            {
                decode_text[ipt]=PayloadBuffer[itxt];
                ipt++;
            }
        }
    }

    ipt=0;

    // check ATXT + #name:
    // the comment/name region runs from istarttext up to the first /X=-style
    // token -- '/' followed by an uppercase letter and '=', or '/N' followed
    // by a digit '1'-'9' (the neighbour-count key, matched the same way the
    // NCNT loop below matches it). Nothing else ends the region: not a
    // space, not a bare '/'. Region cap mirrors the encoder's own budget
    // (atxt 25 + '#' 1 + node_name 19 = 45 bytes); the local buffer is 48
    // for headroom, capped at 47 to leave room for the terminator.
    char cregion[48];
    memset(cregion, 0x00, sizeof(cregion));
    int iregion = 0;

    for(unsigned int id=istarttext; id<PayloadLen && iregion < 47; id++)
    {
        char c = PayloadBuffer[id];

        if(c == '/')
        {
            char c1 = PayloadBuffer[id+1];
            char c2 = PayloadBuffer[id+2];

            if((c1 >= 'A' && c1 <= 'Z' && c2 == '=') || (c1 == 'N' && c2 >= '1' && c2 <= '9'))
                break;
        }

        cregion[iregion] = c;
        iregion++;
    }

    // Split on the LAST '#' in the region: text before it is the free-text
    // comment (pos_atxt), text after it is the node name (pos_name). No '#'
    // -> the whole region is the comment and pos_name stays empty. A '#'
    // can never appear in a name written via --setname (command_functions.cpp),
    // so the last-'#' split is unambiguous for names this firmware writes;
    // it degrades gracefully (name = everything after the last '#') for a
    // comment that legitimately contains '#' from an older/foreign encoder.
    int ihash = -1;

    for(int ic=iregion-1; ic>=0; ic--)
    {
        if(cregion[ic] == '#')
        {
            ihash = ic;
            break;
        }
    }

    if(ihash < 0)
    {
        aprspos.pos_atxt = cregion;
        aprspos.pos_name = "";
    }
    else
    {
        cregion[ihash] = 0x00;
        aprspos.pos_atxt = cregion;
        aprspos.pos_name = cregion + ihash + 1;
    }

    aprspos.bat = 0;
    aprspos.alt = 0;

    // check Batt
    refExtractTag(PayloadBuffer, PayloadLen, istarttext, 'B', REF_TAG_INT, &aprspos.bat);

    // check Altitute
    refExtractTag(PayloadBuffer, PayloadLen, istarttext, 'A', REF_TAG_INT, &aprspos.alt);

    // check Press
    refExtractTag(PayloadBuffer, PayloadLen, istarttext, 'P', REF_TAG_FLOAT, &aprspos.press);

    // check Hum
    refExtractTag(PayloadBuffer, PayloadLen, istarttext, 'H', REF_TAG_FLOAT, &aprspos.hum);

    // check Temp
    refExtractTag(PayloadBuffer, PayloadLen, istarttext, 'T', REF_TAG_FLOAT, &aprspos.temp);

    // check Temp2
    refExtractTag(PayloadBuffer, PayloadLen, istarttext, 'O', REF_TAG_FLOAT, &aprspos.temp2);

    // check QFE
    refExtractTag(PayloadBuffer, PayloadLen, istarttext, 'F', REF_TAG_INT, &aprspos.qfe);

    // check QNH
    refExtractTag(PayloadBuffer, PayloadLen, istarttext, 'Q', REF_TAG_FLOAT, &aprspos.qnh);

    // check GASRES
    refExtractTag(PayloadBuffer, PayloadLen, istarttext, 'G', REF_TAG_FLOAT, &aprspos.gasres);

    // check NCNT
    memset(decode_text, 0x00, sizeof(decode_text));
    ipt=0;
    
    for(itxt=istarttext; itxt<PayloadLen; itxt++)
    {
        if(PayloadBuffer[itxt] == '/' && PayloadBuffer[itxt+1] == 'N' && PayloadBuffer[itxt+2] >= '1' && PayloadBuffer[itxt+2] <= '9')
        {
            for(unsigned int id=itxt+2;id<PayloadLen;id++)
            {
                // ENDE
                if(PayloadBuffer[id] == '/' || PayloadBuffer[id] == ' ' || id == PayloadLen || ipt > 6)
                {
                    sscanf(decode_text, "%d", &aprspos.ncnt);
                    break;
                }

                if(ipt < 3)
                {
                    decode_text[ipt]=PayloadBuffer[id];
                    ipt++;
                }
            }

            break;
        }
    }

    // check CO2
    refExtractTag(PayloadBuffer, PayloadLen, istarttext, 'C', REF_TAG_FLOAT, &aprspos.co2);

    // check GRC (Group-Call list) /R=; up to 6 groups separated by ';'.
    // Own buffer sized for the worst case (6 x "99999;" = 36 chars) instead
    // of the shared decode_text[25] the other keys use.
    {
        char decode_grc[40];
        memset(decode_grc, 0x00, sizeof(decode_grc));
        int igrc = 0;

        for(itxt=istarttext; itxt<PayloadLen; itxt++)
        {
            if(PayloadBuffer[itxt] == '/' && PayloadBuffer[itxt+1] == 'R' && PayloadBuffer[itxt+2] == '=')
            {
                for(unsigned int id=itxt+3;id<PayloadLen;id++)
                {
                    // ENDE
                    if(PayloadBuffer[id] == '/' || PayloadBuffer[id] == ' ' || id == PayloadLen || igrc > 38)
                    {
                        break;
                    }

                    decode_grc[igrc]=PayloadBuffer[id];
                    igrc++;
                }

                // Split on ';', validating each group with CheckGroup() (the
                // same 1..99999 range accepted on the air) and stopping at
                // the first token that fails -- malformed, truncated or
                // non-numeric.
                int istart_tok = 0;
                char ctoken[8];

                for(int ic=0; ic<=igrc && aprspos.grccnt < 6; ic++)
                {
                    if(ic == igrc || decode_grc[ic] == ';')
                    {
                        int toklen = ic - istart_tok;

                        if(toklen <= 0 || toklen >= (int)sizeof(ctoken))
                            break;

                        memset(ctoken, 0x00, sizeof(ctoken));
                        memcpy(ctoken, decode_grc + istart_tok, toklen);

                        int grcval = CheckGroup(String(ctoken));

                        if(grcval == 0)
                            break;

                        aprspos.grc[aprspos.grccnt] = grcval;
                        aprspos.grccnt++;

                        istart_tok = ic+1;
                    }
                }

                break;
            }
        }
    }

    // check version
    refExtractTag(PayloadBuffer, PayloadLen, istarttext, 'V', REF_TAG_INT_AUTOBASE, &aprspos.version);

    // check Bus-Voltage /U=
    refExtractTag(PayloadBuffer, PayloadLen, istarttext, 'U', REF_TAG_FLOAT, &aprspos.vbus);

    // check Current /I=
    refExtractTag(PayloadBuffer, PayloadLen, istarttext, 'I', REF_TAG_FLOAT, &aprspos.vcurrent);

    // check telemetry
    refExtractTag(PayloadBuffer, PayloadLen, istarttext, 'Y', REF_TAG_INT_AUTOBASE, &aprspos.telemetry);

    memset(decode_text, 0x00, sizeof(decode_text));
    ipt=0;

    // check Digital /D=
    for(itxt=istarttext; itxt<PayloadLen; itxt++)
    {
        if(PayloadBuffer[itxt] == '/' && PayloadBuffer[itxt+1] == 'D' && PayloadBuffer[itxt+2] == '=')
        {
            bool din_overflow = false;

            for(unsigned int id=itxt+3;id<PayloadLen;id++)
            {
                // ENDE
                if(PayloadBuffer[id] == '/' || PayloadBuffer[id] == ' ' || id == PayloadLen)
                {
                    break;
                }

                if(ipt < 8)
                {
                    decode_text[ipt]=PayloadBuffer[id];
                    ipt++;
                }
                else
                {
                    // 9th+ data byte -- token too long, reject below
                    din_overflow = true;
                }
            }

            if(!din_overflow && ipt == 8)
            {
                bool din_valid = true;

                for(int idb=0; idb<8; idb++)
                {
                    if(decode_text[idb] != '0' && decode_text[idb] != '1')
                    {
                        din_valid = false;
                        break;
                    }
                }

                if(din_valid)
                {
                    memcpy(aprspos.din, decode_text, 8);
                    aprspos.din[8] = 0x00;
                }
            }

            break;
        }
    }

    aprspos.lat_d = (int)(aprspos.lat / 100.0);
    double min = aprspos.lat - (aprspos.lat_d * 100.0);
    aprspos.lat_d = aprspos.lat_d + (min / 60.0);

    aprspos.lon_d = (int)(aprspos.lon / 100.0);
    min = aprspos.lon - (aprspos.lon_d * 100.0);
    aprspos.lon_d = aprspos.lon_d + (min / 60.0);

    return 0x01;
}

#endif
