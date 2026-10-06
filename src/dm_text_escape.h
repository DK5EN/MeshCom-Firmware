#ifndef _DM_TEXT_ESCAPE_H_
#define _DM_TEXT_ESCAPE_H_

// P15: Arduino-freier Kern der Klammer-Escape-Ausnahme fuer sendMessage()
// (nativ testbar, test/test_dm_text_escape). sendMessage() ersetzt bei einer
// DM jedes '{' im Text durch '(' (Kommentar dort: "A '{' inside the user
// text breaks the receiver's NNN parse"). {ping} und {SET} sind aber keine
// Fliesstext-Nachrichten, sondern eigene Tags:
//   * ein {ping} wuerde zu (ping}{NNN -- kein Ping mehr, die Gegenstelle
//     antwortet nie mit {pong}, und nichts stoppt eine Wiederholung.
//   * ein {SET}n;m; wuerde zu (SET}n;m; -- das Remote-Hop-Limit-Kommando
//     (sendDisplayText(), startsWith("{SET}")) feuert nie.
// dmTextEscapeFrom() liefert den Index, AB DEM escaped werden muss: 0 im
// Normalfall (unveraendertes Verhalten), sonst die Laenge des erkannten
// Tags -- ein '{' NACH dem Tag bricht weiterhin den NNN-Parse des
// Empfaengers (aprsmsg.msg_payload.indexOf("{", 1)) und wird wie bisher
// escaped.
//
// Nur ein EXAKTES fuehrendes Tag zaehlt ("{pingx"/"{SETX" treffen nicht --
// strncmp() vergleicht das schliessende '}' mit).

#include <stddef.h>
#include <string.h>

inline size_t dmTextEscapeFrom(const char *text)
{
    if(text == NULL)
        return 0;

    static const char PING_TAG[] = "{ping}";
    static const char SET_TAG[]  = "{SET}";
    const size_t pingLen = sizeof(PING_TAG) - 1;
    const size_t setLen  = sizeof(SET_TAG) - 1;

    if(strncmp(text, PING_TAG, pingLen) == 0)
        return pingLen;

    if(strncmp(text, SET_TAG, setLen) == 0)
        return setLen;

    return 0;
}

// W0c (McApp ask 2): ein Remote-Management-Frame ("RM1 <ctr> <cmd> [args] <tag>",
// Kommando oder Antwort) geht genau einmal in die Luft: ohne "{NNN"-Suffix (der
// Empfaenger sendet dann keinen DM-ACK) und ohne Wiederholungsleiter. Der
// Frame traegt seinen eigenen Zaehler und HMAC; ein ACK/Retry wuerde nur
// Airtime fressen und beim Empfaenger ein Replay-Fenster aufmachen.
// Nur das EXAKTE fuehrende "RM1 " (4 Byte inkl. Leerzeichen) zaehlt: "RM10",
// "rm1 ", " RM1 ", "xRM1 ", "RM1" ohne Leerzeichen und ein spaeteres "RM1 "
// im Text treffen nicht. Bekannte Folge: eine von Hand getippte Chat-DM, die
// mit "RM1 " beginnt, bekommt ebenfalls weder ACK noch Retry (akzeptiert).
inline bool dmTextIsRm1Frame(const char *text)
{
    return text != NULL && strncmp(text, "RM1 ", 4) == 0;
}

// Eine Stelle fuer "diese DM wird nie wiederholt" (sendMessage(): Ring-Status
// 0xFF): {CET}/{MCP}/{SET} (Zeit-/Fernwirk-/Hop-Tags, kein Fliesstext) und
// RM1-Frames. Unveraendert: {ping} hat seinen eigenen Pfad (bUseOnce). Der
// {NNN-Suffix ist eine getrennte Entscheidung: nur RM1 entfaellt (dmTextIsRm1Frame),
// {CET}/{SET} behalten ihn wie bisher, {MCP} ist nie eine DM.
// isDM: die RM1-Ausnahme gilt nur fuer Direktnachrichten; ein Gruppentext, der
// zufaellig mit "RM1 " beginnt, wird wie jeder Gruppentext wiederholt.
inline bool dmTextNoRetransmit(const char *text, bool isDM)
{
    if(text == NULL)
        return false;

    return strncmp(text, "{CET}", 5) == 0 ||
           strncmp(text, "{MCP}", 5) == 0 ||
           strncmp(text, "{SET}", 5) == 0 ||
           (isDM && dmTextIsRm1Frame(text));
}

#endif // _DM_TEXT_ESCAPE_H_
