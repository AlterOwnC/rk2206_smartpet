#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "los_task.h"
#include "ohos_init.h"
#include "lcd.h"
#include "lz_hardware.h"

// ================== 配置区域 ==================
extern const unsigned char gImage_ball[]; // 图片数组

#define SCREEN_W  LCD_W
#define SCREEN_H  LCD_H
#define BG_COLOR  LCD_WHITE

#define BALL_IMG_W 16
#define BALL_IMG_H 16

// 灵敏度：数值越小，球移动越快（建议值 500-2000）
#define SENSITIVITY 1000 

// MPU6050 相关定义
#define I2C_BUS          0
#define MPU6050_ADDR     0x68
#define PWR_MGMT_1       0x6B
#define ACCEL_XOUT_H     0x3B

static I2cBusIo m_i2cBus = {
    .scl =  { .gpio = GPIO0_PA1, .func = MUX_FUNC3, .type = PULL_NONE, .drv = DRIVE_KEEP, .dir = LZGPIO_DIR_KEEP, .val = LZGPIO_LEVEL_KEEP },
    .sda =  { .gpio = GPIO0_PA0, .func = MUX_FUNC3, .type = PULL_NONE, .drv = DRIVE_KEEP, .dir = LZGPIO_DIR_KEEP, .val = LZGPIO_LEVEL_KEEP },
    .id = FUNC_ID_I2C0,
    .mode = FUNC_MODE_M2,
};

// ================== MPU6050 驱动函数 ==================

void mpu6050_init(void) 
{
    I2cIoInit(m_i2cBus);
    LzI2cInit(I2C_BUS, 400000);
    PinctrlSet(GPIO0_PA1, MUX_FUNC3, PULL_KEEP, DRIVE_KEEP);
    PinctrlSet(GPIO0_PA0, MUX_FUNC3, PULL_KEEP, DRIVE_KEEP);
    
    unsigned char send_data[2] = {PWR_MGMT_1, 0x00};
    LzI2cWrite(I2C_BUS, MPU6050_ADDR, send_data, 2);
    printf("MPU6050 Init OK!\n");
}

void mpu6050_read_accel(short *x, short *y, short *z) 
{
    unsigned char reg_addr = ACCEL_XOUT_H;
    unsigned char read_buf[6] = {0};
    LzI2cWrite(I2C_BUS, MPU6050_ADDR, &reg_addr, 1);
    LzI2cRead(I2C_BUS, MPU6050_ADDR, read_buf, 6);
    *x = (short)((read_buf[0] << 8) | read_buf[1]);
    *y = (short)((read_buf[2] << 8) | read_buf[3]);
    *z = (short)((read_buf[4] << 8) | read_buf[5]);
}

// ================== 游戏主任务 ==================

void gravity_ball_process(void *arg)
{
    lcd_init();
    mpu6050_init();

    float ball_x = (SCREEN_W - BALL_IMG_W) / 2.0f;
    float ball_y = (SCREEN_H - BALL_IMG_H) / 2.0f;
    float vx = 0, vy = 0; // 引入速度变量
    int old_x = (int)ball_x, old_y = (int)ball_y;

    // 只在开始时清一次全屏
    lcd_fill(0, 0, SCREEN_W, SCREEN_H, BG_COLOR);

    while (1)
    {
        short ax, ay, az;
        mpu6050_read_accel(&ax, &ay, &az);

        // 1. 物理模型计算 (加入摩擦力 0.9 让球能停下来)
        // 注意：这里的 ax, ay 符号请根据你的测试结果自行调整
        vx = (vx + (float)ay / 1000.0f) * 0.9f; 
        vy = (vy + (float)ax / 1000.0f) * 0.9f;

        ball_x += vx;
        ball_y += vy;

        // 2. 边界碰撞检测
        if (ball_x < 0) { ball_x = 0; vx = -vx * 0.5f; } // 撞墙反弹
        if (ball_x > SCREEN_W - BALL_IMG_W) { ball_x = SCREEN_W - BALL_IMG_W; vx = -vx * 0.5f; }
        if (ball_y < 0) { ball_y = 0; vy = -vy * 0.5f; }
        if (ball_y > SCREEN_H - BALL_IMG_H) { ball_y = SCREEN_H - BALL_IMG_H; vy = -vy * 0.5f; }

        // 3. 局部渲染逻辑：仅当像素发生位移时才重绘
        int cur_x = (int)ball_x;
        int cur_y = (int)ball_y;

        if (cur_x != old_x || cur_y != old_y)
        {
            // 第一步：用背景色“擦除”旧球区域
            // 只刷 16x16 的区域，速度极快，完全不会闪烁
            lcd_fill(old_x, old_y, old_x + BALL_IMG_W, old_y + BALL_IMG_H, BG_COLOR);
            
            // 第二步：在坐标新位置画球
            lcd_show_picture(cur_x, cur_y, BALL_IMG_W, BALL_IMG_H, gImage_ball);
            
            old_x = cur_x;
            old_y = cur_y;
        }

        LOS_Msleep(30); // 约 60 帧的刷新率
    }
}

// ================== 任务启动 ==================

void start_gravity_ball(void)
{
    unsigned int thread_id;
    TSK_INIT_PARAM_S task = {0};

    task.pfnTaskEntry = (TSK_ENTRY_FUNC)gravity_ball_process;
    task.uwStackSize = 20480;
    task.pcName = "gravity_ball_task";
    task.usTaskPrio = 24;
    
    if (LOS_TaskCreate(&thread_id, &task) != LOS_OK) {
        printf("Failed to create gravity ball task\n");
    }
}

APP_FEATURE_INIT(start_gravity_ball);