#include "web_oled_shot.h"

#if OLED_SHOT_WEB
#include <Arduino.h>
#include "web_rm_util.h"   // web_client (CommonWebClient)
#include "../oled_shot.h"

void sub_screen_oled(void)
{
    const uint8_t *buf = NULL;
    size_t len = 0;
    int w = 0, h = 0;
    const int st = oledShotSource(&buf, &len, &w, &h);
    if (st != 0)
    {
        const char *why = (st == 1) ? "no-display" : (st == 2) ? "display-off" : "not-full-frame";
        web_client.println("HTTP/1.1 503 Service Unavailable");
        web_client.println("Content-type:text/plain");
        web_client.print("X-Reason: ");
        web_client.println(why);
        web_client.print("Content-Length: ");
        web_client.println((unsigned)strlen(why));
        web_client.println("Cache-Control: no-store");
        web_client.println("Connection: close");
        web_client.println();
        web_client.print(why);
        return;
    }
    web_client.println("HTTP/1.1 200 OK");
    web_client.println("Content-type:application/octet-stream");
    web_client.printf("X-Screen-Format: oled1-%dx%d\r\n", w, h);
    web_client.print("Content-Length: ");
    web_client.println((unsigned)len);
    web_client.println("Cache-Control: no-store");
    web_client.println("Connection: close");
    web_client.println();
    web_client.write(buf, len);
}
#endif
