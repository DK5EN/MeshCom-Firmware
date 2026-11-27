#include "web_oled_shot.h"

#if OLED_SHOT_WEB
#include <Arduino.h>
#include "web_rm_util.h"   // web_client (CommonWebClient)
#include "../oled_shot.h"
#include "web_io.h"

void sub_screen_oled(void)
{
    const uint8_t *buf = NULL;
    size_t len = 0;
    int w = 0, h = 0;
    const int st = oledShotSource(&buf, &len, &w, &h);
    if (st != 0)
    {
        const char *why = (st == 1) ? "no-display" : (st == 2) ? "display-off" : "not-full-frame";
        char extra[40];
        snprintf(extra, sizeof(extra), "X-Reason: %s\r\n", why);
        webSendFixedHead(503, "text/plain", strlen(why), extra, "no-store");
        web_client.print(why);
        return;
    }
    char extra[48];
    snprintf(extra, sizeof(extra), "X-Screen-Format: oled1-%dx%d\r\n", w, h);
    webSendFixedHead(200, "application/octet-stream", len, extra, "no-store");
    web_client.write(buf, len);
}
#endif
