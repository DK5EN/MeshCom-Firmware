#if !defined(NRF52_SERIES) || HAS_NRF_ETH   // web GUI is reachable over Ethernet only on nRF52
// Request body reader glue, see web_io.h. The pure part (length checks, buffer, final verdict) is web_body.h.
#include <Arduino.h>

#include "web_io.h"
#include "web_rm_util.h" // extern CommonWebClient web_client
#include "../watchdog_feed.h"

/**
 * Reads the request BODY (the head is read by work_webpage()). Bounded by the Content-Length the caller
 * picked out of the head and by cap, given up on after idle_ms without a byte (a lying Content-Length
 * cannot pin the loop) and after total_ms absolute (a client dripping one byte per ~2 s would otherwise
 * pin the loop task past the 5 s ESP32 task WDT; the loop feeds the WDT, so the deadlines are the bound).
 *
 * @return bytes read into buf (NUL-terminated), or WB_NOLEN / WB_SIZE / WB_SHORT (a stalled body: B10, the
 *         callers answer 408).
 */
int webReadBody(char *buf, size_t cap, long content_length, uint32_t idle_ms, uint32_t total_ms)
{
    WebBodyAcc a;
    const int8_t b = webBodyBegin(&a, buf, cap, content_length);
    if (b < 0)
        return b;

    unsigned long last = millis();
    const unsigned long start = last;

    while ((int32_t)a.got < a.want && (millis() - last) <= idle_ms && (millis() - start) <= total_ms)
    {
        yield();
        meshcom_wdt_feed();

        if (web_client.available())
        {
            int c = web_client.read();
            if (c < 0)
                break;

            webBodyPush(&a, (char)c);
            last = millis();
        }
        else if (!web_client.connected())
        {
            break;
        }
    }

    const int8_t e = webBodyEnd(&a);
    return e == 0 ? (int)a.got : e;
}

/**
 * Head of every fixed-length response (web_io.h): one writer, one line ending, one reason-phrase table.
 */
void webSendFixedHead(uint16_t status, const char *type, size_t len, const char *extra, const char *cache)
{
    web_client.printf("HTTP/1.1 %u %s\r\n", (unsigned)status, webStatusText(status));
    if (type)
    {
        web_client.print("Content-type:");
        web_client.print(type);
        web_client.print("\r\n");
    }
    if (extra)
        web_client.print(extra);
    web_client.printf("Content-Length: %u\r\n", (unsigned)len);
    if (cache)
    {
        web_client.print("Cache-Control: ");
        web_client.print(cache);
        web_client.print("\r\n");
    }
    web_client.print("Connection: close\r\n\r\n");
}
#endif
