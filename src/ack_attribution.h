#pragma once

// ACK-Attribution: wer hat quittiert?
//
// Reine Funktionen ohne Globals, ohne String, ohne Heap, damit sie aus dem
// OnRxDone-Kontext (nRF52: LORA-Task) aufrufbar und nativ testbar sind
// (test/test_ack_validate, test/test_ack_phone_frame).
//
// Hintergrund und Entscheidungen: docs/ack-wer-hat-quittiert.md,
// Umsetzung: docs/ack-implementierungsplan.md.
//
// Draht (0x41, gelesen):
//   [0..10]  wie bisher, siehe ack_functions.h
//   [11]     n = Laenge des Anhangs, akzeptiert 0 und 3, alles andere = kein Anhang
//   [12..14] 22-Bit-Node-Hash des Gateways, little endian, nur wenn n == 3
//
// BLE (0x41, erzeugt, Uebergabe an addBLEOutBuffer(), die 4 Byte Zeit anhaengt):
//   [0]      0x41
//   [1..4]   msg_id, little endian
//   [5]      Status 0x00 Node ACK (heard) / 0x01 Gateway bzw. Server / 0x02 Peer ACK
//   [6]      n = Laenge des Anhangs, 0 = altes Format (byteidentisch mit frueher)
//   [7..]    Rufzeichen, n Byte, [A-Z0-9-], n <= ACK_ATTR_CALL_MAX, kein NUL

#include <stdint.h>
#include <string.h>

#define ACK_WIRE_BASE_LEN      12
#define ACK_WIRE_APPENDIX_LEN  3
#define ACK_WIRE_MAX_LEN       (ACK_WIRE_BASE_LEN + ACK_WIRE_APPENDIX_LEN)

#define ACK_PHONE_BASE_LEN     7
#define ACK_ATTR_CALL_MAX      10
#define ACK_PHONE_MAX_LEN      (ACK_PHONE_BASE_LEN + ACK_ATTR_CALL_MAX)

/**
 * @brief Laenge des Draht-Anhangs eines ACK-Frames.
 *
 * Verwirft nie einen Frame: bei jedem Verstoss (n != 3, Puffer zu kurz,
 * NULL) ist die Antwort 0 und der Frame gilt als 12-Byte-ACK.
 *
 * @return 3, wenn Byte 11 == 3 und size >= 15, sonst 0.
 */
static inline uint8_t ackWireAppendixLen(const uint8_t *payload, uint16_t size)
{
    if(payload == NULL)
        return 0;

    if(size < ACK_WIRE_MAX_LEN)
        return 0;

    if(payload[11] != ACK_WIRE_APPENDIX_LEN)
        return 0;

    return ACK_WIRE_APPENDIX_LEN;
}

/**
 * @brief 22-Bit-Node-Hash aus Byte 12..14. Nur gueltig, wenn
 *        ackWireAppendixLen() == 3 geliefert hat.
 */
static inline uint32_t ackWireHash(const uint8_t *payload)
{
    return ((uint32_t)payload[12] | ((uint32_t)payload[13] << 8) | ((uint32_t)payload[14] << 16)) & 0x3FFFFF;
}

/** @brief Zeichensatz des Rufzeichen-Anhangs: [A-Z0-9-]. */
static inline bool ackAttrCallChar(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
}

/**
 * @brief Laenge eines gueltigen Rufzeichen-Anhangs, 0 bei jedem Verstoss.
 *
 * NULL, leer, laenger als ACK_ATTR_CALL_MAX oder ein Zeichen ausserhalb
 * [A-Z0-9-] (auch Kleinbuchstaben) liefern 0: lieber kein Rufzeichen als
 * ein halbes.
 */
static inline uint8_t ackAttrCallLen(const char *call)
{
    if(call == NULL)
        return 0;

    uint8_t n = 0;
    for(; call[n] != '\0'; n++)
    {
        if(n >= ACK_ATTR_CALL_MAX)
            return 0;

        if(!ackAttrCallChar(call[n]))
            return 0;
    }

    return n;
}

/**
 * @brief Baut den BLE-Statusframe (Layout siehe oben).
 *
 * @param out     Zielpuffer, mindestens ACK_PHONE_MAX_LEN Byte.
 * @param msgId   msg_id der quittierten Nachricht.
 * @param status  0x00 / 0x01 / 0x02.
 * @param call    Rufzeichen des Quittierenden, NULL oder "" fuer "unbekannt".
 * @return Laenge fuer addBLEOutBuffer(): 7 + n.
 */
static inline uint16_t buildAckPhoneFrame(uint8_t *out, uint32_t msgId, uint8_t status, const char *call)
{
    uint8_t n = ackAttrCallLen(call);

    out[0] = 0x41;
    out[1] = (uint8_t)(msgId & 0xFF);
    out[2] = (uint8_t)((msgId >> 8) & 0xFF);
    out[3] = (uint8_t)((msgId >> 16) & 0xFF);
    out[4] = (uint8_t)((msgId >> 24) & 0xFF);
    out[5] = status;
    out[6] = n;

    if(n > 0)
        memcpy(out + ACK_PHONE_BASE_LEN, call, n);

    return (uint16_t)(ACK_PHONE_BASE_LEN + n);
}
