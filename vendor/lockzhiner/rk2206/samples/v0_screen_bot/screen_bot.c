#include <stdio.h>
#include "los_task.h"
#include "ohos_init.h"
#include "lcd.h"
#include "adc_key.h"
#include "menu_ui.h"
#include "eyes_emotion.h"
#include "servo_drive.h"
#include "feeder_motor.h"
#include "hardware_control.h"
#include "udp_server.h"
#include "voice_module.h"
#include "mqtt_cloud.h"
#include "ds3231.h"
#include "lz_hardware.h"
#include "command_queue.h"

volatile KeyCode g_current_key = KEY_NONE;

// 环境自动控制阈值
#define TEMP_HIGH_START  28
#define TEMP_OK_STOP     27
#define WATER_LOW_START  20
#define WATER_OK_STOP    80

void adc_process(void *arg)
{
    adc_key_init();
    uint8_t key_pressed_flag = 0;
    while (1) {
        KeyCode current_key = adc_key_scan();
        if (current_key != KEY_NONE) {
            if (key_pressed_flag == 0) {
                g_current_key = current_key;
                key_pressed_flag = 1;
            }
        } else {
            key_pressed_flag = 0;
        }
        LOS_Msleep(20);
    }
}

void rtc_process(void *arg)
{
    ds3231_init();
    RTC_Time temp;
    ds3231_get_datetime(&temp);
    osMutexAcquire(g_rtc_mutex, osWaitForever);
    g_rtc_time = temp;
    osMutexRelease(g_rtc_mutex);

    while (1) {
        uint32_t flags = osEventFlagsWait(g_rtc_evt, EVENT_RTC_SYNC,
                                          osFlagsWaitAny, 200);
        if ((flags & osFlagsError) == 0) {
            printf("\n[RTC] Writing new time safely from UI request...\n");
            ds3231_set_datetime(g_sync_year, g_sync_month, g_sync_day,
                                g_sync_hour, g_sync_minute, 0);

            uint8_t write_flag[2] = {0x07, 0xAA};
            LzI2cWrite(1, 0x68, write_flag, 2);
        }

        ds3231_get_datetime(&temp);
        osMutexAcquire(g_rtc_mutex, osWaitForever);
        g_rtc_time = temp;
        osMutexRelease(g_rtc_mutex);
    }
}

void lcd_process(void *arg)
{
    if (lcd_init() != 0) return;

    servo_init();
    devices_init();
    feeder_motor_init();

    static SystemPage last_sys_page = PAGE_IDLE;

    while (1)
    {
        SystemCommand cmd;
        while (cmd_recv(&cmd, 0) == osOK) {
            switch (cmd.type) {
            case CMD_FEED:
                if (g_sys_page != PAGE_SUB_FEEDING) {
                    ui_start_feeding(cmd.param1);
                }
                break;
            case CMD_CAT_MODE_ENTER:
                if (g_sys_page != PAGE_CAT_MODE) {
                    ui_jump_to_cat_mode((uint8_t)cmd.param1);
                }
                break;
            case CMD_CAT_MODE_EXIT:
                if (g_sys_page == PAGE_CAT_MODE && g_remote_cat_mode == 1) {
                    ui_jump_to_idle();
                }
                break;
            case CMD_FAN_ON:
                fan_control(1);
                break;
            case CMD_FAN_OFF:
                fan_control(0);
                break;
            case CMD_PUMP_ON:
                pump_control(1);
                break;
            case CMD_PUMP_OFF:
                pump_control(0);
                break;
            case CMD_SENSOR_UPDATE:
                g_sensor_temp   = cmd.param1;
                g_sensor_hum    = cmd.param2;
                g_sensor_weight = cmd.param3;
                g_sensor_water  = cmd.param4;
                // 传感器数据页面使用局部刷新, 不触发全屏重绘
                break;
            default:
                break;
            }
        }

        if (g_sensor_temp >= TEMP_HIGH_START && g_fan_state == 0) {
            fan_control(1);
        } else if (g_sensor_temp < TEMP_OK_STOP && g_fan_state == 1) {
            fan_control(0);
        }

        if (g_sensor_water <= WATER_LOW_START && g_pump_state == 0) {
            pump_control(1);
        } else if (g_sensor_water >= WATER_OK_STOP && g_pump_state == 1) {
            pump_control(0);
        }

        if (g_current_key != KEY_NONE) {
            menu_process_key(g_current_key);
            g_current_key = KEY_NONE;
        }

        check_scheduled_feeding(g_rtc_time.hour, g_rtc_time.minute,
                                g_rtc_time.second);

        if (last_sys_page != g_sys_page) {
            if (g_sys_page == PAGE_CAT_MODE) {
                laser_control(1);
            } else if (last_sys_page == PAGE_CAT_MODE) {
                laser_control(0);
            }
            last_sys_page = g_sys_page;
        }

        ui_update_time_only();

        if (g_sys_page == PAGE_IDLE) {
            if (g_page_changed) {
                lcd_fill(0, 0, LCD_W, LCD_H, LCD_BLACK);
                ui_draw_status_bar();
                ui_draw_footer();
                g_page_changed = 0;
            }
            eyes_play_normal_step();
        } else if (g_sys_page == PAGE_CAT_MODE) {
            if (g_page_changed) {
                lcd_fill(0, 0, LCD_W, LCD_H, LCD_BLACK);
                ui_draw_status_bar();
                g_page_changed = 0;
            }
            eyes_play_cat_step();
        } else if (g_sys_page == PAGE_SUB_FEEDING) {
            if (g_page_changed) {
                lcd_fill(0, 0, LCD_W, LCD_H, LCD_BLACK);
                ui_draw_status_bar();
                g_page_changed = 0;
            }
            eyes_play_feeding_step();
        } else {
            menu_draw_current_page();
            LOS_Msleep(50);
        }
    }
}

void app_example_init(void)
{
    system_sync_init();

    unsigned int tid1, tid2, tid3, tid4, tid5, tid6;
    TSK_INIT_PARAM_S t = {0};

    t.pfnTaskEntry = (TSK_ENTRY_FUNC)lcd_process;
    t.uwStackSize = 20480;
    t.pcName = "lcd_ui";
    t.usTaskPrio = 24;
    LOS_TaskCreate(&tid1, &t);

    t.pfnTaskEntry = (TSK_ENTRY_FUNC)adc_process;
    t.uwStackSize = 2048;
    t.pcName = "adc_key";
    t.usTaskPrio = 23;
    LOS_TaskCreate(&tid2, &t);

    t.pfnTaskEntry = (TSK_ENTRY_FUNC)rtc_process;
    t.uwStackSize = 2048;
    t.pcName = "rtc_task";
    t.usTaskPrio = 25;
    LOS_TaskCreate(&tid3, &t);

    t.pfnTaskEntry = (TSK_ENTRY_FUNC)udp_server_thread;
    t.uwStackSize = 10240;
    t.pcName = "udp_server_task";
    t.usTaskPrio = 6;
    LOS_TaskCreate(&tid4, &t);

    t.pfnTaskEntry = (TSK_ENTRY_FUNC)voice_uart_thread;
    t.uwStackSize = 4096;
    t.pcName = "voice_uart_task";
    t.usTaskPrio = 24;
    LOS_TaskCreate(&tid5, &t);

    t.pfnTaskEntry = (TSK_ENTRY_FUNC)mqtt_cloud_thread;
    t.uwStackSize = 12288;
    t.pcName = "mqtt_cloud";
    t.usTaskPrio = 7;
    LOS_TaskCreate(&tid6, &t);
}

APP_FEATURE_INIT(app_example_init);
