#ifndef __UI_EPD47H__
#define __UI_EPD47H__

/*********************************************************************************
 *                                  INCLUDES
 * *******************************************************************************/
#include "lvgl.h"
#include "t5epaper_main.h"

/*********************************************************************************
 *                                   DEFINES
 * *******************************************************************************/
#define LCD_HOR_SIZE LV_HOR_RES
#define LCD_VER_SIZE LV_VER_RES

#define UI_SLIDING_DISTANCE     100

#define EPD_COLOR_BG          0xffffff
#define EPD_COLOR_FG          0x000000
#define EPD_COLOR_FOCUS_ON    0x91B821
#define EPD_COLOR_TEXT        0x000000
#define EPD_COLOR_BORDER      0xBBBBBB
#define EPD_COLOR_PROMPT_BG   0x1e1e00
#define EPD_COLOR_PROMPT_TXT  0xfffee6

/*********************************************************************************
 *                                   MACROS
 * *******************************************************************************/

/*********************************************************************************
 *                                  TYPEDEFS
 * *******************************************************************************/
enum {
    SCREEN0_ID = 0,
    SCREEN1_ID,
    SCREEN2_ID,
    SCREEN2_1_ID,
    SCREEN2_2_ID,
    SCREEN3_ID,
    SCREEN4_ID,
    SCREEN4_1_ID,
    SCREEN4_2_ID,
    SCREEN5_ID,
    SCREEN6_ID,
    SCREEN7_ID,
    SCREEN8_ID,
    SCREEN9_ID,
    SCREEN10_ID,
};

enum {
    TASKBAR_ID_TIME_HOUR = 0,
    TASKBAR_ID_TIME_MINUTE,
    TASKBAR_ID_CHARGE,
    TASKBAR_ID_CHARGE_FINISH,
    TASKBAR_ID_BATTERY_PERCENT,
    TASKBAR_ID_WIFI,
    TASKBAR_ID_MAX,
};

typedef void (*ui_indev_read_cb)(int);

struct menu_icon {
    const void *icon_src;
    const char *icon_str;
    lv_coord_t offs_x;
    lv_coord_t offs_y;
};

enum{
    UI_SETTING_TYPE_SW,
    UI_SETTING_TYPE_SUB,
};

typedef struct _ui_setting
{
    const char *name;
    int type;
    void (*set_cb)(int);
    const char* (*get_cb)(int *);
    int sub_id;
    lv_obj_t *obj;
    lv_obj_t *st;
} ui_setting_handle;

/*********************************************************************************
 *                              GLOBAL PROTOTYPES
 * *******************************************************************************/
void ui_entry(void);

#endif /* __UI_EPD47H__ */