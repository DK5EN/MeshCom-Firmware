#ifndef _BLE_JSON_FRAME_H_
#define _BLE_JSON_FRAME_H_

#include <stddef.h>
#include <stdint.h>
#include <ArduinoJson.h>

// Serialisiert ein JSON-Dokument in einen BLE-Rahmenpuffer: buf[0] ist das
// Typbyte (z.B. 0x44 'D'), das JSON beginnt bei buf+1.
//
// Die Schranke ist die Puffergroesse, nicht die gemessene JSON-Laenge:
// serializeJson() schreibt hoechstens bufsize-1 Bytes und liefert die
// tatsaechlich geschriebene Anzahl. Mit measureJson()+1 als Schranke waere ein
// Dokument, das laenger ist als der Puffer, ein Stack-Ueberlauf (BND-03).
//
// Rueckgabe: Rahmenlaenge (Typbyte + JSON), nie groesser als bufsize. Ein
// abgeschnittenes Dokument ist kein gueltiges JSON mehr - das ist dieselbe
// Eigenschaft, die addBLEComToOutBuffer() beim Klemmen auf 245 Byte ohnehin
// hat, aber ohne Speicherverletzung.
static inline uint16_t bleJsonFrame(const JsonDocument &doc, uint8_t *buf, size_t bufsize)
{
    if (buf == nullptr || bufsize < 2)
        return 0;
    size_t json_len = serializeJson(doc, (char *)buf + 1, bufsize - 1);
    return (uint16_t)(json_len + 1);
}

#endif
