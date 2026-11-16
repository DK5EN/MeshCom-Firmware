// GPS-SKY W3: sky plot view for the Position tab of the T-Deck and T-Deck Plus (not the T-Deck Pro).
// Sits over the text view (track_ta); a toggle button switches between the two. Data comes from the shared
// GSV core in src/gps_skyplot.h, fed from WZ_GPS_Feed. All calls must run in the ESP32 loop task.
#ifndef TDECK_GPS_SKY_H
#define TDECK_GPS_SKY_H

#if defined(BOARD_T_DECK) || defined(BOARD_T_DECK_PLUS)

#include <lvgl.h>

// Builds the sky card over the area of text_view, the SKY / TEXT side tabs attached to the card and the SAT/AGE
// label in the right column of tab (geometry: tdeck_gps_sky_layout.h). Default view after boot: sky plot.
void tdeckGpsSkyCreate(lv_obj_t *tab, lv_obj_t *text_view);
// Redraws the plot when the satellites changed; no-op while the text view is shown.
void tdeckGpsSkyRefresh();
// Shows txt in the info label for 2 s when the sky view is visible (the text view has its own message).
void tdeckGpsSkyNotice(const char *txt);

#endif
#endif
