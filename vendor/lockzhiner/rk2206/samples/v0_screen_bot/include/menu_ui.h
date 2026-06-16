#ifndef _MENU_UI_H_
#define _MENU_UI_H_

#include <stdint.h>
#include "adc_key.h"

typedef enum {
    PAGE_IDLE = 0,
    PAGE_MAIN_MENU,
    PAGE_SUB_FEED_AMT,
    PAGE_SUB_SCHEDULE,
    PAGE_SUB_SCHEDULE_EDIT,  // F-4: 定时计划编辑子页
    PAGE_SUB_DEVICES,
    PAGE_SUB_FEEDING,
    PAGE_CAT_MODE,
    PAGE_SUB_ENV,
    PAGE_SUB_SETTINGS
} SystemPage;

extern volatile SystemPage g_sys_page;
extern volatile uint8_t g_page_changed;

extern int g_sensor_temp;
extern int g_sensor_hum;
extern int g_sensor_weight;
extern int g_sensor_water; 

extern volatile int16_t g_sync_year;
extern volatile int8_t  g_sync_month;
extern volatile int8_t  g_sync_day;
extern volatile int8_t  g_sync_hour;
extern volatile int8_t  g_sync_minute;

extern volatile uint8_t g_remote_cat_mode;
extern volatile int g_feeding_countdown; // 喂食倒计时(ms), 由 eyes_emotion.c 和 menu_ui.c 共用

/* F-3: 设备控制模式 */
typedef enum {
    CTRL_MODE_AUTO = 0,   // 传感器自动控制
    CTRL_MODE_MANUAL,     // 人工手动控制
} ControlMode;

extern volatile ControlMode g_fan_mode;
extern volatile ControlMode g_pump_mode;

void menu_process_key(KeyCode key);
void menu_draw_current_page(void);

void api_set_schedule(uint8_t index, uint8_t active, uint8_t hour, uint8_t minute, uint8_t portion);
void check_scheduled_feeding(uint8_t h, uint8_t m, uint8_t s);

void ui_jump_to_pump(void);
void ui_jump_to_idle(void);

// 【修改】加入来源参数：0代表本地/语音，1代表网络
void ui_jump_to_cat_mode(uint8_t is_remote);          
void ui_start_feeding(uint8_t portions); 

#endif