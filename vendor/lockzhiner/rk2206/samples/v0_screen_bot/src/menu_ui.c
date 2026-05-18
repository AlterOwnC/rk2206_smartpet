#include "menu_ui.h"
#include "lcd.h"
#include <stdio.h>
#include "feeder_motor.h"
#include "hardware_control.h"
#include "command_queue.h"

volatile SystemPage g_sys_page = PAGE_IDLE; 
volatile uint8_t g_page_changed = 1; 

static int8_t main_cursor = 0;   
static int8_t amount_cursor = 0; 
static int8_t schedule_cursor = 0; 
static int8_t device_cursor = 0; 

static int8_t setting_cursor = 0; 
static uint8_t is_editing = 0;    
static int16_t edit_year = 2026;
static int8_t  edit_month = 1;
static int8_t  edit_day = 1;
static int8_t  edit_hour = 12;
static int8_t  edit_minute = 0;

int g_feeder_speed = 60;        
int g_feeder_time_base = 3000;  
volatile int g_feeding_countdown = 0; 

int g_sensor_temp = 24;
int g_sensor_hum = 54;
int g_sensor_weight = 0;
int g_sensor_water = 80; 

volatile int16_t g_sync_year = 2026;
volatile int8_t  g_sync_month = 1;
volatile int8_t  g_sync_day = 1;
volatile int8_t  g_sync_hour = 12;
volatile int8_t  g_sync_minute = 0;

// 【新增】逗猫模式的“开启源”追踪标志 (默认0)
volatile uint8_t g_remote_cat_mode = 0;

void ui_jump_to_pump(void) {
    g_sys_page = PAGE_SUB_DEVICES;
    device_cursor = 0; 
    g_page_changed = 1;
}

void ui_jump_to_idle(void) {
    g_sys_page = PAGE_IDLE;
    g_page_changed = 1;
}

void ui_jump_to_cat_mode(uint8_t is_remote) {
    g_sys_page = PAGE_CAT_MODE;
    g_remote_cat_mode = is_remote; // 0=本地(免疫网络关闭), 1=远程网络
    g_page_changed = 1;
}

void ui_start_feeding(uint8_t portions) {
    g_sys_page = PAGE_SUB_FEEDING; 
    g_feeding_countdown = g_feeder_time_base * portions;
    feeder_motor_start(g_feeder_speed);
    g_page_changed = 1;
}

typedef struct {
    uint8_t active;  
    uint8_t hour;    
    uint8_t minute;  
    uint8_t portion; 
} FeedSchedule;

FeedSchedule g_schedules[5] = {
    {1, 8,  30, 0}, 
    {1, 18, 0,  1}, 
    {0, 12, 0,  0}, 
    {0, 0,  0,  0},
    {0, 0,  0,  0}
};

void api_set_schedule(uint8_t index, uint8_t active, uint8_t hour, uint8_t minute, uint8_t portion)
{
    if (index < 5) {
        g_schedules[index].active = active;
        g_schedules[index].hour = hour;
        g_schedules[index].minute = minute;
        g_schedules[index].portion = portion;
        g_page_changed = 1; 
    }
}

void check_scheduled_feeding(uint8_t h, uint8_t m, uint8_t s)
{
    static uint8_t last_trigger_min = 60; 
    if (m != last_trigger_min && g_sys_page == PAGE_IDLE) 
    {
        for (int i = 0; i < 5; i++) {
            if (g_schedules[i].active && g_schedules[i].hour == h && g_schedules[i].minute == m) 
            {
                last_trigger_min = m; 
                ui_start_feeding(g_schedules[i].portion + 1);
                break;
            }
        }
    }
}

void menu_process_key(KeyCode key)
{
    if (key == KEY_NONE) return;
    g_page_changed = 1; 

    switch (g_sys_page)
    {
        case PAGE_IDLE:
            g_sys_page = PAGE_MAIN_MENU;
            main_cursor = 0;
            break;

        case PAGE_CAT_MODE:
            g_sys_page = PAGE_MAIN_MENU; 
            break;

        case PAGE_MAIN_MENU:
            if (key == KEY_DOWN)      { main_cursor++; if(main_cursor > 5) main_cursor = 0; }
            else if (key == KEY_UP)   { main_cursor--; if(main_cursor < 0) main_cursor = 5; }
            else if (key == KEY_BACK) { g_sys_page = PAGE_IDLE; } 
            else if (key == KEY_ENTER) 
            {
                if (main_cursor == 0)      { g_sys_page = PAGE_SUB_FEED_AMT; amount_cursor = 0; }
                else if (main_cursor == 1) { g_sys_page = PAGE_SUB_DEVICES; device_cursor = 0; } 
                else if (main_cursor == 2) { g_sys_page = PAGE_SUB_SCHEDULE; schedule_cursor = 0; } 
                else if (main_cursor == 3) { 
                    g_sys_page = PAGE_CAT_MODE; 
                    // 【核心防御】：本地菜单进入时，标记为本地开启(0)，UDP的0指令将无法中断它！
                    g_remote_cat_mode = 0; 
                }
                else if (main_cursor == 4) { g_sys_page = PAGE_SUB_ENV; }
                else if (main_cursor == 5) { 
                    g_sys_page = PAGE_SUB_SETTINGS; 
                    setting_cursor = 0;
                    is_editing = 0;
                } 
            }
            break;

        case PAGE_SUB_DEVICES:
            if (key == KEY_DOWN)      { device_cursor++; if(device_cursor > 1) device_cursor = 0; }
            else if (key == KEY_UP)   { device_cursor--; if(device_cursor < 0) device_cursor = 1; }
            else if (key == KEY_BACK) { g_sys_page = PAGE_MAIN_MENU; } 
            else if (key == KEY_ENTER) { 
                if (device_cursor == 0) { pump_control(!g_pump_state); }
                if (device_cursor == 1) { fan_control(!g_fan_state); }
            } 
            break;

        case PAGE_SUB_FEED_AMT:
            if (key == KEY_DOWN)      { amount_cursor++; if(amount_cursor > 2) amount_cursor = 0; }
            else if (key == KEY_UP)   { amount_cursor--; if(amount_cursor < 0) amount_cursor = 2; }
            else if (key == KEY_BACK) { g_sys_page = PAGE_MAIN_MENU; } 
            else if (key == KEY_ENTER) { 
                ui_start_feeding(amount_cursor + 1);
            } 
            break;

        case PAGE_SUB_SCHEDULE:
            if (key == KEY_DOWN)      { schedule_cursor++; if(schedule_cursor > 4) schedule_cursor = 0; }
            else if (key == KEY_UP)   { schedule_cursor--; if(schedule_cursor < 0) schedule_cursor = 4; }
            else if (key == KEY_BACK) { g_sys_page = PAGE_MAIN_MENU; } 
            else if (key == KEY_ENTER) { 
                g_schedules[schedule_cursor].active = !g_schedules[schedule_cursor].active;
            } 
            break;

        case PAGE_SUB_FEEDING:
            feeder_motor_start(0); 
            g_feeding_countdown = 0;
            g_sys_page = PAGE_MAIN_MENU; 
            break;

        case PAGE_SUB_ENV:
            if (key == KEY_BACK) { g_sys_page = PAGE_MAIN_MENU; }
            else { g_page_changed = 0; }
            break;

        case PAGE_SUB_SETTINGS:
            if (is_editing) 
            {
                if (key == KEY_UP) {
                    if (setting_cursor == 0)      { edit_year++; if(edit_year > 2099) edit_year = 2000; }
                    else if (setting_cursor == 1) { edit_month++; if(edit_month > 12) edit_month = 1; }
                    else if (setting_cursor == 2) { edit_day++; if(edit_day > 31) edit_day = 1; }
                    else if (setting_cursor == 3) { edit_hour++; if(edit_hour > 23) edit_hour = 0; }
                    else if (setting_cursor == 4) { edit_minute++; if(edit_minute > 59) edit_minute = 0; }
                } 
                else if (key == KEY_DOWN) {
                    if (setting_cursor == 0)      { edit_year--; if(edit_year < 2000) edit_year = 2099; }
                    else if (setting_cursor == 1) { edit_month--; if(edit_month < 1) edit_month = 12; }
                    else if (setting_cursor == 2) { edit_day--; if(edit_day < 1) edit_day = 31; }
                    else if (setting_cursor == 3) { edit_hour--; if(edit_hour < 0) edit_hour = 23; }
                    else if (setting_cursor == 4) { edit_minute--; if(edit_minute < 0) edit_minute = 59; }
                } 
                else if (key == KEY_ENTER || key == KEY_BACK) {
                    is_editing = 0;
                }
            } 
            else 
            {
                if (key == KEY_DOWN)      { setting_cursor++; if(setting_cursor > 5) setting_cursor = 0; }
                else if (key == KEY_UP)   { setting_cursor--; if(setting_cursor < 0) setting_cursor = 5; }
                else if (key == KEY_BACK) { g_sys_page = PAGE_MAIN_MENU; } 
                else if (key == KEY_ENTER) 
                {
                    if (setting_cursor == 5) {
                        g_sync_year   = edit_year;
                        g_sync_month  = edit_month;
                        g_sync_day    = edit_day;
                        g_sync_hour   = edit_hour;
                        g_sync_minute = edit_minute;
                        
                        osEventFlagsSet(g_rtc_evt, EVENT_RTC_SYNC);
                        g_sys_page = PAGE_MAIN_MENU;
                    } else {
                        is_editing = 1;
                    }
                }
            }
            break;
            
        default: break;
    }
}

static uint16_t get_setting_color(int8_t index) {
    if (setting_cursor == index) {
        return is_editing ? LCD_GREEN : LCD_YELLOW; 
    }
    return LCD_WHITE;
}

void menu_draw_current_page(void)
{
    uint8_t page_entered = g_page_changed;
    if (g_page_changed) {
        if (g_sys_page != PAGE_SUB_ENV) {
            lcd_fill(0, 0, LCD_W, LCD_H, LCD_BLACK);
        }
        g_page_changed = 0;
    }

    uint16_t c_sel = LCD_YELLOW; 
    uint16_t c_nor = LCD_WHITE;  

    if (g_sys_page == PAGE_MAIN_MENU)
    {
        lcd_show_string(100, 35, (const uint8_t *)"MAIN MENU", LCD_LIGHTBLUE, LCD_BLACK, 24, 0);
        lcd_show_string(90, 65,  (const uint8_t *)"Feed Now",    main_cursor==0?c_sel:c_nor, LCD_BLACK, 24, 0);
        lcd_show_string(90, 93,  (const uint8_t *)"Water & Fan", main_cursor==1?c_sel:c_nor, LCD_BLACK, 24, 0); 
        lcd_show_string(90, 121, (const uint8_t *)"Schedules",   main_cursor==2?c_sel:c_nor, LCD_BLACK, 24, 0);
        lcd_show_string(90, 149, (const uint8_t *)"Interactive", main_cursor==3?c_sel:c_nor, LCD_BLACK, 24, 0);
        lcd_show_string(90, 177, (const uint8_t *)"Sensor Data", main_cursor==4?c_sel:c_nor, LCD_BLACK, 24, 0);
        lcd_show_string(90, 205, (const uint8_t *)"Settings",    main_cursor==5?c_sel:c_nor, LCD_BLACK, 24, 0);
        lcd_show_string(50, 65 + main_cursor*28, (const uint8_t *)"->", c_sel, LCD_BLACK, 24, 0);
    }
    else if (g_sys_page == PAGE_SUB_DEVICES)
    {
        lcd_show_string(80, 45, (const uint8_t *)"CARE DEVICES", LCD_LIGHTBLUE, LCD_BLACK, 24, 0);
        char buf[32];
        sprintf(buf, "[%s] Water Pump", g_pump_state ? "ON " : "OFF");
        lcd_show_string(70, 100, (const uint8_t *)buf, device_cursor==0?c_sel:c_nor, LCD_BLACK, 16, 0);
        sprintf(buf, "[%s] Cooling Fan", g_fan_state ? "ON " : "OFF");
        lcd_show_string(70, 140, (const uint8_t *)buf, device_cursor==1?c_sel:c_nor, LCD_BLACK, 16, 0);
        lcd_show_string(40, 100 + device_cursor*40, (const uint8_t *)"->", c_sel, LCD_BLACK, 16, 0);
    }
    else if (g_sys_page == PAGE_SUB_SCHEDULE)
    {
        lcd_show_string(90, 35, (const uint8_t *)"TIMERS", LCD_LIGHTBLUE, LCD_BLACK, 24, 0);
        char buf[32];
        for (int i = 0; i < 5; i++) {
            sprintf(buf, "[%s] %02d:%02d Lv:%d", 
                g_schedules[i].active ? "ON " : "OFF", 
                g_schedules[i].hour, g_schedules[i].minute, 
                g_schedules[i].portion + 1);
            lcd_show_string(70, 70 + i*35, (const uint8_t *)buf, schedule_cursor==i?c_sel:c_nor, LCD_BLACK, 16, 0);
        }
        lcd_show_string(40, 70 + schedule_cursor*35, (const uint8_t *)"->", c_sel, LCD_BLACK, 16, 0);
    }
    else if (g_sys_page == PAGE_SUB_FEED_AMT)
    {
        lcd_show_string(80, 45, (const uint8_t *)"PORTION SIZE", LCD_LIGHTBLUE, LCD_BLACK, 24, 0);
        lcd_show_string(120, 100, (const uint8_t *)"1 Portion",  amount_cursor==0?c_sel:c_nor, LCD_BLACK, 24, 0);
        lcd_show_string(120, 140, (const uint8_t *)"2 Portions", amount_cursor==1?c_sel:c_nor, LCD_BLACK, 24, 0);
        lcd_show_string(120, 180, (const uint8_t *)"3 Portions", amount_cursor==2?c_sel:c_nor, LCD_BLACK, 24, 0);
        lcd_show_string(80, 100 + amount_cursor*40, (const uint8_t *)"->", c_sel, LCD_BLACK, 24, 0);
    }
    else if (g_sys_page == PAGE_SUB_ENV)
    {
        // 标签栏 x=60, 数值起始 x=124 (8字符 × 8px), 字号16 → 字宽8px
        // 温湿度/水位: 3位数字, 重量: 5位数字
        if (page_entered) {
            lcd_fill(0, 0, LCD_W, LCD_H, LCD_BLACK);
            lcd_show_string(100, 45, (const uint8_t *)"SENSOR DATA", LCD_LIGHTBLUE, LCD_BLACK, 24, 0);
            // 静态标签 + 单位 (只画一次)
            lcd_show_string(60,  90, (const uint8_t *)"Temp:   ", LCD_WHITE, LCD_BLACK, 16, 0);
            lcd_show_string(60, 130, (const uint8_t *)"Humid:  ", LCD_WHITE, LCD_BLACK, 16, 0);
            lcd_show_string(60, 170, (const uint8_t *)"Weight: ", LCD_WHITE, LCD_BLACK, 16, 0);
            lcd_show_string(60, 210, (const uint8_t *)"Water:  ", LCD_WHITE, LCD_BLACK, 16, 0);
        }
        // 单位后缀 — 3位数字后
        lcd_show_string(148,  90, (const uint8_t *)" C", LCD_WHITE, LCD_BLACK, 16, 0);
        lcd_show_string(148, 130, (const uint8_t *)" %", LCD_WHITE, LCD_BLACK, 16, 0);
        lcd_show_string(164, 170, (const uint8_t *)" g", LCD_WHITE, LCD_BLACK, 16, 0);
        lcd_show_string(148, 210, (const uint8_t *)" %", LCD_WHITE, LCD_BLACK, 16, 0);

        // 数值区 (每次刷新, 固定宽度覆盖旧值)
        if (!page_entered) {
            lcd_fill(124,  90, 148, 106, LCD_BLACK);  // 3位 × 8px = 24px
            lcd_fill(124, 130, 148, 146, LCD_BLACK);
            lcd_fill(124, 170, 164, 186, LCD_BLACK);  // 5位 × 8px = 40px
            lcd_fill(124, 210, 148, 226, LCD_BLACK);
        }
        lcd_show_int_num(124,  90, (uint16_t)g_sensor_temp,   3, LCD_WHITE, LCD_BLACK, 16);
        lcd_show_int_num(124, 130, (uint16_t)g_sensor_hum,    3, LCD_WHITE, LCD_BLACK, 16);
        lcd_show_int_num(124, 170, (uint16_t)g_sensor_weight, 5, LCD_WHITE, LCD_BLACK, 16);
        lcd_show_int_num(124, 210, (uint16_t)g_sensor_water,  3, LCD_WHITE, LCD_BLACK, 16);
    }
    else if (g_sys_page == PAGE_SUB_SETTINGS)
    {
        lcd_show_string(80, 35, (const uint8_t *)"SYSTEM TIME", LCD_LIGHTBLUE, LCD_BLACK, 24, 0);
        char buf[32];
        sprintf(buf, "Year:   %04d", edit_year);
        lcd_show_string(70, 70, (const uint8_t *)buf, get_setting_color(0), LCD_BLACK, 16, 0);
        sprintf(buf, "Month:  %02d", edit_month);
        lcd_show_string(70, 100, (const uint8_t *)buf, get_setting_color(1), LCD_BLACK, 16, 0);
        sprintf(buf, "Day:    %02d", edit_day);
        lcd_show_string(70, 130, (const uint8_t *)buf, get_setting_color(2), LCD_BLACK, 16, 0);
        sprintf(buf, "Hour:   %02d", edit_hour);
        lcd_show_string(70, 160, (const uint8_t *)buf, get_setting_color(3), LCD_BLACK, 16, 0);
        sprintf(buf, "Minute: %02d", edit_minute);
        lcd_show_string(70, 190, (const uint8_t *)buf, get_setting_color(4), LCD_BLACK, 16, 0);
        
        lcd_show_string(70, 220, (const uint8_t *)"[ Save & Sync ]", get_setting_color(5), LCD_BLACK, 16, 0);
        lcd_show_string(40, 70 + setting_cursor * 30, (const uint8_t *)"->", c_sel, LCD_BLACK, 16, 0);
    }
}