/***************************************************************
 * 文件名: eyes_emotion.c
 * 说    明: 表情动画引擎 + UI 绘制组件
 *           包含灵动电子眼待机动画、逗猫模式仿生猎物模拟(舵机联动)、
 *           喂食 8 帧专属动画三大动画系统, 以及时间状态栏绘制工具。
 *
 *           核心机制:
 *           - delay_with_break(): 可打断延时函数, 在动画帧间检测按键
 *             和页面切换, 同时驱动喂食倒计时
 *           - eyes_play_normal_step(): 单帧待机动画, 随机选择看左/看右/
 *             眨眼/彩蛋连招四种行为
 *           - eyes_play_cat_step(): 单帧逗猫动画, 三种行为(装死20% /
 *             试探抖动40% / 突进跳跃40%) + 眼神联动
 *           - eyes_play_feeding_step(): 单帧喂食动画, 顺序播放 8 帧
 ***************************************************************/

#include "eyes_emotion.h"
#include "menu_ui.h" 
#include "adc_key.h"
#include "lcd.h"
#include "los_task.h"
#include <stdlib.h> 
#include <stdio.h>
#include "ds3231.h" 
#include "servo_drive.h"
#include "feeder_motor.h"

extern const unsigned char gImage_normal_1[];
extern const unsigned char gImage_normal_2[];
extern const unsigned char gImage_normal_3[];
extern const unsigned char gImage_face_cat_1[];
extern const unsigned char gImage_face_cat_2[];
extern const unsigned char gImage_face_cat_3[];
extern const unsigned char gImage_feed_phys_f1[];
extern const unsigned char gImage_feed_phys_f2[];
extern const unsigned char gImage_feed_phys_f3[];
extern const unsigned char gImage_feed_phys_f4[];
extern const unsigned char gImage_feed_phys_f5[];
extern const unsigned char gImage_feed_phys_f6[];
extern const unsigned char gImage_feed_phys_f7[];
extern const unsigned char gImage_feed_phys_f8[];

extern const unsigned char gImage_cyber_eye_00[];
extern const unsigned char gImage_cyber_eye_01[];
//extern const unsigned char gImage_cyber_eye_02[];
extern const unsigned char gImage_cyber_eye_03[];
//extern const unsigned char gImage_cyber_eye_04[];
extern const unsigned char gImage_cyber_eye_05[];
extern const unsigned char gImage_cyber_eye_06[];
extern const unsigned char gImage_cyber_eye_07[];
extern const unsigned char gImage_cyber_eye_08[];
//extern const unsigned char gImage_cyber_eye_09[];
extern const unsigned char gImage_cyber_eye_10[];

extern volatile KeyCode g_current_key; 
extern volatile SystemPage g_sys_page;
/* g_feeding_countdown 已由 menu_ui.h 统一导出 */

static uint8_t last_draw_sec = 99; 

void ui_update_time_only(void);

/***************************************************************
 * UI 绘制组件
 ***************************************************************/
void ui_update_time_only(void)
{
    char time_str[24];
    RTC_Time t;
    rtc_time_read_safe(&t);
    sprintf(time_str, "%04d-%02d-%02d %02d:%02d:%02d",
            t.year, t.month, t.day,
            t.hour, t.minute, t.second);

    lcd_show_string(5, 10, (const uint8_t *)time_str, LCD_WHITE, LCD_BLACK, 16, 0);
}

void ui_draw_status_bar(void)
{
    last_draw_sec = 99; 
    ui_update_time_only();
    
    lcd_show_string(240, 10, (const uint8_t *)"WiFi:ON ", LCD_LIGHTBLUE, LCD_BLACK, 16, 0);
    lcd_draw_line(0, 35, 320, 35, LCD_CYAN); 
}

void ui_draw_footer(void)
{
    lcd_show_string(20, 215, (const uint8_t *)"Press any key to enter menu...", LCD_GRAY, LCD_BLACK, 16, 0);
}

/***************************************************************
 * 函数名称: delay_with_break
 * 说    明: 可打断的毫秒级延时函数 — 表情动画引擎的核心调度器
 *           在延时期间以 5ms 粒度轮询检测:
 *           1. 按键中断 (g_current_key != KEY_NONE)
 *           2. 页面切换 (g_sys_page != expected_page)
 *           3. 状态栏时间刷新 (每秒更新)
 *           4. 喂食倒计时驱动 (PAGE_SUB_FEEDING 页专属)
 * 参    数:
 *       @ms:            延时总时长 (毫秒)
 *       @expected_page: 预期的当前页面, 用于检测页面切换
 * 返 回 值: 0=延时正常结束, 1=被按键或页面切换打断
 ***************************************************************/
static int delay_with_break(int ms, SystemPage expected_page)
{
    int step = 5; 
    int slices = ms / step;

    for (int i = 0; i < slices; i++) {
        if (g_current_key != KEY_NONE || g_sys_page != expected_page) return 1; 
        
        if (g_sys_page == PAGE_IDLE || g_sys_page == PAGE_CAT_MODE || g_sys_page == PAGE_SUB_FEEDING) {
            /* 线程安全读取 RTC 秒数, 避免撕裂读 */
            RTC_Time t;
            rtc_time_read_safe(&t);
            if (t.second != last_draw_sec) {
                last_draw_sec = t.second;
                ui_update_time_only();
            }
        }

        if (g_sys_page == PAGE_SUB_FEEDING && g_feeding_countdown > 0) {
            g_feeding_countdown -= step;
            
            if (g_feeding_countdown % 500 == 0 || g_feeding_countdown <= 0) {
                char time_str[32];
                // 倒计时放在 y=215 的底部安全区，x=110 居中显示
                sprintf(time_str, "Time Left: %d s  ", (g_feeding_countdown / 1000) + 1);
                lcd_show_string(110, 215, (const uint8_t *)time_str, LCD_YELLOW, LCD_BLACK, 16, 0);
            }

            if (g_feeding_countdown <= 0) {
                feeder_motor_control(0);    // 因引入电机驱动模块, 改为 GPIO 低电平关闭
                g_sys_page = PAGE_IDLE;     
                g_page_changed = 1;         
                return 1;                   
            }
        }
        LOS_Msleep(step);
    }
    return (g_current_key != KEY_NONE || g_sys_page != expected_page); 
}

/***************************************************************
 * 函数名称: eyes_play_normal_step
 * 说    明: 播放一帧灵动待机动画 (单帧, 由 lcd_ui 主循环反复调用)
 *           动画流程: 居中待机(1~3s随机) → 随机选择行为:
 *           - 45%: 向右看 + 眨眼 (眼珠右移 → 闭眼 → 睁眼看右)
 *           - 45%: 向左看 + 眨眼 (眼珠左移 → 闭眼 → 睁眼看左)
 *           - 10%: 彩蛋连招 (右看→眨眼→睁眼→左看→眨眼, 快速切换)
 *           动画期间舵机自动回中(90°), 可通过按键或页面切换随时打断
 * 参    数: 无
 * 返 回 值: 无
 ***************************************************************/
void eyes_play_normal_step(void)
{
    if (g_sys_page != PAGE_IDLE) return;

    servo_set_angle(SERVO_X_PWM_PORT, 90);
    servo_set_angle(SERVO_Y_PWM_PORT, 90);

    // 1. 居中待机
    lcd_show_picture(60, 40, 200, 160, gImage_cyber_eye_00);
    
    int idle_time = 1000 + (rand() % 2000); 
    if (delay_with_break(idle_time, PAGE_IDLE)) return;

    int action_rand = rand() % 100;

    if (action_rand < 45) 
    {
        // 2. 向右看并眨眼
        lcd_show_picture(60, 40, 200, 160, gImage_cyber_eye_01); 
        if (delay_with_break(10, PAGE_IDLE)) return;
        
        lcd_show_picture(60, 40, 200, 160, gImage_cyber_eye_03); 
        if (delay_with_break(60, PAGE_IDLE)) return;
        
        lcd_show_picture(60, 40, 200, 160, gImage_cyber_eye_05); 
        if (delay_with_break(10, PAGE_IDLE)) return;

        lcd_show_picture(60, 40, 200, 160, gImage_cyber_eye_06); 
        if (delay_with_break(400 + (rand() % 300), PAGE_IDLE)) return; 
    }
    else if (action_rand < 90) 
    {
        // 3. 快速向左看并眨眼
        lcd_show_picture(60, 40, 200, 160, gImage_cyber_eye_07); 
        if (delay_with_break(5, PAGE_IDLE)) return; 
        
        lcd_show_picture(60, 40, 200, 160, gImage_cyber_eye_08); 
        if (delay_with_break(60, PAGE_IDLE)) return; 
        
        lcd_show_picture(60, 40, 200, 160, gImage_cyber_eye_10); 
        if (delay_with_break(500 + (rand() % 400), PAGE_IDLE)) return; 
    }
    else 
    {
        // 4. 小概率彩蛋连招
        lcd_show_picture(60, 40, 200, 160, gImage_cyber_eye_01); 
        if (delay_with_break(5, PAGE_IDLE)) return;
        lcd_show_picture(60, 40, 200, 160, gImage_cyber_eye_03); 
        if (delay_with_break(40, PAGE_IDLE)) return;
        lcd_show_picture(60, 40, 200, 160, gImage_cyber_eye_05); 
        if (delay_with_break(5, PAGE_IDLE)) return;

        lcd_show_picture(60, 40, 200, 160, gImage_cyber_eye_06); 
        if (delay_with_break(60, PAGE_IDLE)) return; 

        lcd_show_picture(60, 40, 200, 160, gImage_cyber_eye_07); 
        if (delay_with_break(5, PAGE_IDLE)) return;
        lcd_show_picture(60, 40, 200, 160, gImage_cyber_eye_08); 
        if (delay_with_break(5, PAGE_IDLE)) return;
        
        lcd_show_picture(60, 40, 200, 160, gImage_cyber_eye_10); 
        if (delay_with_break(800, PAGE_IDLE)) return;
    }
}

/***************************************************************
 * 函数名称: eyes_play_cat_step
 * 说    明: 播放一帧逗猫模式动画 (仿生猎物模拟 + 屏幕眼神联动)
 *           通过双轴舵机控制激光红点在地板有效区域内运动,
 *           屏幕猫眼表情实时跟踪激光位置 (左/中/右)。
 *           三种随机行为:
 *           - 20% 装死: 静止 1~3 秒
 *           - 40% 试探: 在当前位置 ±8° 内小幅抖动 3~6 次
 *           - 40% 突进: 跳跃到随机新位置, 眼神同步更新
 *           使用 static 变量记录激光位置, 保证动作连续性
 * 参    数: 无
 * 返 回 值: 无
 ***************************************************************/
// 使用静态变量记录上一次激光的位置，防止动作割裂
static int current_laser_x = 90;
static int current_laser_y = 120; // 默认看向前方地板

void eyes_play_cat_step(void)
{
    if (g_sys_page != PAGE_CAT_MODE) return;

    // 1. 定义地板有效互动边界 (防撞底盘，防丢失)
    const int X_MIN = 40;  
    const int X_MAX = 140; 
    const int Y_MIN = 110; 
    const int Y_MAX = 150; 

    // 2. 生成 0-99 随机数决定下一个状态
    int action = rand() % 100;

    // --- 眼神联动核心逻辑 ---
    // 根据当前 X 轴的位置，决定屏幕上的猫眼朝哪个方向看
    const unsigned char* cat_face = gImage_face_cat_1; // 默认看中间
    if (current_laser_x < 80) {
        cat_face = gImage_face_cat_2; // 激光在左，眼神看左
    } else if (current_laser_x > 100) {
        cat_face = gImage_face_cat_3; // 激光在右，眼神看右
    }
    // 动作开始前，先刷新屏幕表情
    lcd_show_picture(60, 40, 200, 160, cat_face);

    // 3. 状态机执行
    if (action < 20) 
    {
        // ---------------------------------
        // 动作 A: 【装死 / 隐蔽】 (20% 概率)
        // ---------------------------------
        int freeze_time = 1000 + rand() % 2000; // 停顿 1 到 3 秒
        if (delay_with_break(freeze_time, PAGE_CAT_MODE)) return;
    }
    else if (action < 60) 
    {
        // ---------------------------------
        // 动作 B: 【微动 / 试探】 (40% 概率)
        // ---------------------------------
        int twitch_count = 3 + rand() % 4; // 抖动 3-6 次
        for (int i = 0; i < twitch_count; i++) 
        {
            // 在当前位置 ±8° 范围内小幅度徘徊
            int nx = current_laser_x + (rand() % 16 - 8);
            int ny = current_laser_y + (rand() % 16 - 8);
            
            // 越界保护
            if (nx < X_MIN) nx = X_MIN;
            if (nx > X_MAX) nx = X_MAX;
            if (ny < Y_MIN) ny = Y_MIN;
            if (ny > Y_MAX) ny = Y_MAX;

            servo_set_angle(SERVO_X_PWM_PORT, nx);
            servo_set_angle(SERVO_Y_PWM_PORT, ny);
            
            // 高频抖动
            if (delay_with_break(80 + rand() % 150, PAGE_CAT_MODE)) return;
        }
    }
    else 
    {
        // ---------------------------------
        // 动作 C: 【突进 / 逃窜】 (40% 概率)
        // ---------------------------------
        // 在全局边界内随机生成一个新的跳跃点
        current_laser_x = X_MIN + rand() % (X_MAX - X_MIN + 1);
        current_laser_y = Y_MIN + rand() % (Y_MAX - Y_MIN + 1);

        servo_set_angle(SERVO_X_PWM_PORT, current_laser_x);
        servo_set_angle(SERVO_Y_PWM_PORT, current_laser_y);

        // 激光突进后，屏幕眼神必须立刻跟上激光的新位置！
        if (current_laser_x < 80) {
            cat_face = gImage_face_cat_2;
        } else if (current_laser_x > 100) {
            cat_face = gImage_face_cat_3;
        } else {
            cat_face = gImage_face_cat_1;
        }
        lcd_show_picture(60, 40, 200, 160, cat_face);

        // 突进跑动后，通常会有稍长一点的喘息时间
        if (delay_with_break(300 + rand() % 500, PAGE_CAT_MODE)) return;
    }
}

/***************************************************************
 * 函数名称: eyes_play_feeding_step
 * 说    明: 播放一帧喂食专属 8 帧动画序列
 *           从 f1 到 f8 逐帧播放, 帧间通过 delay_with_break 延时,
 *           同时驱动喂食倒计时和状态栏刷新。
 *           当倒计时归零时, delay_with_break 自动停止电机并切回待机页。
 * 参    数: 无
 * 返 回 值: 无
 ***************************************************************/
void eyes_play_feeding_step(void)
{
    if (g_sys_page != PAGE_SUB_FEEDING) return;

    lcd_show_picture(60, 45, 200, 160, gImage_feed_phys_f1);
    if (delay_with_break(200, PAGE_SUB_FEEDING)) return;

    lcd_show_picture(60, 45, 200, 160, gImage_feed_phys_f2);
    if (delay_with_break(100, PAGE_SUB_FEEDING)) return;

    lcd_show_picture(60, 45, 200, 160, gImage_feed_phys_f3);
    if (delay_with_break(100, PAGE_SUB_FEEDING)) return;

    lcd_show_picture(60, 45, 200, 160, gImage_feed_phys_f4);
    if (delay_with_break(80, PAGE_SUB_FEEDING)) return;

    lcd_show_picture(60, 45, 200, 160, gImage_feed_phys_f5);
    if (delay_with_break(120, PAGE_SUB_FEEDING)) return;

    lcd_show_picture(60, 45, 200, 160, gImage_feed_phys_f6);
    if (delay_with_break(100, PAGE_SUB_FEEDING)) return;

    lcd_show_picture(60, 45, 200, 160, gImage_feed_phys_f7);
    if (delay_with_break(100, PAGE_SUB_FEEDING)) return;

    lcd_show_picture(60, 45, 200, 160, gImage_feed_phys_f8);
    if (delay_with_break(1000, PAGE_SUB_FEEDING)) return;
}