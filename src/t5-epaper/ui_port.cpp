#include "ui.h"
#include "Arduino.h"
#include "t5epaper_main.h"
#include "utilities.h"
#include "peripheral.h"
#include "epdiy.h"
#include "ui_port.h"
#include "nvs_param.h"

int ui_setting_backlight = 3;  // 0 - 3
int epd_vcom_default = 1560;
int refresh_mode = UI_REFRESH_MODE_NORMAL;

//************************************[ Other fun ]******************************************

void ui_nvs_set_defaulat_param(void)
{
    nsv_param_init();
    ui_setting_backlight = nvs_param_get_u8(NVS_ID_BACKLIGHT);
    epd_vcom_default = nvs_param_get_u16(NVS_ID_EPD_VCOM);
    refresh_mode = nvs_param_get_u8(NVS_ID_REFRESH_MODE);

    printf("ui_setting_backlight = %d\n", ui_setting_backlight );
    printf("epd_vcom_default = %d\n", epd_vcom_default );
    printf("refresh_mode = %d\n", refresh_mode );

    ui_setting_set_backlight(ui_setting_backlight);
}

void ui_indev_touch_en(void)
{
    indev_touch_en();
}

void ui_indev_touch_dis(void)
{
    indev_touch_dis();
}

void ui_refresh_set_mode(int mode)
{
    mode = mode < UI_REFRESH_MODE_FAST ? UI_REFRESH_MODE_FAST : mode;
    mode = mode > UI_REFRESH_MODE_NEAT ? UI_REFRESH_MODE_NEAT : mode;
    refresh_mode = mode;
}

int ui_refresh_get_mode()
{
    return refresh_mode;
}

void ui_full_refresh(void)
{
    disp_full_refresh();
}

void ui_full_clean(void)
{
    disp_full_clean();
}

void ui_epd_clean(void)
{
    disp_clean();
}

void ui_set_rotation(lv_disp_rot_t rot)
{
    // EPD_ROT_LANDSCAPE = 0,
    // EPD_ROT_PORTRAIT = 1,
    // EPD_ROT_INVERTED_LANDSCAPE = 2,
    // EPD_ROT_INVERTED_PORTRAIT = 3,
    switch (rot)
    {
        case LV_DISP_ROT_NONE: epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);  break;
        case LV_DISP_ROT_90:   epd_set_rotation(EPD_ROT_INVERTED_LANDSCAPE); break;
        case LV_DISP_ROT_180:  epd_set_rotation(EPD_ROT_PORTRAIT);           break;
        case LV_DISP_ROT_270:  epd_set_rotation(EPD_ROT_LANDSCAPE);          break;
        default:
            break;
    }
    // LV_DISP_ROT_NONE = 0,
    // LV_DISP_ROT_90,
    // LV_DISP_ROT_180,
    // LV_DISP_ROT_270
    lv_disp_set_rotation(lv_disp_get_default(), rot);
}

//************************************[ screen 1 ]****************************************** clock
void ui_clock_get_time(uint8_t *h, uint8_t *m, uint8_t *s)
{
    // *h = timeinfo.tm_hour;
    // *m = timeinfo.tm_min;
    // *s = timeinfo.tm_sec;

    if(peri_buf[E_PERI_RTC] == true)
    {
        RTC_DateTime datetime = rtc.getDateTime();
        *h = datetime.hour;
        *m = datetime.minute;
        *s = datetime.second;

    } else
    {
        static int test_m = 19;
        *h = 10;
        *m = test_m++;
        *s = 0;
    }

    //KBC printf("h=%d, m=%d, s=%d\n", *h, *m, *s);
}

void ui_clock_get_data(uint8_t *year, uint8_t *month, uint8_t *day, uint8_t *week)
{
    // *year = timeinfo.tm_year % 100;
    // *month = timeinfo.tm_mon+1;
    // *day = timeinfo.tm_mday;
    // *week = timeinfo.tm_wday;

    if(peri_buf[E_PERI_RTC] == true)
    {
        RTC_DateTime datetime = rtc.getDateTime();
        *year = datetime.year % 100;
        *month = datetime.month;
        *day = datetime.day;
        *week = datetime.week;
    } else {
        static int test_data = 18;
        *year = 24;
        *month = 12;
        *day = test_data++;
        *week = 2;
    }
    printf("y=%d, m=%d, d=%d, w=%d\n", *year, *month, *day, *week);
}
//************************************[ screen 2 ]****************************************** lora
void ui_lora_recv_suspend(void)
{
    lora_recv_suspend();
}

void ui_lora_recv_resume(void)
{
    lora_recv_resume();
}

void ui_lora_set_mode(int mode)
{
    lora_set_mode(mode);
}
int ui_lora_get_mode(void)
{
    return lora_get_mode();
}
void ui_lora_send(const char *str)
{
    lora_transmit(str);
}
bool ui_lora_recv(const char **str, int *rssi)
{
    return lora_get_recv(str, rssi);
}
void ui_lora_clean_recv_flag(void)
{
    lora_set_recv_flag();
}

float ui_lora_get_freq(void)            { return LORA_FREQUENNCY; }
float ui_lora_get_bandwidth(void)       { return LORA_BANDWIDTH; }
int16_t ui_lora_get_output_power(void)  { return LORA_OUTPUT_POWER; }
uint8_t ui_lora_get_spread_factor(void) { return LORA_SPREAD_FACTOR; }
//************************************[ screen 3 ]****************************************** sd_card
void ui_sd_read(void)
{

}

void ui_sd_get_capacity(uint64_t *total, uint64_t *used)
{
    int ret = 0;
    ui_test_get_sd(&ret);
    if(ret)
    {
        if(total)
            *total = SD.totalBytes() / (1024 * 1024);
        if(used)
            *used = SD.usedBytes() / (1024 * 1024);

        printf("total=%lluMB, used=%lluMB\n", *total, *used);

        uint64_t cardSize = SD.cardSize() / (1024 * 1024);
        Serial.printf("SD Card Size: %lluMB\n", cardSize);

        uint64_t totalSize = SD.totalBytes() / (1024 * 1024);
        Serial.printf("SD Card Total: %lluMB\n", totalSize);

        uint64_t usedSize = SD.usedBytes() / (1024 * 1024);
        Serial.printf("SD Card Used: %lluMB\n", usedSize);
    }
}
//************************************[ screen 4 ]****************************************** setting
#if 1

int ui_setting_get_vcom(void)
{
    return epd_vcom_default;
}

void ui_setting_set_vcom(int v)
{
    v = v > 5000 ? 5000 : v;
    v = v < 200 ? 200 : v;
    epd_vcom_default = v;
    epd_set_vcom(v); // TPS651851 VCOM output range 0-5.1v  step:10mV
    nvs_param_set_u16(NVS_ID_EPD_VCOM, epd_vcom_default);
}

void ui_setting_set_backlight(int bl)
{
    switch (bl)
    {
        case 0: analogWrite(BOARD_BL_EN, 0); break;
        case 1: analogWrite(BOARD_BL_EN, 50); break;
        case 2: analogWrite(BOARD_BL_EN, 100); break;
        case 3: analogWrite(BOARD_BL_EN, 230); break;
        default:
            break;
    }
    ui_setting_backlight = bl;
}

void ui_setting_set_backlight_level(int level)
{
    level++;    
    level &= 0x03;
    ui_setting_set_backlight(level);
    nvs_param_set_u8(NVS_ID_BACKLIGHT, ui_setting_backlight);
}

const char *ui_setting_get_backlight(int *ret_bl)
{
    const char *ret = NULL;
    switch (ui_setting_backlight)
    {  
        case 0: ret = "OFF"; break;
        case 1: ret = "Low"; break;
        case 2: ret = "Medium"; break;
        case 3: ret = "High"; break;   
        default:
            break;
    }

    if(ret_bl) *ret_bl = ui_setting_backlight;

    return ret;
}

void ui_setting_set_refresh_speed(int bl)
{
    switch (refresh_mode)
    {
        case UI_REFRESH_MODE_FAST:   refresh_mode = UI_REFRESH_MODE_NORMAL;   break;
        case UI_REFRESH_MODE_NORMAL: refresh_mode = UI_REFRESH_MODE_NEAT;     break;
        case UI_REFRESH_MODE_NEAT:   refresh_mode = UI_REFRESH_MODE_FAST;     break;
        default:
            break;
    }
    nvs_param_set_u8(NVS_ID_REFRESH_MODE, refresh_mode);
}

const char *ui_setting_get_refresh_speed(int *ret_bl)
{
    const char *ret = NULL;
    switch (refresh_mode)
    {  
        case UI_REFRESH_MODE_FAST:   ret = "Fast";   break;
        case UI_REFRESH_MODE_NORMAL: ret = "Normal"; break;
        case UI_REFRESH_MODE_NEAT:   ret = "Neat";   break;
        default:
            break;
    }

    if(ret_bl) *ret_bl = refresh_mode;

    return ret;
}

const char *ui_setting_get_sf_ver(void)
{
    return UI_T5_EPARPER_S3_PRO_VERSION;
}
const char *ui_setting_get_hd_ver(void)
{
    return BOARD_T5_EPARPER_S3_PRO_VERSION;
}

#endif
//************************************[ screen 5 ]****************************************** test
const char *ui_test_get_gps(int *ret_n)
{
    if(ret_n) *ret_n = peri_buf[E_PERI_GPS];
    return ((peri_buf[E_PERI_GPS] ==  true) ? "PASS" : "---");
}
const char *ui_test_get_lora(int *ret_n)
{
    if(ret_n) *ret_n = peri_buf[E_PERI_LORA];
    return ((peri_buf[E_PERI_LORA] ==  true) ? "PASS" : "---");
}
const char *ui_test_get_sd(int *ret_n)
{
    if(ret_n) *ret_n = peri_buf[E_PERI_SD_CARD];
    return ((peri_buf[E_PERI_SD_CARD] ==  true) ? "PASS" : "---");
}
const char *ui_test_get_rtc(int *ret_n)
{
    if(ret_n) *ret_n = peri_buf[E_PERI_RTC];
    return ((peri_buf[E_PERI_RTC] ==  true) ? "PASS" : "---");
}
const char *ui_test_get_touch(int *ret_n)
{
    if(ret_n) *ret_n = peri_buf[E_PERI_TOUCH];
    return ((peri_buf[E_PERI_TOUCH] ==  true) ? "PASS" : "---");
}
const char *ui_test_get_BQ25896(int *ret_n)
{
    if(ret_n) *ret_n = peri_buf[E_PERI_BQ25896];
    return ((peri_buf[E_PERI_BQ25896] ==  true) ? "PASS" : "---");
}
const char *ui_test_get_BQ27220(int *ret_n)
{
    if(ret_n) *ret_n = peri_buf[E_PERI_BQ27220];
    return ((peri_buf[E_PERI_BQ27220] ==  true) ? "PASS" : "---");
}

//************************************[ screen 6 ]****************************************** wifi
bool ui_wifi_get_status(void)
{
    return peri_buf[E_PERI_WIFI];
}
void ui_wifi_set_status(bool statue)
{
    peri_buf[E_PERI_WIFI] = statue;
}

String ui_wifi_get_ip(void)
{
    return WiFi.localIP().toString();
    // return "WIFI not connected";
}
const char *ui_wifi_get_ssid(void)
{
    return "WIFI not connected";
    // return WiFi.SSID().c_str();
    
}
const char *ui_wifi_get_pwd(void)
{
    return "WIFI not connected";
    // return WiFi.psk().c_str();
}

//************************************[ screen 7 ]****************************************** battery
#if 1
/* 25896 */
void battery_chg_encharge(void)
{
    PPM.enableCharge();
}

void battery_chg_discharge(void)
{
    PPM.disableCharge();
}

bool battery_25896_is_vaild(void)
{
    return peri_buf[E_PERI_BQ25896];
    // return 1;
}

bool battery_25896_is_vbus_in(void)
{
    return PPM.isVbusIn();
}

bool battery_25896_is_chr(void)
{
    return PPM.isCharging();
}

void battery_25896_refr(void)
{
}

const char * battery_25896_get_CHG_ST(void)
{
    return PPM.getChargeStatusString();
    // return 0;
}
const char * battery_25896_get_VBUS_ST(void) 
{
    return PPM.getBusStatusString();
    return 0;
}
const char * battery_25896_get_NTC_ST(void)
{
    return PPM.getNTCStatusString();
    // return 0;
}
float battery_25896_get_VBUS(void)
{
    return (PPM.getVbusVoltage() *1.0 / 1000.0 );
    // return 0;
}
float battery_25896_get_VSYS(void) 
{
    return (PPM.getSystemVoltage() * 1.0 / 1000.0);
    // return 0;
}
float battery_25896_get_VBAT(void)
{
    return (PPM.getBattVoltage() * 1.0 / 1000.0);
    // return 0;
}
float battery_25896_get_targ_VOLT(void)
{
    return (PPM.getChargeTargetVoltage() * 1.0 / 1000.0);
    // return 0;
}
float battery_25896_get_CHG_CURR(void)
{
    return (PPM.getChargeCurrent());
    // return 0;
}
float battery_25896_get_PREC_CURR(void)
{
    return (PPM.getPrechargeCurr());
}

/* 27220 */
bool ui_battery_27220_is_vaild(void) {return peri_buf[E_PERI_BQ27220]; }
bool ui_battery_27220_get_input(void) { return bq27220.getIsCharging();}
bool ui_battery_27220_get_charge_finish(void) { return bq27220.getCharingFinish();}
uint16_t ui_battery_27220_get_status(void) 
{
    BQ27220BatteryStatus batt;
    bq27220.getBatteryStatus(&batt);
    return batt.full;
}
uint16_t ui_battery_27220_get_voltage(void) { return bq27220.getVoltage(); }
int16_t ui_battery_27220_get_current(void) { return bq27220.getCurrent(); }
uint16_t ui_battery_27220_get_temperature(void) { return bq27220.getTemperature(); }
uint16_t ui_battery_27220_get_full_capacity(void) { return bq27220.getFullChargeCapacity(); }
uint16_t ui_battery_27220_get_design_capacity(void) { return bq27220.getDesignCapacity(); }
uint16_t ui_battery_27220_get_remain_capacity(void) { return bq27220.getRemainingCapacity(); }
uint16_t ui_battery_27220_get_percent(void) { return bq27220.getStateOfCharge(); }
uint16_t ui_battery_27220_get_health(void) { return bq27220.getStateOfHealth(); }
const char * ui_battert_27220_get_percent_level(void)
{
    int percent = bq27220.getStateOfCharge();
    const char * str = NULL;
    if(percent < 20)      str =  LV_SYMBOL_BATTERY_EMPTY;
    else if(percent < 40) str =  LV_SYMBOL_BATTERY_1;
    else if(percent < 65) str =  LV_SYMBOL_BATTERY_2;
    else if(percent < 90) str =  LV_SYMBOL_BATTERY_3;
    else                  str =  LV_SYMBOL_BATTERY_FULL;
    return str;
}
#endif
//************************************[ screen 8 ]****************************************** gps
uint32_t ui_gps_get_charsProcessed(void)
{
    return gps_get_charsProcessed();
}

void ui_gps_task_suspend(void)
{
    gps_task_suspend();
}
void ui_gps_task_resume(void)
{
    gps_task_resume();
}
void ui_gps_get_coord(double *lat, double *lng)
{
    gps_get_coord(lat, lng);
}
void ui_gps_get_data(uint16_t *year, uint8_t *month, uint8_t *day)
{
    gps_get_data(year, month, day);
}
void ui_gps_get_time(uint8_t *hour, uint8_t *minute, uint8_t *second)
{
    gps_get_time(hour, minute, second);
}

void ui_gps_get_satellites(uint32_t *vsat)
{
    gps_get_satellites(vsat);
}
void ui_gps_get_speed(double *speed)
{
    gps_get_speed(speed);
}

//************************************[ screen 8 ]****************************************** shutdown
void ui_shutdown_vcom(int v)
{
    epd_set_vcom(v);
}

void ui_shutdown(void)
{
    PPM.shutdown();
}

void ui_sleep(void)
{
    touch.sleep();
    lora_sleep();

    digitalWrite(BOARD_TOUCH_RST, LOW); 
    digitalWrite(BOARD_LORA_RST, LOW); 
    
    gpio_hold_en((gpio_num_t)BOARD_TOUCH_RST);
    gpio_hold_en((gpio_num_t)BOARD_LORA_RST);
    gpio_deep_sleep_hold_en();

    io_extend_lora_gps_power_on(false);
    analogWrite(BOARD_BL_EN, 0);

    epd_poweroff();

    esp_sleep_enable_ext0_wakeup((gpio_num_t)BOARD_BOOT_BTN, 0);
    // esp_sleep_enable_ext1_wakeup((1UL << KEY_BTN), ESP_EXT1_WAKEUP_ANY_LOW); 
    esp_deep_sleep_start();
}

