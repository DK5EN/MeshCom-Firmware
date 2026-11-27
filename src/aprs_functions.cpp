#include "mc_text.h"
#include "aprs_path_scan.h"
#include <aprs_functions.h>
#include <loop_functions.h>
#include <loop_functions_extern.h>
#include <lora_setchip.h>
#include <regex_functions.h>
#include <debugconf.h>
#include <configuration.h>
#include <charset_filter.h>

#define MAX_APRS_FRAME_SIZE 340

// R2-04: die Feldbreiten in aprs_structures.h stehen dort als Literale, weil
// dieser Header nicht configuration_global.h hereinziehen darf (Begruendung
// ebendort). Hier sind beide sichtbar, also wird die Kopplung hier geprueft --
// laeuft MAX_CALL_LEN oder UDP_TX_BUF_SIZE weg, bricht der Build, nicht der
// Funk.
static_assert(MC_CALL_LEN_Z == MAX_CALL_LEN + 1,
              "MC_CALL_LEN_Z und MAX_CALL_LEN sind auseinandergelaufen");
static_assert(MC_PAYLOAD_LEN == UDP_TX_BUF_SIZE + 1,
              "MC_PAYLOAD_LEN und UDP_TX_BUF_SIZE sind auseinandergelaufen");

char shortSUBVERSION()
{
    char csfw[2]={0};
    snprintf(csfw, sizeof(csfw), "%-1.1s", SOURCE_VERSION_SUB);
    return csfw[0];
}

uint8_t shortVERSION()
{
    char cfw[5]={0};
    memcpy(cfw, SOURCE_VERSION+2, 2);
    int iversion;
    sscanf(cfw, "%d", &iversion);
    return (uint8_t)iversion;
}

int CheckGroup(String callsign)
{
	if(callsign.length() < 1 || callsign.length() > 6)
		return 0;
	
	for(int ic=0;ic<(int)callsign.length();ic++)
	{
		if(callsign.charAt(ic) == 0x00)
			break;

		if(callsign.charAt(ic) < 0x30 || callsign.charAt(ic) > 0x39)
			return 0;
	}

	int ig=callsign.toInt();

	if(ig == 100001)
        return ig;
        
    if(ig < 1 || ig > 99999)
		return 0;
	
	return ig;
}

int CheckGroup(const char *callsign)
{
	// mirrors CheckGroup(String): 1..6 chars, digits only, 1..99999 or 100001
	size_t len = callsign ? strnlen(callsign, 7) : 0;
	if(len < 1 || len > 6)
		return 0;

	for(size_t ic=0;ic<len;ic++)
	{
		if(callsign[ic] < 0x30 || callsign[ic] > 0x39)
			return 0;
	}

	// B-APRS-PATH-01: the value of the 1..6 digits checked above, over exactly those digits (atoi() would stop
	// at the NUL right after them: same value, but its loop has no bound a model checker can see)
	int ig=0;
	for(size_t ic=0;ic<len;ic++)
		ig = ig*10 + (callsign[ic] - '0');

	if(ig == 100001)
        return ig;

    if(ig < 1 || ig > 99999)
		return 0;

	return ig;
}

bool CheckOwnGroup(String callsign)
{
    if(strcmp(callsign.c_str(), "*") == 0)
        return false;
        
    // no Group-Check
    int checkgroup = CheckGroup(callsign);

    if(bDisplayInfo && checkgroup <= 99999)
        Serial.printf("\n[INFO]...Check GRC %i for own-node\n", checkgroup);

    if(checkgroup == 0)
        return false;

    bool bHasGroup=false;

    for(int ig=0;ig<6;ig++)
    {
        if(meshcom_settings.node_gcb[ig] > 0 && meshcom_settings.node_gcb[ig] < 100000)
            bHasGroup=true;

        if(meshcom_settings.node_gcb[ig] == checkgroup)
        {
            if(bDisplayInfo && checkgroup <= 99999)
                Serial.printf("[INFO]...GRC %i used on own-node\n", checkgroup);

            return true;
        }
    }

    if(bHasGroup)
    {
        if(bDisplayInfo && checkgroup <= 99999)
            Serial.printf("[INFO]...GRC not used for own-node %s\n", callsign.c_str());

        return false;
    }

    return true;
}

void initAPRS(struct aprsMessage &aprsmsg, char msgType)
{
    aprsmsg.msg_len = 0;
    aprsmsg.msg_id = 0;
    aprsmsg.payload_type = msgType;
    aprsmsg.max_hop = meshcom_settings.max_hop_pos;    // other
    if(msgType == ':' || msgType == '@')
        aprsmsg.max_hop = meshcom_settings.max_hop_text;    // TEXT
    aprsmsg.msg_server = false;
    aprsmsg.msg_track = false;
    aprsmsg.msg_app_offline = false;
    aprsmsg.msg_mesh = false;
    aprsmsg.msg_source_path[0] = 0;
    aprsmsg.msg_destination_path[0] = 0;
    aprsmsg.msg_destination_call[0] = 0;
    aprsmsg.msg_gateway_call[0] = 0;
    aprsmsg.msg_payload[0] = 0;
    aprsmsg.msg_fcs = 0;
    aprsmsg.msg_source_hw = BOARD_HARDWARE;
    
    aprsmsg.msg_source_mod = (getMOD() & 0xF) | (meshcom_settings.node_country << 4);

    aprsmsg.msg_source_fw_version = shortVERSION();
    aprsmsg.msg_source_fw_sub_version = shortSUBVERSION();
    aprsmsg.msg_source_fw_on_air = false;
    aprsmsg.msg_last_hw = 0x80 | BOARD_HARDWARE;    // mit lastHeard Bit
    aprsmsg.msg_source_last[0] = 0;
    // R2-04: msg_source_call FEHLTE hier. Solange die Felder Arduino-String
    // waren, war ein frisch angelegtes aprsMessage auf dem Stack in diesem
    // einen Feld trotzdem leer -- der String-Konstruktor hat es erledigt, und
    // initAPRS() musste es nie tun. Als char[] ist ein Stack-Exemplar
    // UNINITIALISIERT, und das Feld haette den Inhalt des vorigen Frames
    // behalten. test_aprs_corpus (f008) hat genau das gefangen: srccall= wurde
    // zu srccall=DK5EN-98, dem Rufzeichen aus dem Frame davor.
    aprsmsg.msg_source_call[0] = 0;
    aprsmsg.msg_last_path_cnt = 0;
}

uint16_t decodeAPRS(uint8_t RcvBuffer[UDP_TX_BUF_SIZE], uint16_t rsize, struct aprsMessage &aprsmsg)
{
    uint8_t temp[11];

    initAPRS(aprsmsg, 0x00);    // decode init

    aprsmsg.msg_len = rsize;

    if(RcvBuffer[0] == 0x41)    // ACK
        return 0x41;

    if(RcvBuffer[0] == 0x3C)    // loRaAPRS Packet
        return 0x00;

    if(rsize < 16)
    {
        if(bLORADEBUG)
        {
            Serial.printf("APRS decode - Packet discarded, wrong APRS-protocol - size <%i> to short!\n", rsize);

            if(rsize < 255)
                printAsciiBuffer(RcvBuffer, rsize);
        }

        return 0x00;
    }

    if(rsize > MAX_APRS_FRAME_SIZE)
        return 0x00;

    aprsmsg.payload_type = RcvBuffer[0];

    if(aprsmsg.payload_type == 0x3A || aprsmsg.payload_type == 0x21 || aprsmsg.payload_type == 0x40)
    {
        aprsmsg.msg_id = RcvBuffer[1] | (RcvBuffer[2] << 8) | (RcvBuffer[3] << 16) | (RcvBuffer[4] << 24);

        aprsmsg.max_hop = RcvBuffer[5] & 0x0F;

        if((RcvBuffer[5] & 0x80) == 0x80)
            aprsmsg.msg_server = true;

        if((RcvBuffer[5] & 0x40) == 0x40)
            aprsmsg.msg_track = true;

        if((RcvBuffer[5] & 0x20) == 0x20)
            aprsmsg.msg_app_offline = true;

        if((RcvBuffer[5] & 0x10) == 0x10)
            aprsmsg.msg_mesh = true;

        uint16_t inext=0;

        bool bCallsignOk=true;

        // Source Path (B-APRS-PATH-01, 2026-11-26): the field is scanned once for its end and split at the
        // first and last comma; every output is one range of RcvBuffer (aprs_path_scan.h). Same result as
        // the former byte-by-byte copy into three 255-byte stack buffers, which the bounded model checker
        // could not close (the write position depended on the whole byte history).
        const AprsPathField src = aprsPathScan(RcvBuffer, 6, rsize, '>');
        aprsmsg.msg_last_path_cnt = (uint8_t)(1 + src.commas);
        if(src.found)
            inext = (uint16_t)(src.end + 1);
        bool bSourceEndOk = src.found;

        aprsPathCopy(aprsmsg.msg_source_path, sizeof(aprsmsg.msg_source_path), RcvBuffer, src.start, src.end);
        aprsPathCopy(aprsmsg.msg_source_last, sizeof(aprsmsg.msg_source_last), RcvBuffer, src.last_start, src.end);
        aprsPathCopy(aprsmsg.msg_source_call, sizeof(aprsmsg.msg_source_call), RcvBuffer, src.start, src.first_end);

        if(!bSourceEndOk)
        {
                if(bLORADEBUG)
                {
                    Serial.printf("APRS decode - Packet discarded, wrong APRS-protocol - bSourceEndOk (>) missing!\n");
            
                    if(rsize < 255)
                        printAsciiBuffer(RcvBuffer, rsize);
                }

            return 0x00;
        }

        if(!checkRegexCall(aprsmsg.msg_source_last))
        {
            if(bLORADEBUG)
            {
                Serial.printf("APRS decode - Source-Last-CallSign Error [%s]\n", aprsmsg.msg_source_last);
            }

            bCallsignOk=false;
        }

        if(!checkRegexCall(aprsmsg.msg_source_call))
        {
            if(bLORADEBUG)
            {
                Serial.printf("APRS decode - Source-CallSign Error [%s]\n", aprsmsg.msg_source_call);
            }

            bCallsignOk=false;
        }

        // QA-RX-07: routing/group marks are destinations only, never a source
        // (a DM from "*" would make the receiver broadcast an :ack).
        if(isSpecialTargetCall(aprsmsg.msg_source_call))
        {
            if(bLORADEBUG)
            {
                Serial.printf("APRS decode - Source-CallSign is a routing mark [%s]\n", aprsmsg.msg_source_call);
            }

            bCallsignOk=false;
        }

        if(!bCallsignOk)
        {
            return 0x00;
        }

        // Destination Path: same range scan, terminated by the payload type (B-APRS-PATH-01).
        const AprsPathField dst = aprsPathScan(RcvBuffer, inext, rsize, aprsmsg.payload_type);
        if(dst.found)
            inext = (uint16_t)(dst.end + 1);
        bool bDestinationEndOk = dst.found;

        aprsPathCopy(aprsmsg.msg_destination_path, sizeof(aprsmsg.msg_destination_path), RcvBuffer, dst.start, dst.end);    // routing & message to
        aprsPathCopy(aprsmsg.msg_destination_call, sizeof(aprsmsg.msg_destination_call), RcvBuffer, dst.last_start, dst.end);    // message to

        if(!bDestinationEndOk)
        {
            if(bLORADEBUG)
            {
                Serial.printf("APRS decode - Packet discarded, wrong APRS-protocol - bDestinationEndOk (payload_type) missing!\n");

                if(rsize < 255)
                    printAsciiBuffer(RcvBuffer, rsize);
            }

            return 0x00;
        }

        if(CheckGroup(aprsmsg.msg_destination_call) == 0)
        {
            // HN-Bericht (Nachbarschaftsmatrix, Stufe 3): eigenes Frame-Ziel fuer
            // den periodischen HEY-artigen Nachbarschaftsbericht (payload_type
            // '@'). Bewusst NICHT in checkRegexCall()s globaler Liste (dort neben
            // "H"/"HG") -- die Liste gilt auch fuer QUELL-Rufzeichen, und "HN" soll
            // dort kein legales Rufzeichen sein. Nur als ZIEL eines '@'-Frames
            // zulaessig, jeder andere payload_type faellt weiter unten in
            // checkRegexCall() und wird wie bisher abgelehnt.
            if(aprsmsg.payload_type == '@' && strcmp(aprsmsg.msg_destination_call, "HN") == 0)   // strcmp statt is_equ(): diese Datei laeuft auch in Host-Tests ohne loop_functions
            {
                // ok, HN-Bericht
            }
            else if(!checkRegexCall(aprsmsg.msg_destination_call))
            {
                if(bLORADEBUG)
                {
                    Serial.printf("APRS decode - Destination-CallSign Error [%s]\n", aprsmsg.msg_destination_call);
                }
                bCallsignOk=false;
            }
        }

        if(!bCallsignOk)
        {
            return 0x00;
        }

        // Payload
        bool bPayloadEndOk=false;

        char cConcat1[UDP_TX_BUF_SIZE];
        memset(cConcat1, 0x00, UDP_TX_BUF_SIZE);
        int iConcat1=0;

        // R2-04: die Schranke `iConcat1 < (int)sizeof(cConcat1) - 1` ist NEU.
        // Vorher lief diese Schleife allein bis `rsize` und verliess sich
        // darauf, dass jeder Aufrufer hoechstens UDP_TX_BUF_SIZE (255) Bytes
        // hereingibt -- was heute stimmt (R1-06 hat es nachgewiesen: LoRa ueber
        // rxPayloadCopy[2][UDP_TX_BUF_SIZE], BLE ueber ein uint8_t als Laenge,
        // UDP ueber die Ringpuffer-Slots). Der Test darueber auf
        // MAX_APRS_FRAME_SIZE (340) legt allerdings eine GROESSERE Schranke
        // nahe, als die Puffer hier tragen: bei rsize > 255 haette diese
        // Schleife ueber cConcat1[UDP_TX_BUF_SIZE] hinausgeschrieben, mitten in
        // cConcat2/cConcat3 auf demselben Stack. Diese Zeile macht die
        // Sicherheit oertlich, statt sie von den Aufrufern zu borgen.
        // B-APRS-PATH-01: the payload is located as one range (up to the NUL, rsize or sizeof(cConcat1) - 1
        // bytes) and copied once; same bounds as the former byte-by-byte loop.
        {
            const uint16_t pstart = inext;
            const uint16_t pend = aprsPayloadScan(RcvBuffer, pstart, rsize, (uint16_t)(sizeof(cConcat1) - 1), &bPayloadEndOk);
            iConcat1 = (int)(pend - pstart);
            if(iConcat1 > 0)
                memcpy(cConcat1, RcvBuffer + pstart, (size_t)iConcat1);
            if(bPayloadEndOk)
                inext = (uint16_t)(pend + 1);
        }

        // CHR-01: strip C0/C1 controls, invalid/overlong UTF-8 and bidi/
        // zero-width format characters from the RX text before it goes
        // anywhere -- this one site covers both LoRa RX and UDP-from-server
        // RX, since both call decodeAPRS(). PLAIN mode: this payload can
        // also be a position or telemetry frame, whose structural bytes
        // ('/', '{', ':', ...) are printable ASCII and must survive.
        iConcat1 = (int)charset_filter_apply(cConcat1, (size_t)iConcat1, CHARSET_FILTER_PLAIN);
        cConcat1[iConcat1] = 0x00;

        mcSet(aprsmsg.msg_payload, sizeof(aprsmsg.msg_payload), cConcat1);

        if(!bPayloadEndOk)
        {
            if(bLORADEBUG)
            {
                Serial.printf("APRS decode - Packet discarded, wrong APRS-protocol - PayloadEnd (0x00) missing!\n");

                if(rsize < 255)
                    printAsciiBuffer(RcvBuffer, rsize);
            }

            return 0x00;
        }

        // Trailer (hw + mod + 2-byte FCS = 4 bytes) must fully fit within rsize
        if((inext + 4) > rsize)
        {
            if(bLORADEBUG)
            {
                Serial.printf("APRS decode - Packet discarded, wrong APRS-protocol - Trailer (HW/MOD/FCS) truncated!\n");

                if(rsize < 255)
                    printAsciiBuffer(RcvBuffer, rsize);
            }

            return 0x00;
        }

        //Serial.printf("rsize:%i inext:%i HW:%02X MOD:%02X FCS1:%02X FCS2:%02X fcs:%i\n", rsize, inext, RcvBuffer[inext], RcvBuffer[inext+1], RcvBuffer[inext+2], RcvBuffer[inext+3], (unsigned int)(RcvBuffer[inext+2] << 8) | RcvBuffer[inext+3]);

        aprsmsg.msg_source_hw = RcvBuffer[inext];
        inext++;

        aprsmsg.msg_source_mod = RcvBuffer[inext];
        inext++;

        aprsmsg.msg_fcs = (RcvBuffer[inext] << 8) | RcvBuffer[inext+1];

        // Check FCS
        unsigned int FCS_SUMME = aprsFcsSum(RcvBuffer, inext);   // B-APRS-PATH-01: same byte sum, own function

        inext=inext+2;

        if(aprsmsg.msg_fcs != FCS_SUMME)
        {
            // Check ob es nicht das eigene packet ist
            if(strcmp(aprsmsg.msg_source_last, meshcom_settings.node_call) != 0)
            {
                memcpy(temp, RcvBuffer, 10);
                if(bLORADEBUG)
                {
                    Serial.printf("APRS decode - Packet (%i) discarded, wrong FCS <%08X>:<%08X> wrong! <%02X %02X%02X%02X%02X %02X %-60.60s>\n", rsize, aprsmsg.msg_fcs, FCS_SUMME, temp[0], temp[4], temp[3], temp[2], temp[1], temp[5], RcvBuffer+6);

                    if(rsize < 255)
                        printAsciiBuffer(RcvBuffer, rsize);
                }
            }
            
            return 0x00;
        }

        if(inext < rsize)
        {
            aprsmsg.msg_source_fw_version = RcvBuffer[inext];
            aprsmsg.msg_source_fw_on_air = true;
            inext++;
        }

        if(inext < rsize)
        {
            aprsmsg.msg_last_hw = RcvBuffer[inext];
            inext++;
        }

        if(inext < rsize)
        {
            if(RcvBuffer[inext] == 0x7e)
            {
                aprsmsg.msg_source_fw_sub_version = '#';
                inext++;
            }
            else
            {
                if(RcvBuffer[inext] == 0x00)
                    aprsmsg.msg_source_fw_sub_version = '#';
                else
                    aprsmsg.msg_source_fw_sub_version = RcvBuffer[inext];
                inext++;
            }
        }

        if(inext < rsize)
        {
            if(RcvBuffer[inext] == 0x7e)
            {
                inext++;
            }
        }

        aprsmsg.msg_len = inext;


        if(aprsmsg.msg_source_fw_version > 0 && aprsmsg.msg_source_fw_version < 35)  // 4.35a
        {
            Serial.printf("APRS decode - Packet discarded, wrong FW-version <%s><%d>!\n", aprsmsg.msg_source_path,  aprsmsg.msg_source_fw_version);

            return 0x00;
        }


        return aprsmsg.payload_type;
    }
    else
    {
        memcpy(temp, RcvBuffer, 10);
        
        if(bLORADEBUG)
        {
            if(temp[0] != 0x80)
            {
                Serial.printf("APRS decode - Packet discarded, wrong APRS-protocol! <%02X %02X%02X%02X%02X %02X %-60.60s>\n", temp[0], temp[4], temp[3], temp[2], temp[1], temp[5], RcvBuffer+6);
        
                if(rsize < 256)
                    printAsciiBuffer(RcvBuffer, rsize);
            }
        }

        return 0x00;
    }
}

void initAPRSPOS(struct aprsPosition &aprspos)
{
    aprspos.pos_atxt = "";
    aprspos.pos_name = "";

    aprspos.lat = 0.0;
    aprspos.lat_c = 0x00;
    aprspos.lon = 0.0;
    aprspos.lon_c = 0x00;
    aprspos.alt = 0;
    aprspos.bat = 0;
    aprspos.lat_d = 0.0;
    aprspos.lon_d = 0.0;
    aprspos.aprs_group = '/';
    aprspos.aprs_symbol = '&';

    // WX
    aprspos.press = 0.0;
    aprspos.hum = 0.0;
    aprspos.temp = 0.0;
    aprspos.temp2 = 0.0;
    aprspos.qfe = 0;
    aprspos.qnh = 0.0;
    aprspos.gasres = 0.0;
    aprspos.co2 = 0.0;
    aprspos.ncnt = 0;

    aprspos.version = 0;
    aprspos.telemetry = 0;
    aprspos.din[0] = 0x00;

    aprspos.vbus = 0.0;
    aprspos.vcurrent = 0.0;
    for(int igrc=0; igrc<6; igrc++)
        aprspos.grc[igrc] = 0;
    aprspos.grccnt = 0;
}

// DRY campaign D3-01 (docs/optimization-audit-20260910.md:200): 14 of the
// decodeAPRSPOS() '/X=value' tag blocks (Batt/Alt/Press/Hum/Temp/Temp2/
// QFE/QNH/GASRES/CO2/version/Bus-Voltage/Current/telemetry) shared this
// exact loop, byte for byte, differing only in the tag letter, the sscanf
// format and the destination field. This is that loop, extracted verbatim.
//
// Deliberately NOT routed through here (their clamping/offset differs, see
// the audit and the call sites themselves): /N (NCNT, 2-char tag matched by
// a digit range, offset +2, 3-byte cap instead of 7), /R (GRC, its own
// buffer + ';'-split group-list parsing) and /D (Digital, 8-byte fixed-width
// bitfield with its own validity check). Unifying those into "one true
// helper" would have meant either adding parameters nothing else uses or
// quietly changing their behavior -- reported instead of forced.
//
// Quirks preserved on purpose, not fixed:
//  - only the first occurrence of the tag is honored (outer loop breaks on
//    match), so a duplicate tag later in the string is ignored;
//  - a present-but-empty value ("/X=/" or "/X= ") calls sscanf() on an
//    empty string, which performs zero conversions and leaves *target
//    exactly as it was (decodeAPRSPOS()'s initAPRSPOS() default, normally);
//  - a value longer than the 7-byte capture window is silently truncated
//    to its first 7 characters, not rejected (e.g. an 8-digit /A= parses
//    only the leading 7 digits);
//  - the `id == PayloadBuffer.length()` end check can never actually fire
//    (the enclosing `for(id...; id<PayloadBuffer.length(); ...)` already
//    exits before the body runs again at that point) -- moot in practice
//    because decodeAPRSPOS() always concat(" ")s a trailing space onto
//    PayloadBuffer before any of these run, so a value can never truly
//    reach the raw end of the buffer without a delimiter first.
// The value's type, instead of a printf format string.
//
// sscanf() must be reached with a LITERAL format: several envs build with
// -Werror=format-nonliteral (it fired on E22_1262_S3-DevKitC-1-N16R8), and a
// non-literal format also silently disables the compiler's argument-type
// check on the void* target -- which is exactly the check worth keeping when
// the caller hands over an address and a type separately.
//
// APRS_TAG_INT_AUTOBASE is "%i", NOT "%d", and the two are kept apart on
// purpose: "%i" infers the base, so "010" reads as octal 8 and "0x10" as 16.
// Only the version ('V') and telemetry ('Y') tags used it; collapsing them
// into "%d" would silently change what those two fields decode to.
enum AprsTagType
{
    APRS_TAG_INT,          // "%d" -> int *
    APRS_TAG_INT_AUTOBASE, // "%i" -> int *
    APRS_TAG_FLOAT,        // "%f" -> float *
};

// B-APRS-POS-01 (2026-11-26): the tags are located in ONE pass over the payload (aprsPosFindTags) instead of one
// full scan per tag, and each value is read at its known position with a fixed bound (at most 7 characters plus
// the delimiter). Same values as the former per-tag scan, which a bounded model checker had to unroll once per
// tag and payload position (memout at 15 GB for 24-byte payloads).
struct AprsPosTags
{
    int16_t at[26]; // index of the first "/X=" for X = 'A'..'Z', -1 if absent
    int16_t ncnt;   // index of the first "/N" followed by '1'..'9', -1 if absent
};

static void aprsPosFindTags(const char *PayloadBuffer, size_t PayloadLen, unsigned int istarttext, AprsPosTags &tags)
{
    for(int k=0; k<26; k++)
        tags.at[k] = -1;
    tags.ncnt = -1;

    for(unsigned int itxt=istarttext; itxt<PayloadLen; itxt++)
    {
        if(PayloadBuffer[itxt] != '/')
            continue;
        const char c1 = PayloadBuffer[itxt+1];   // the buffer carries 2 bytes of lookahead past PayloadLen
        const char c2 = PayloadBuffer[itxt+2];
        if(c1 >= 'A' && c1 <= 'Z' && c2 == '=' && tags.at[c1 - 'A'] < 0)
            tags.at[c1 - 'A'] = (int16_t)itxt;
        if(c1 == 'N' && c2 >= '1' && c2 <= '9' && tags.ncnt < 0)
            tags.ncnt = (int16_t)itxt;
    }
}

// Value of the tag found at pos ("/X=" then up to 7 characters, ended by '/', ' ' or the 7-character cap) into
// target. Nothing is read when the tag is absent, and nothing is converted when the payload ends before a
// delimiter (the former loop never reached sscanf() then either).
static void aprsExtractTag(const char *PayloadBuffer, size_t PayloadLen, int pos, AprsTagType type, void *target)
{
    if(pos < 0)
        return;
    char decode_text[8];
    memset(decode_text, 0x00, sizeof(decode_text));

    for(unsigned int k=0; k<8; k++)
    {
        const unsigned int id = (unsigned int)pos + 3 + k;
        if(id >= PayloadLen)
            return;
        // ENDE
        if(PayloadBuffer[id] == '/' || PayloadBuffer[id] == ' ' || k > 6)
        {
            switch(type)
            {
                case APRS_TAG_INT:          sscanf(decode_text, "%d", (int *)target);   break;
                case APRS_TAG_INT_AUTOBASE: sscanf(decode_text, "%i", (int *)target);   break;
                case APRS_TAG_FLOAT:        sscanf(decode_text, "%f", (float *)target); break;
            }
            return;
        }
        decode_text[k]=PayloadBuffer[id];
    }
}

// Payload is copied once into a stack buffer (no String churn per packet). Same
// semantics as the String version: a ' ' is appended and bytes past the end read 0.
// APRS-POS-RANGE-01 (2026-11-26): sscanf("%lf") also reads an exponent, "inf" and "nan", so an 11-byte lat/lon field
// such as "1e99" gave a value whose (int)(x / 100.0) at the end of decodeAPRSPOS() is undefined behaviour. A value is
// usable when x / 100 fits an int; 2e11 keeps every 11-digit field without exponent (at most 99999999999), NaN fails
// both comparisons. Found by the bounded model checker (specs/cbmc/aprs/pos_pass.check).
static bool aprsPosRawInRange(double x)
{
    return x > -2.0e11 && x < 2.0e11;
}

uint16_t decodeAPRSPOS(const char *Payload, struct aprsPosition &aprspos)
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

            double raw = 0.0; // aprspos.lat is 0.0 from initAPRSPOS(), sscanf() without a number leaves it so
            sscanf(decode_text, "%lf", &raw);
            if (!aprsPosRawInRange(raw))
                return 0x00;
            aprspos.lat = raw;

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

            double raw = 0.0; // aprspos.lon is 0.0 from initAPRSPOS(), sscanf() without a number leaves it so
            sscanf(decode_text, "%lf", &raw);
            if (!aprsPosRawInRange(raw))
                return 0x00;
            aprspos.lon = raw;
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

    AprsPosTags tags;
    aprsPosFindTags(PayloadBuffer, PayloadLen, (unsigned int)istarttext, tags);

    // check Batt
    aprsExtractTag(PayloadBuffer, PayloadLen, tags.at['B' - 'A'], APRS_TAG_INT, &aprspos.bat);

    // check Altitute
    aprsExtractTag(PayloadBuffer, PayloadLen, tags.at['A' - 'A'], APRS_TAG_INT, &aprspos.alt);

    // check Press
    aprsExtractTag(PayloadBuffer, PayloadLen, tags.at['P' - 'A'], APRS_TAG_FLOAT, &aprspos.press);

    // check Hum
    aprsExtractTag(PayloadBuffer, PayloadLen, tags.at['H' - 'A'], APRS_TAG_FLOAT, &aprspos.hum);

    // check Temp
    aprsExtractTag(PayloadBuffer, PayloadLen, tags.at['T' - 'A'], APRS_TAG_FLOAT, &aprspos.temp);

    // check Temp2
    aprsExtractTag(PayloadBuffer, PayloadLen, tags.at['O' - 'A'], APRS_TAG_FLOAT, &aprspos.temp2);

    // check QFE
    aprsExtractTag(PayloadBuffer, PayloadLen, tags.at['F' - 'A'], APRS_TAG_INT, &aprspos.qfe);

    // check QNH
    aprsExtractTag(PayloadBuffer, PayloadLen, tags.at['Q' - 'A'], APRS_TAG_FLOAT, &aprspos.qnh);

    // check GASRES
    aprsExtractTag(PayloadBuffer, PayloadLen, tags.at['G' - 'A'], APRS_TAG_FLOAT, &aprspos.gasres);

    // check NCNT: "/N" + digit, the value is its first 3 characters up to the next '/' or ' '. The payload always
    // ends with the appended ' ', so a delimiter is always found and the value always converted (the former loop
    // scanned on to that delimiter without copying more).
    memset(decode_text, 0x00, sizeof(decode_text));
    if(tags.ncnt >= 0)
    {
        for(unsigned int k=0; k<3; k++)
        {
            const unsigned int id = (unsigned int)tags.ncnt + 2 + k;
            if(id >= PayloadLen || PayloadBuffer[id] == '/' || PayloadBuffer[id] == ' ')
                break;
            decode_text[k]=PayloadBuffer[id];
        }
        sscanf(decode_text, "%d", &aprspos.ncnt);
    }

    // check CO2
    aprsExtractTag(PayloadBuffer, PayloadLen, tags.at['C' - 'A'], APRS_TAG_FLOAT, &aprspos.co2);

    // check GRC (Group-Call list) /R=; up to 6 groups separated by ';'.
    // Own buffer sized for the worst case (6 x "99999;" = 36 chars) instead
    // of the shared decode_text[25] the other keys use.
    {
        char decode_grc[40];
        memset(decode_grc, 0x00, sizeof(decode_grc));
        int igrc = 0;

        if(tags.at['R' - 'A'] >= 0)
        {
            itxt = (unsigned int)tags.at['R' - 'A'];
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

                        int grcval = CheckGroup((const char *)ctoken);   // same result as the String overload, no temporary

                        if(grcval == 0)
                            break;

                        aprspos.grc[aprspos.grccnt] = grcval;
                        aprspos.grccnt++;

                        istart_tok = ic+1;
                    }
                }
            }
        }
    }

    // check version
    aprsExtractTag(PayloadBuffer, PayloadLen, tags.at['V' - 'A'], APRS_TAG_INT_AUTOBASE, &aprspos.version);

    // check Bus-Voltage /U=
    aprsExtractTag(PayloadBuffer, PayloadLen, tags.at['U' - 'A'], APRS_TAG_FLOAT, &aprspos.vbus);

    // check Current /I=
    aprsExtractTag(PayloadBuffer, PayloadLen, tags.at['I' - 'A'], APRS_TAG_FLOAT, &aprspos.vcurrent);

    // check telemetry
    aprsExtractTag(PayloadBuffer, PayloadLen, tags.at['Y' - 'A'], APRS_TAG_INT_AUTOBASE, &aprspos.telemetry);

    memset(decode_text, 0x00, sizeof(decode_text));
    ipt=0;

    // check Digital /D=: exactly 8 characters of '0'/'1'. A 9th character before the delimiter rejects the token;
    // looking at 9 positions decides that (the former loop scanned on to the delimiter).
    if(tags.at['D' - 'A'] >= 0)
    {
        itxt = (unsigned int)tags.at['D' - 'A'];
        {
            bool din_overflow = false;

            for(unsigned int k=0; k<9; k++)
            {
                const unsigned int id = itxt + 3 + k;
                // ENDE
                if(id >= PayloadLen || PayloadBuffer[id] == '/' || PayloadBuffer[id] == ' ')
                    break;

                if(k < 8)
                {
                    decode_text[k]=PayloadBuffer[id];
                    ipt++;
                }
                else
                {
                    // 9th data byte -- token too long, reject below
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

uint16_t decodeAPRSPOS(String PayloadBuffer, struct aprsPosition &aprspos)
{
    return decodeAPRSPOS(PayloadBuffer.c_str(), aprspos);
}

uint16_t encodeStartAPRS(uint8_t msg_buffer[UDP_TX_BUF_SIZE], struct aprsMessage &aprsmsg)
{
    char msg_start[UDP_TX_BUF_SIZE];

    // :|0x11223344|0x05|OE1KBC|>*:Hallo Mike, ich versuche eine APRS Meldung\0x00

    msg_buffer[0]=aprsmsg.payload_type;
    
    msg_buffer[1]=aprsmsg.msg_id & 0xff;
    msg_buffer[2]=(aprsmsg.msg_id >> 8) & 0xff;
    msg_buffer[3]=(aprsmsg.msg_id >> 16) & 0xff;
    msg_buffer[4]=(aprsmsg.msg_id >> 24) & 0xff;

    msg_buffer[5]=aprsmsg.max_hop & 0x0F;

    if(aprsmsg.msg_server)
        msg_buffer[5] = msg_buffer[5] | 0x80;

    if(aprsmsg.msg_track)
        msg_buffer[5] = msg_buffer[5] | 0x40;

    if(aprsmsg.msg_app_offline)
        msg_buffer[5] = msg_buffer[5] | 0x20;

    if(bMESH)
        msg_buffer[5] = msg_buffer[5] | 0x10;

    snprintf(msg_start, sizeof(msg_start), "%s>%s%c", aprsmsg.msg_source_path, aprsmsg.msg_destination_path, aprsmsg.payload_type);

    uint16_t ilng=strlen(aprsmsg.msg_source_path) + 1 + strlen(aprsmsg.msg_destination_path) + 1;
    
#ifndef UNIT_TEST
    // F3-8 (advisor 2026-11-23): with the live path width the cap below never truncates; a truncated frame would lose
    // the payload-type byte and encodeAPRS would append behind it. Widening MC_PATH_LEN must fail here, not at runtime.
    static_assert(2 * (MC_PATH_LEN - 1) + 2 <= UDP_TX_BUF_SIZE - 6, "MC_PATH_LEN too wide for encodeStartAPRS");
#endif
#ifndef MUT_F3_8
    // F3-8: the copy lands at msg_buffer+6, so the cap is the room behind the 6-byte header
    if(ilng > UDP_TX_BUF_SIZE - 6)
        ilng = UDP_TX_BUF_SIZE - 6;
#else
    if(ilng >= UDP_TX_BUF_SIZE)
        ilng = UDP_TX_BUF_SIZE - 1;
#endif

    memcpy(msg_buffer+6, msg_start, ilng);

    return ilng+6;
}

uint16_t encodePayloadAPRS(uint8_t msg_buffer[MAX_MSG_LEN_PHONE], struct aprsMessage &aprsmsg)
{
    auto ilng = strlen(aprsmsg.msg_payload);
    if(ilng >= UDP_TX_BUF_SIZE)
        ilng = UDP_TX_BUF_SIZE - 1;

    // CHR-01: strip C0/C1 controls, invalid/overlong UTF-8 and bidi/
    // zero-width format characters from the outgoing text before it hits
    // the wire. This single memcpy is the chokepoint for every TX
    // composer (serial, BLE, web, T-Deck, ...), since they all converge on
    // sendMessage() -> encodeAPRS() -> here. PLAIN mode: this payload can
    // also be a position or telemetry frame, whose structural bytes
    // ('/', '{', ':', ...) are printable ASCII and must survive.
    char cFiltered[UDP_TX_BUF_SIZE];
    memcpy(cFiltered, aprsmsg.msg_payload, ilng);
    size_t filtered_len = charset_filter_apply(cFiltered, ilng, CHARSET_FILTER_PLAIN);

    memcpy(msg_buffer, cFiltered, filtered_len);
    return static_cast<uint16_t>(filtered_len);
}

//10:30:29 RX-LoRa: 105 ! xAE48D54D 05 1 0 9V1LH-1,OE1KBC-12>*!0122.64N/10356.52E#/B=005/A=000161/P=1004.9/H=40.2/T=28.9/Q=1005.4/G232;2321 HW:04 MOD:03 FCS:15D5 FW:17 LH:09

uint16_t encodeAPRS(uint8_t msg_buffer[UDP_TX_BUF_SIZE], struct aprsMessage &aprsmsg)
{
    uint16_t inext = encodeStartAPRS(msg_buffer, aprsmsg);

    uint16_t inext_payload = encodePayloadAPRS(msg_buffer+inext, aprsmsg);
        
    if(inext_payload == 0)
        return 0;

    inext = inext + inext_payload;

    // max posible payload (LoRa MSG max 255 byte)
    if((inext + 10) >= UDP_TX_BUF_SIZE)
        inext = UDP_TX_BUF_SIZE - 10;

    msg_buffer[inext] = 0x00;
    inext++;

    msg_buffer[inext] = aprsmsg.msg_source_hw;
    inext++;

    msg_buffer[inext] = aprsmsg.msg_source_mod;
    inext++;

    unsigned int FCS_SUMME=0;

    for(int ifcs=0; ifcs<inext; ifcs++)
    {
        FCS_SUMME += (unsigned int)msg_buffer[ifcs];
    }
    
    // FCS
    msg_buffer[inext] = (FCS_SUMME >> 8) & 0xFF;
    inext++;
    msg_buffer[inext] = FCS_SUMME & 0xFF;
    inext++;

    aprsmsg.msg_fcs = FCS_SUMME;

    msg_buffer[inext] = aprsmsg.msg_source_fw_version;
    inext++;

    msg_buffer[inext] = aprsmsg.msg_last_hw;
    inext++;

    if(aprsmsg.msg_source_fw_sub_version == 0x00)
        msg_buffer[inext] = 0x23;   // #
    else
        msg_buffer[inext] = aprsmsg.msg_source_fw_sub_version;
    inext++;

    msg_buffer[inext] = 0x7e;
    inext++;

    if(inext > UDP_TX_BUF_SIZE)
        inext = UDP_TX_BUF_SIZE;

    aprsmsg.msg_len = inext;

    return inext;
}

// Append the per-hop HEY signal report "NCT,RSSI,SNR;" to a '@' payload.
// NCT = neighbour count as sent on air (nbrNcntAir(), capped at two digits),
// RSSI as positive number, SNR in dB.
// Used by the mesh relay path and the gateway UDP upload (same wire format).
bool appendHeySignalReport(struct aprsMessage &aprsmsg, int16_t rssi, int8_t snr, int ncnt_air)
{
    // Die Kette waechst mit jedem Relais um bis zu HEY_REPORT_GROUP_MAX Zeichen.
    // Regulaer begrenzt MAX_HOP_LIMIT die Zahl der Gruppen, ein von der
    // Luftschnittstelle hereingereichtes '@'-Paket mit ueberlanger Nutzlast aber
    // nicht. Ohne Schranke waechst der re-encodierte Rahmen ueber
    // UDP_TX_BUF_SIZE, wo lora_functions.cpp ihn auf Byteebene kappt -- also
    // mitten in einer Gruppe, die ein Empfaenger nicht mehr parsen kann. Die
    // Kette hier zu beenden ist der verlustaermere Weg: was bereits drinsteht,
    // bleibt gueltig.
    if (strlen(aprsmsg.msg_payload) + HEY_REPORT_GROUP_MAX > HEY_PATH_PAYLOAD_MAX)
        return false;

    // Eine Gruppe, in einem Stueck. Vorher waren das sechs String-Anhaenge mit
    // drei String-Zwischenobjekten; jetzt ein Puffer auf dem Stack und ein
    // Anhang. Die Formate bilden die alten EXAKT ab: String(int) ist "%d",
    // und String(double, 0) ist "%.0f" -- rssi*-1.0 ist als double ganzzahlig,
    // also gibt es hier keine Rundungsfrage.
    char cGroup[HEY_REPORT_GROUP_MAX + 2];
    snprintf(cGroup, sizeof(cGroup), "%d,%.0f,%d;",
             ncnt_air, (double)(rssi * -1.0), (int)snr);
    mcAppend(aprsmsg.msg_payload, sizeof(aprsmsg.msg_payload), cGroup);
    return true;
}

// Haengt "," + call an path, wenn das Ergebnis hoechstens MC_PATH_MAX_CHARS
// (decodierbar) lang ist und in cap passt. Sonst bleibt path unveraendert (kein
// einzelnes Komma) und false kommt zurueck.
bool appendCallToPath(char *path, size_t cap, const char *call)
{
    if (path == NULL || call == NULL || call[0] == '\0')
        return false;

    size_t pl = strnlen(path, cap);
    if (pl >= cap || pl + 1 + strlen(call) > MC_PATH_MAX_CHARS || pl + 1 + strlen(call) + 1 > cap)
        return false;

    mcAppendChar(path, cap, ',');
    mcAppend(path, cap, call);
    return true;
}

// HEY-UP-01: Gateway haengt beim HEY-Upload sein eigenes Rufzeichen an den
// msg_source_path, damit der Server den Empfaenger kennt. Immer Long-Path
// (keine bSHORTPATH-Kuerzung). Haengt nichts an (false), wenn own_call leer
// ist, nicht in MC_PATH_MAX_CHARS passt oder schon Pfadelement ist
// (exakter Elementvergleich ueber ","+call+",", wie die Loop-Erkennung).
bool appendUploaderToPath(struct aprsMessage &aprsmsg, const char *own_call)
{
    if (own_call == NULL || own_call[0] == '\0')
        return false;

    char cSearch[MC_PATH_LEN + 4];
    char cPath[MC_PATH_LEN + 4];
    if (snprintf(cSearch, sizeof(cSearch), ",%s,", own_call) >= (int)sizeof(cSearch))
        return false;
    if (snprintf(cPath, sizeof(cPath), ",%s,", aprsmsg.msg_source_path) >= (int)sizeof(cPath))
        return false;
    if (strstr(cPath, cSearch) != NULL)
        return false;

    return appendCallToPath(aprsmsg.msg_source_path, sizeof(aprsmsg.msg_source_path), own_call);
}

// HEY-Upload-Rahmen: Pfad sichern, Uploader anhaengen, encodieren, Pfad
// wiederherstellen (immer). msg_source_path bleibt danach unveraendert, damit
// Loop-Erkennung und Relay-Append des Aufrufers wie bisher arbeiten.
// Kein Kopieren der ganzen aprsMessage (LoRa-Task-Stack).
uint16_t encodeHeyUpload(uint8_t buf[UDP_TX_BUF_SIZE], struct aprsMessage &aprsmsg, const char *own_call)
{
    char cSaved[MC_PATH_LEN];
    memcpy(cSaved, aprsmsg.msg_source_path, sizeof(cSaved));

    appendUploaderToPath(aprsmsg, own_call);

    memset(buf, 0x00, UDP_TX_BUF_SIZE);
    uint16_t len = encodeAPRS(buf, aprsmsg);

    memcpy(aprsmsg.msg_source_path, cSaved, sizeof(cSaved));
    return len;
}

// OE1KBC-17>APLT00-1,WIDE1-1,qAS,OE3CGG-10:!4807.01N/01619.20E[(T-ECHO by F4AVI)
uint16_t encodeLoRaAPRS(uint8_t msg_buffer[UDP_TX_BUF_SIZE], char cSourceCall[10], double lat, char lat_c, double lon, char lon_c, int alt)
{
    (void)alt;
    char msg_start[UDP_TX_BUF_SIZE];

    uint16_t ilng = 0;

	double slat = 100.0;
    slat = lat*slat;
	double slon = 100.0;
    slon=lon*slon;
	
    double slatr=60.0;
    double slonr=60.0;
    
    slat = (int)lat;
    slatr = (lat - slat) * slatr;
    slat = (slat * 100.) + slatr;
    
    slon = (int)lon;
    slonr = (lon - slon) * slonr;
    slon = (slon * 100.) + slonr;

    if(lon_c != 'W' && lon_c != 'E')
        lon_c = 'E';

    if(lat_c != 'N' && lat_c != 'S')
        lat_c = 'N';


    // Create buffer
    msg_buffer[0]='<';

    msg_buffer[1]=0xFF;
    msg_buffer[2]=0x01;

    // CHR-02: strip APRS structure separators and truncate on a UTF-8
    // boundary at APRS_ATXT_WIRE_MAX bytes (the cap decodeAPRSPOS() applies on
    // receive), so a receiver's byte-counting parser never inherits a split
    // multi-byte sequence.
    char catxt[sizeof(meshcom_settings.node_atxt)];
    charset_wire_field(meshcom_settings.node_atxt, catxt, sizeof(catxt), APRS_ATXT_WIRE_MAX, nullptr);

    snprintf(msg_start, sizeof(msg_start), "%s>APLT00-1,WIDE1-1:!%07.2lf%c%c%08.2lf%c%c%s", cSourceCall, slat, lat_c, meshcom_settings.node_symid, slon, lon_c, meshcom_settings.node_symcd, catxt);

    ilng = strlen(msg_start) + 3;

    if(ilng >= UDP_TX_BUF_SIZE)
        ilng = UDP_TX_BUF_SIZE - 1;

    memcpy(msg_buffer + 3, msg_start, ilng - 3);

    msg_buffer[ilng] = 0x00;

    return ilng;
}

uint16_t encodeLoRaAPRScompressed(uint8_t msg_buffer[UDP_TX_BUF_SIZE], char cSourceCall[10], double lat, char lat_c, double lon, char lon_c, int alt)
{
    (void)alt;
    if(lat == 0.0 or lon == 0.0)
    {
        if(bDisplayCont)
            Serial.println("[APRS] Error encodeLoRaAPRScompressed");
        return 0;
    }

    char msg_start[UDP_TX_BUF_SIZE];

    uint16_t ilng = 0;

    if(lon_c != 'W' && lon_c != 'E')
        lon_c = 'E';

    if(lat_c != 'N' && lat_c != 'S')
        lat_c = 'N';

    double dlat = abs(lat);
    if(lat_c == 'S')
        dlat = dlat * -1.0;

    double dlon = abs(lon);
    if(lon_c == 'W')
        dlon = dlon * -1.0;

    long lgeo = 0;
    uint8_t l1, l2, l3, l4;
    char clat[4];
    char clon[4];

    for(int ig=1;ig<3;ig++)
    {
        if(ig == 1)
        {
            lgeo = 380926.0 * (90.0 - dlat);
            //Serial.printf("dlat %c %lf lgeo %ld ", lat_c, dlat, lgeo);
        }
        else
        {
            lgeo = 190463.0 * (180.0 + dlon);
            //Serial.printf("dlat %c %lf lgeo %ld\n", lon_c, dlon, lgeo);
        }


        l1 = (double)lgeo / 753571.0;
        lgeo = lgeo - (long)(l1 * 753571);

        //Serial.printf("l1 %i > rest %ld ", l1, lgeo);

        l2 = (double)lgeo / 8281.0;
        lgeo = lgeo - (long)(l2 * 8281);

        //Serial.printf("l2 %i > rest %ld ", l2, lgeo);

        l3 = (double)lgeo / 91.0;

        //Serial.printf("l3 %i > rest %ld ", l3, lgeo);

        l4 = lgeo - (long)(l3 * 91);

        //Serial.printf("l4 %i\n", l4);

        if(ig == 1)
        {
            clat[0] = (char)(l1+33);
            clat[1] = (char)(l2+33);
            clat[2] = (char)(l3+33);
            clat[3] = (char)(l4+33);
        }
        else
        {
            clon[0] = (char)(l1+33);
            clon[1] = (char)(l2+33);
            clon[2] = (char)(l3+33);
            clon[3] = (char)(l4+33);
        }
    }


    // Create buffer
    msg_buffer[0]='<';
    
    msg_buffer[1]=0xFF;
    msg_buffer[2]=0x01;

    String strtmp = meshcom_settings.node_atxt;
    strtmp.trim();

    // CHR-02: strip APRS structure separators and truncate on a UTF-8
    // boundary at APRS_ATXT_WIRE_MAX bytes (APRS-TXT-01: was 16, now the same
    // cap as the mesh position; no #name in this frame, D2).
    char catxt[sizeof(meshcom_settings.node_atxt)];
    charset_wire_field(strtmp.c_str(), catxt, sizeof(catxt), APRS_ATXT_WIRE_MAX, nullptr);

    // WIDE1-1 wie in encodeLoRaAPRS()/encodeLoRaAPRSText(): LoRa-APRS-Digipeater
    // wiederholen nur Frames mit diesem Alias (#1174).
    snprintf(msg_start, sizeof(msg_start), "%s>%s,WIDE1-1:!%c%c%c%c%c%c%c%c%c%c P[%s", cSourceCall, meshcom_settings.node_aprsmc, meshcom_settings.node_symid, clat[0], clat[1], clat[2], clat[3], clon[0], clon[1], clon[2], clon[3], meshcom_settings.node_symcd, catxt);

    ilng = strlen(msg_start) + 3;

    if(ilng >= UDP_TX_BUF_SIZE)
        ilng = UDP_TX_BUF_SIZE - 1;

    memcpy(msg_buffer + 3, msg_start, ilng - 3);

    msg_buffer[ilng] = 0x00;

    return ilng;
}

uint16_t encodeLoRaAPRSText(uint8_t msg_buffer[UDP_TX_BUF_SIZE], char cSourceCall[10], char cText[100])
{
    char msg_start[UDP_TX_BUF_SIZE];

    uint16_t ilng = 0;

    // Create buffer
    msg_buffer[0]='<';
    
    msg_buffer[1]=0xFF;
    msg_buffer[2]=0x01;

    /* check for some IGates
    String msgtext="(MeshCom)";
    if(meshcom_settings.node_atxt[0] != 0x00)
        msgtext = meshcom_settings.node_atxt;
    */

    if(memcmp(cText, "T#", 2) == 0)
        snprintf(msg_start, sizeof(msg_start), "%s>APLT00-1,WIDE1-1:%s", cSourceCall, cText);
    else
        snprintf(msg_start, sizeof(msg_start), "%s>APLT00-1,WIDE1-1::%s:%s", cSourceCall, cSourceCall, cText);

    ilng = strlen(msg_start) + 3;

    if(ilng >= UDP_TX_BUF_SIZE)
        ilng = UDP_TX_BUF_SIZE - 1;

    memcpy(msg_buffer + 3, msg_start, ilng - 3);

    msg_buffer[ilng] = 0x00;

    return ilng;
}

double conv_coord_to_dec(double coord)
{
    int ig = 0;
	double dm = 0;

    // GGMM.DDDD -> GG.DDDDDD
    ig = coord / 100.0;
    dm = (coord - (double)(ig * 100)) / 60.0;
    return (double)ig + dm;
}