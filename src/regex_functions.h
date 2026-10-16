#ifndef _REGEX_FUNCTIONS_H_
#define _REGEX_FUNCTIONS_H_

#include <Arduino.h>
#include <configuration.h>
#include <debugconf.h>

bool checkRegexCall(String strCallSign);

// Prueft die Via-Liste des Knotens (--via): kommagetrennte Hop-Rufzeichen,
// zusammen hoechstens 39 Zeichen (node_via ist char[40]). Leere Liste, leere
// Eintraege, SSID ausserhalb 0..99 oder mit fuehrender Null und die
// Sondermarken "*", "H", "HG" werden abgelehnt.
bool checkViaList(const String &list);

// Bringt das EIGENE Rufzeichen des Knotens auf die kanonische Form.
// Ausdruecklich nicht fuer empfangene Rufzeichen: checkRegexCall() prueft auch
// die Absender fremder Frames, und wuerde deren Call hier umgeschrieben, waeren
// Dedup, ACK-Zuordnung und der Via-Pfad-Vergleich nicht mehr deckungsgleich mit
// dem, was auf der Luft steht.
//
// Liefert false, wenn die kanonische Form nicht darstellbar ist (mehr als neun
// Zeichen) -- der Aufrufer weist das Rufzeichen dann zurueck, statt es
// abgeschnitten zu speichern. Sonst true, auch wenn nichts geaendert wurde.
bool normalizeOwnCall(String &callsign);

#endif
