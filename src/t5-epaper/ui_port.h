#pragma once

#include "lvgl.h"

#define UI_REFRESH_MODE_FAST   0
#define UI_REFRESH_MODE_NORMAL 1
#define UI_REFRESH_MODE_NEAT   2

void ui_nvs_set_defaulat_param(void);
void ui_indev_touch_en(void);
void ui_indev_touch_dis(void);
void ui_refresh_set_mode(int mode);
int ui_refresh_get_mode();
void ui_full_refresh(void);
void ui_full_clean(void);
void ui_epd_clean(void);
void ui_set_rotation(lv_disp_rot_t rot);

// clock
void ui_clock_get_time(uint8_t *h, uint8_t *m, uint8_t *s);
void ui_clock_get_data(uint8_t *year, uint8_t *month, uint8_t *day, uint8_t *week);

// lora
#define LORA_MODE_SEND 0
#define LORA_MODE_RECV 1

void ui_lora_recv_suspend(void);
void ui_lora_recv_resume(void);
void ui_lora_set_mode(int mode);
int ui_lora_get_mode(void);
void ui_lora_send(const char *str);
bool ui_lora_recv(const char **str, int *rssi);
void ui_lora_clean_recv_flag(void);

float ui_lora_get_freq(void);
float ui_lora_get_bandwidth(void);
int16_t ui_lora_get_output_power(void);
uint8_t ui_lora_get_spread_factor(void);

// sd
void ui_sd_read(void);
void ui_sd_get_capacity(uint64_t *total, uint64_t *used);

// setting
int ui_setting_get_vcom(void);
void ui_setting_set_vcom(int v);
void ui_setting_set_backlight(int bl);
void ui_setting_set_backlight_level(int level);
const char *ui_setting_get_backlight(int *ret_bl);
void ui_setting_set_refresh_speed(int bl);
const char *ui_setting_get_refresh_speed(int *ret_bl);
// setting -> about system
const char *ui_setting_get_sf_ver(void);
const char *ui_setting_get_hd_ver(void);

// test
const char *ui_test_get_gps(int *ret_n);
const char *ui_test_get_lora(int *ret_n);
const char *ui_test_get_sd(int *ret_n);
const char *ui_test_get_rtc(int *ret_n);
const char *ui_test_get_touch(int *ret_n);
const char *ui_test_get_BQ25896(int *ret_n);
const char *ui_test_get_BQ27220(int *ret_n);

// wifi
bool ui_wifi_get_status(void);
void ui_wifi_set_status(bool statue);

String ui_wifi_get_ip(void);
const char *ui_wifi_get_ssid(void);
const char *ui_wifi_get_pwd(void);

// battery
/* 25896 */
void battery_chg_encharge(void);
void battery_chg_discharge(void);
bool battery_25896_is_vaild(void);
bool battery_25896_is_vbus_in(void);
bool battery_25896_is_chr(void);
void battery_25896_refr(void);
const char * battery_25896_get_CHG_ST(void);
const char * battery_25896_get_VBUS_ST(void);
const char * battery_25896_get_NTC_ST(void);
float battery_25896_get_VBUS(void);
float battery_25896_get_VSYS(void);
float battery_25896_get_VBAT(void);
float battery_25896_get_targ_VOLT(void);
float battery_25896_get_CHG_CURR(void);
float battery_25896_get_PREC_CURR(void);
/* 27220 */
bool ui_battery_27220_is_vaild(void);
bool ui_battery_27220_get_input(void);
bool ui_battery_27220_get_charge_finish(void);
uint16_t ui_battery_27220_get_status(void);
uint16_t ui_battery_27220_get_voltage(void);
int16_t ui_battery_27220_get_current(void);
uint16_t ui_battery_27220_get_temperature(void);
uint16_t ui_battery_27220_get_full_capacity(void);
uint16_t ui_battery_27220_get_design_capacity(void);
uint16_t ui_battery_27220_get_remain_capacity(void);
uint16_t ui_battery_27220_get_percent(void);    // percent = remain_capacity / full_capacity
uint16_t ui_battery_27220_get_health(void);     // health = full_capacity / design_capacity
const char * ui_battert_27220_get_percent_level(void);

// gsp
uint32_t ui_gps_get_charsProcessed(void);
void ui_gps_task_suspend(void);
void ui_gps_task_resume(void);
void ui_gps_get_coord(double *lat, double *lng);
void ui_gps_get_data(uint16_t *year, uint8_t *month, uint8_t *day);
void ui_gps_get_time(uint8_t *hour, uint8_t *minute, uint8_t *second);
void ui_gps_get_satellites(uint32_t *vsat);
void ui_gps_get_speed(double *speed);
// shutdown
void ui_shutdown_vcom(int v);
void ui_shutdown(void);

void ui_sleep(void);









