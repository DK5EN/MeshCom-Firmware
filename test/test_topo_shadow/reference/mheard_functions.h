// Referenzkopie (MeshCom 5, Welle 4): src/mheard_functions.h aus 313e619a,
// dem letzten Stand vor dem Umstieg auf die Topologie. Seit Welle 4 gibt es
// MHeard und die Pfadtabelle in der Firmware nicht mehr; die Kopie lebt nur
// noch fuer test/test_topo_shadow, das die Topologie gegen genau dieses
// Verhalten vergleicht. Unveraendert bis auf diesen Kopf und die Dinge,
// die mit MHeard aus src/ verschwinden und hier deshalb selbst stehen:
// struct mheardLine mit MC_DATE_LEN/MC_TIME_LEN (vorher aprs_structures.h) und
// MAX_MHEARD/MAX_MHPATH (vorher configuration_global.h, Werte des
// S3-/RAK-Profils, das test/support/configuration.h fuer native Builds
// festlegt).
#ifndef _MHEARD_FUNCTIONS_H_
#define _MHEARD_FUNCTIONS_H_

#include <Arduino.h>
#include <configuration.h>
#include <aprs_structures.h>

#ifndef MAX_MHEARD
#define MAX_MHEARD 80                      // max count of messages in mheard ringbuffer (S3-/RAK-Profil)
#endif
#ifndef MAX_MHPATH
#define MAX_MHPATH 100                     // max count of messages in mhpath ringbuffer (S3-/RAK-Profil)
#endif

// mh_date/mh_time: fester "YYYY-MM-DD"/"HH:MM:SS"-Vertrag von
// mheardFormatDate()/mheardFormatTime() (mheard_record.h).
#ifndef MC_DATE_LEN
#define MC_DATE_LEN     11     // "YYYY-MM-DD" + NUL
#endif
#ifndef MC_TIME_LEN
#define MC_TIME_LEN     9      // "HH:MM:SS" + NUL
#endif

struct mheardLine
{
    char mh_callsign[MC_CALL_LEN_Z];
    char mh_date[MC_DATE_LEN];
    char mh_time[MC_TIME_LEN];
    char mh_sourcecallsign[MC_CALL_LEN_Z];
    char mh_sourcepath[MC_PATH_LEN];
    char mh_destinationpath[MC_PATH_LEN];
    char mh_path_payload[MC_PAYLOAD_LEN];
    char mh_payload_type;
    uint8_t mh_hw;
    uint8_t mh_mod;
    int16_t mh_rssi;
    int8_t mh_snr;
    double mh_dist;
    uint8_t mh_path_len;
    uint8_t mh_mesh;
    uint8_t mh_ncount;
};

void initMheard();
void initMheardLine(struct mheardLine &mheardLine);
void updateMheard(struct mheardLine &mheardLine, uint8_t isPhoneReady);
void updateHeyPath(struct mheardLine &mheardLine);
// R2-01: war decodeMHeard(), das eine pipe-getrennte Zeichenkette
// zeichenweise zerlegte. Jetzt Feldkopien aus dem Datensatz.
struct MheardRecord;
void mheardLineFromRecord(const MheardRecord &rec, struct mheardLine &mheardLine);
void mheardRecordFromLine(const struct mheardLine &mheardLine, MheardRecord &rec);
void showMHeard();
void showPath();
void sendMheard();

// DR-28 (BACKLOG OPT-D16, decided 2026-09-12): fills idx[] with the
// occupied mHeard slots, most recently heard first. Returns the number of
// entries written to idx[]. The slot-parallel storage arrays themselves
// (mheardRecords, mheardCalls, mheardLat/Lon/Alt, mheardEpoch, mheardMillis,
// mheardNCount -- all written by updateMheard() from the LORA task) are
// NEVER reordered; this is a read-only view over them for the renderers
// (showMHeard()/sendMheard()/showMHeardTDECK(), sub_page_mheard() in
// web_functions.cpp). `now` is the caller's millis() snapshot, so every
// renderer sorts against the same instant it also uses for its own
// aging/freshness check.
uint8_t mheardSortedIndex(uint8_t *idx, uint32_t now);
void startMheardToPhone();
bool mheardToPhonePending();
#if defined(BOARD_T_DECK) || defined(BOARD_T_DECK_PLUS)
void showMHeardTDECK();
void showPathTDECK();
#endif

void saveMHeardPersistence();
void loadMHeardPersistence();
void savePathPersistence();
void loadPathPersistence();

unsigned long getLatestMHeardTimestamp();

String getHardwareLong(uint8_t hwid);
char* getPayloadType(char ptype);
int getMheardCount();

// NC-02 (BACKLOG SS3.8o): monotonic freshness checks, mirroring NC-01's
// mheardMillis[]/mheardPathMillis[] aging (mheard_functions.cpp). Callers
// outside mheard_functions.cpp (via_functions.cpp, web_functions.cpp) use
// these instead of externing mheardMillis[]/mheardPathMillis[] and
// comparing mheardEpoch[]/mheardPathEpoch[] against getUnixClock(), which
// wraps to "always stale" on a node with no valid wall clock. iset out of
// range returns false (stale), never reads out of bounds.
bool mheardFreshMs(int iset, uint32_t window_ms);
bool mheardPathFreshMs(int iset, uint32_t window_ms);

#endif