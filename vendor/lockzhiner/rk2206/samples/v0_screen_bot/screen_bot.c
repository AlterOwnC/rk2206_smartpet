/***************************************************************
 * 文件名: screen_bot.c
 * 说    明: v0_screen_bot 智能宠物陪伴机器人 — 主控制模块
 *           负责创建 6 个 RTOS 线程, 协调 LCD 动画、按键检测、
 *           RTC 时钟、WiFi UDP、语音 UART、MQTT 云连接。
 *           主循环(lcd_ui 线程)消费命令队列, 驱动表情动画引擎,
 *           执行自动环境调控(温控风扇/水位水泵)和定时喂食。
 ***************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include "los_task.h"
#include "los_mux.h"
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

/* 当前按下的按键键值 (由 adc_process 线程写入, lcd_ui 线程消费) */
volatile KeyCode g_current_key = KEY_NONE;

/* ================== 环境自动控制阈值 ================== */

/* 温度 >= 30°C 时自动开启风扇, 降至 26°C 以下时关闭 (4°C 滞后防频繁启停) */
#define TEMP_HIGH_START  30
#define TEMP_OK_STOP     26

/* 水位 <= 20% 时自动开启水泵, 升至 80% 以上时关闭 (60% 滞后防频繁启停) */
#define WATER_LOW_START  20
#define WATER_OK_STOP    80

/***************************************************************
 * 线程名称: adc_process
 * 说    明: ADC 按键扫描线程 (优先级 23, 栈 2KB)
 *           每 20ms 扫描一次 ADC 电压值, 识别四个按键并消抖,
 *           将按键结果写入全局变量 g_current_key
 * 参    数: arg — 未使用
 * 返 回 值: 无
 ***************************************************************/
void adc_process(void *arg)
{
    adc_key_init();
    uint8_t key_pressed_flag = 0; // 按键消抖标志: 0=未按下, 1=已按下
    while (1) {
        KeyCode current_key = adc_key_scan();
        if (current_key != KEY_NONE) {
            /* 仅在按键首次按下时触发, 避免重复响应 */
            if (key_pressed_flag == 0) {
                g_current_key = current_key;
                key_pressed_flag = 1;
            }
        } else {
            key_pressed_flag = 0;
        }
        LOS_Msleep(20); // 20ms 扫描周期
    }
}

/***************************************************************
 * 线程名称: rtc_process
 * 说    明: RTC 时钟读写线程 (优先级 25, 栈 2KB)
 *           定期读取 DS3231 时钟芯片的时间, 通过互斥锁更新全局
 *           时间变量 g_rtc_time; 监听 RTC 同步事件以接收 UI 端
 *           的时间设置请求并写入硬件。
 * 参    数: arg — 未使用
 * 返 回 值: 无
 ***************************************************************/
void rtc_process(void *arg)
{
    ds3231_init();
    RTC_Time temp;
    ds3231_get_datetime(&temp);

    /* 初始化时获取当前 RTC 时间 */
    LOS_MuxPend(g_rtc_mutex, LOS_WAIT_FOREVER);
    g_rtc_time = temp;
    LOS_MuxPost(g_rtc_mutex);

    while (1) {
        /* 等待 UI 端的时间同步事件 (200ms 超时, 非阻塞) */
        uint32_t flags = LOS_EventRead(&g_rtc_evt, EVENT_RTC_SYNC,
                                       LOS_WAITMODE_OR | LOS_WAITMODE_CLR, LOS_MS2Tick(200));
        /* 精确判等而非 & 判位：LOS_ERRNO_EVENT_READ_TIMEOUT (0x02001c01)
         * 的 bit0=1，与 EVENT_RTC_SYNC (0x01) 逐位重合，位与会误判 */
        if (flags == EVENT_RTC_SYNC) {
            printf("\n[RTC] Writing new time safely from UI request...\n");
            int ret = ds3231_set_datetime(g_sync_year, g_sync_month, g_sync_day,
                                          g_sync_hour, g_sync_minute, 0);
            if (ret != 0) {
                printf("[RTC] WARNING: set_datetime returned %d — time may NOT be saved!\n", ret);
            }

            /* 写入一次标志位防止下次上电丢失时间 (仅当时间写入成功后) */
            if (ret == 0) {
                uint8_t write_flag[2] = {0x07, 0xAA};
                LzI2cWrite(1, 0x68, write_flag, 2);
            }
        }

        /* 每次循环都更新全局时间 (供 LCD 状态栏和定时喂食使用) */
        ds3231_get_datetime(&temp);
        LOS_MuxPend(g_rtc_mutex, LOS_WAIT_FOREVER);
        g_rtc_time = temp;
        LOS_MuxPost(g_rtc_mutex);
    }
}

/***************************************************************
 * 线程名称: lcd_process
 * 说    明: LCD 主循环线程 (优先级 24, 栈 20KB)
 *           系统的核心调度线程, 负责:
 *           1. 初始化 LCD/舵机/外设/电机
 *           2. 消费命令队列 (来自 UDP/语音/MQTT) 并执行硬件动作
 *           3. 自动环境调控 (温度→风扇, 水位→水泵)
 *           4. 按键分发和定时喂食检测
 *           5. 页面状态机驱动 (动画 vs 菜单)
 * 参    数: arg — 未使用
 * 返 回 值: 无
 ***************************************************************/
void lcd_process(void *arg)
{
    /* 先初始化硬件外设到安全状态, 再初始化 LCD
       GPIO 默认输入可能导致 MOSFET/继电器意外导通, 必须优先配置 */
    devices_init();
    feeder_motor_init();
    servo_init();

    if (lcd_init() != 0) {
        printf("[FATAL] LCD init failed — thread exiting (peripherals are in safe state)\n");
        return;
    }

    static SystemPage last_sys_page = PAGE_IDLE; // 记录上一帧的页面, 用于检测页面切换

    while (1)
    {
        /* ---- 1. 消费命令队列 (非阻塞, 一次清空全部积压指令) ---- */
        SystemCommand cmd;
        while (cmd_recv(&cmd, 0) == LOS_OK) {
            switch (cmd.type) {
            case CMD_FEED:
                /* 喂食: 仅在非喂食页面触发, 防止重复叠加 */
                if (g_sys_page != PAGE_SUB_FEEDING) {
                    ui_start_feeding(cmd.param1);
                }
                break;
            case CMD_CAT_MODE_ENTER:
                /* 进入逗猫模式: param1=0 本地/语音, param1=1 远程UDP */
                if (g_sys_page != PAGE_CAT_MODE) {
                    ui_jump_to_cat_mode((uint8_t)cmd.param1);
                }
                break;
            case CMD_CAT_MODE_EXIT:
                /* 退出逗猫模式: 仅远程模式(g_remote_cat_mode==1)允许云端关闭 */
                if (g_sys_page == PAGE_CAT_MODE && g_remote_cat_mode == 1) {
                    ui_jump_to_idle();
                }
                break;
            case CMD_FAN_ON:
                fan_control(1);
                g_fan_manual_override = 1; /* 手动控制, 阻止自动调控覆盖 */
                break;
            case CMD_FAN_OFF:
                fan_control(0);
                g_fan_manual_override = 1;
                break;
            case CMD_PUMP_ON:
                pump_control(1); // 高电平开启水泵 (因引入电机驱动模块)
                g_pump_manual_override = 1;
                break;
            case CMD_PUMP_OFF:
                pump_control(0);
                g_pump_manual_override = 1;
                break;
            case CMD_SENSOR_UPDATE:
                /* 传感器数据更新: 来自 UDP 服务器的 JSON 解析结果
                   param1=温度, param2=湿度, param3=重量, param4=水位
                   互斥锁保护写入, 与 voice_module/mqtt_cloud 读端一致
                   范围验证: 拒绝明显异常值, 防止自动调控误动作 */
                LOS_MuxPend(g_sensor_mutex, LOS_WAIT_FOREVER);
                if (cmd.param1 >= -40 && cmd.param1 <= 85)
                    g_sensor_temp = cmd.param1;
                if (cmd.param2 >= 0 && cmd.param2 <= 100)
                    g_sensor_hum = cmd.param2;
                if (cmd.param3 >= 0 && cmd.param3 <= 5000)
                    g_sensor_weight = cmd.param3;
                if (cmd.param4 >= 0 && cmd.param4 <= 100)
                    g_sensor_water = cmd.param4;
                LOS_MuxPost(g_sensor_mutex);
                break;
            default:
                break;
            }
        }

        /* ---- 2. 自动环境调控 (滞后控制算法, 防止频繁开关) ---- */

        /* 温度控制: 高温 → 开风扇, 降温到安全线 → 关风扇
           手动覆盖标志: 传感器回到安全范围时自动清零, 恢复自动模式
           F-3: 仅在 AUTO 模式下自动调控; MANUAL 模式下跳过 */
        if (g_fan_manual_override && g_sensor_temp < TEMP_OK_STOP) {
            g_fan_manual_override = 0; /* 温度已恢复安全, 交还自动控制 */
        }
        if (g_fan_mode == CTRL_MODE_AUTO && !g_fan_manual_override) {
            if (g_sensor_temp >= TEMP_HIGH_START && g_fan_state == 0) {
                fan_control(1);
            } else if (g_sensor_temp < TEMP_OK_STOP && g_fan_state == 1) {
                fan_control(0);
            }
        }

        /* 水位控制: 缺水 → 开水泵, 补水到安全线 → 关水泵
           手动覆盖标志: 水位回到安全范围时自动清零, 恢复自动模式
           F-3: 仅在 AUTO 模式下自动调控; MANUAL 模式下跳过 */
        if (g_pump_manual_override && g_sensor_water >= WATER_OK_STOP) {
            g_pump_manual_override = 0; /* 水位已恢复安全, 交还自动控制 */
        }
        if (g_pump_mode == CTRL_MODE_AUTO && !g_pump_manual_override) {
            if (g_sensor_water <= WATER_LOW_START && g_pump_state == 0) {
                pump_control(1);
            } else if (g_sensor_water >= WATER_OK_STOP && g_pump_state == 1) {
                pump_control(0);
            }
        }

        /* ---- 3. 按键处理 ---- */
        if (g_current_key != KEY_NONE) {
            menu_process_key(g_current_key);
            g_current_key = KEY_NONE;
        }

        /* ---- 4. 定时喂食检测 (线程安全读取 RTC 时间) ---- */
        {
            RTC_Time t;
            rtc_time_read_safe(&t);
            check_scheduled_feeding(t.hour, t.minute, t.second);
        }

        /* ---- 5. 页面切换时激光联动控制 ---- */
        if (last_sys_page != g_sys_page) {
            if (g_sys_page == PAGE_CAT_MODE) {
                laser_control(1); // 进入逗猫模式 → 开启激光
            } else if (last_sys_page == PAGE_CAT_MODE) {
                laser_control(0); // 退出逗猫模式 → 关闭激光
            }
            last_sys_page = g_sys_page;
        }

        /* ---- 6. UI 渲染 (状态栏时间实时刷新) ---- */
        ui_update_time_only();

        /* ---- 7. 页面状态机: 根据当前页面选择不同的驱动逻辑 ---- */
        if (g_sys_page == PAGE_IDLE) {
            /* 待机页: 全屏重绘 → 状态栏+底栏 → 灵动电子眼随机动画 */
            if (g_page_changed) {
                lcd_fill(0, 0, LCD_W, LCD_H, LCD_BLACK);
                ui_draw_status_bar();
                ui_draw_footer();
                g_page_changed = 0;
            }
            eyes_play_normal_step();
        } else if (g_sys_page == PAGE_CAT_MODE) {
            /* 逗猫页: 全屏重绘 → 状态栏 → 仿生猎物模拟动画 */
            if (g_page_changed) {
                lcd_fill(0, 0, LCD_W, LCD_H, LCD_BLACK);
                ui_draw_status_bar();
                g_page_changed = 0;
            }
            eyes_play_cat_step();
        } else if (g_sys_page == PAGE_SUB_FEEDING) {
            /* 喂食页: 全屏重绘 → 状态栏 → 8帧喂食专属动画 */
            if (g_page_changed) {
                lcd_fill(0, 0, LCD_W, LCD_H, LCD_BLACK);
                ui_draw_status_bar();
                g_page_changed = 0;
            }
            eyes_play_feeding_step();
        } else {
            /* 菜单页: 不在此处做全屏重绘, 由 menu_draw_current_page 自行管理 */
            menu_draw_current_page();
            LOS_Msleep(50); // 菜单页降低刷新率, 节省 CPU
        }
    }
}

/***************************************************************
 * 函数名称: app_example_init
 * 说    明: 系统启动入口 — 创建 6 个 RTOS 线程
 *           所有线程的创建参数如下:
 *
 *           | 线程名           | 优先级 | 栈大小 | 功能                     |
 *           |:---             |:---    |:---    |:---                     |
 *           | lcd_ui          | 24     | 20KB   | LCD 主循环 (核心调度)     |
 *           | adc_key         | 23     | 2KB    | ADC 按键扫描 (20ms周期)   |
 *           | rtc_task        | 25     | 2KB    | DS3231 RTC 时钟读写       |
 *           | udp_server_task | 6      | 10KB   | WiFi UDP 传感器数据接收   |
 *           | voice_uart_task | 24     | 8KB    | UART2 语音模块指令解析    |
 *           | mqtt_cloud      | 7      | 12KB   | 华为 IoTDA MQTT 云连接    |
 *
 * 参    数: 无
 * 返 回 值: 无
 ***************************************************************/
void app_example_init(void)
{
    system_sync_init();

    /* 使用系统启动 Tick 计数作为随机数种子, 避免每次上电动画序列完全相同
       (Tick 值受上电时序/外设初始化耗时影响, 每次启动有一定差异) */
    srand((unsigned int)LOS_TickCountGet());

    unsigned int tid1, tid2, tid3, tid4, tid5, tid6;
    TSK_INIT_PARAM_S t = {0};

    /* 线程 1: LCD 主循环 */
    t.pfnTaskEntry = (TSK_ENTRY_FUNC)lcd_process;
    t.uwStackSize = 20480;
    t.pcName = "lcd_ui";
    t.usTaskPrio = 24;
    LOS_TaskCreate(&tid1, &t);

    /* 线程 2: ADC 按键扫描 */
    t.pfnTaskEntry = (TSK_ENTRY_FUNC)adc_process;
    t.uwStackSize = 2048;
    t.pcName = "adc_key";
    t.usTaskPrio = 23;
    LOS_TaskCreate(&tid2, &t);

    /* 线程 3: RTC 时钟读写与同步 */
    t.pfnTaskEntry = (TSK_ENTRY_FUNC)rtc_process;
    t.uwStackSize = 2048;
    t.pcName = "rtc_task";
    t.usTaskPrio = 25;
    LOS_TaskCreate(&tid3, &t);

    /* 线程 4: WiFi UDP 服务器 (端口 6666) */
    t.pfnTaskEntry = (TSK_ENTRY_FUNC)udp_server_thread;
    t.uwStackSize = 10240;
    t.pcName = "udp_server_task";
    t.usTaskPrio = 6;
    LOS_TaskCreate(&tid4, &t);

    /* 线程 5: 语音模块 UART 指令解析 */
    t.pfnTaskEntry = (TSK_ENTRY_FUNC)voice_uart_thread;
    t.uwStackSize = 8192; // 增加到 8KB, 防止 printf/sprintf 栈溢出
    t.pcName = "voice_uart_task";
    t.usTaskPrio = 24;
    LOS_TaskCreate(&tid5, &t);

    /* 线程 6: 华为 IoTDA MQTT 云连接与属性上报 */
    t.pfnTaskEntry = (TSK_ENTRY_FUNC)mqtt_cloud_thread;
    t.uwStackSize = 12288;
    t.pcName = "mqtt_cloud";
    t.usTaskPrio = 7;
    LOS_TaskCreate(&tid6, &t);
}

APP_FEATURE_INIT(app_example_init);
