#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include "los_task.h"
#include "ohos_init.h"
#include "lz_hardware.h"

/* 定义ADC的通道号 (通常是 5，如果没反应我们等下可以试着换成 4) */
#define ADC_CHANNEL         5

/* [修复 1]：将 .func 改为 MUX_FUNC0，让引脚回归模拟 ADC 模式 */
static DevIo m_adcKey = {
    .isr =   {.gpio = INVALID_GPIO},
    .rst =   {.gpio = INVALID_GPIO},
    .ctrl1 = {.gpio = GPIO0_PC5, .func = MUX_FUNC0, .type = PULL_NONE, .drv = DRIVE_KEEP, .dir = LZGPIO_DIR_KEEP, .val = LZGPIO_LEVEL_KEEP},
    .ctrl2 = {.gpio = INVALID_GPIO},
};

/***************************************************************
* 函数名称: turn_off_rgb_led
* 说    明: 强行拉低 RGB 灯的控制引脚，关闭烦人的红光
***************************************************************/
void turn_off_rgb_led()
{
    // 红色通常是 PB4, 绿色 PB5, 蓝色 PB6。统一拉低关闭三极管。
    LzGpioInit(GPIO0_PB4); LzGpioSetDir(GPIO0_PB4, LZGPIO_DIR_OUT); LzGpioSetVal(GPIO0_PB4, LZGPIO_LEVEL_LOW);
    LzGpioInit(GPIO0_PB5); LzGpioSetDir(GPIO0_PB5, LZGPIO_DIR_OUT); LzGpioSetVal(GPIO0_PB5, LZGPIO_LEVEL_LOW);
    LzGpioInit(GPIO0_PB6); LzGpioSetDir(GPIO0_PB6, LZGPIO_DIR_OUT); LzGpioSetVal(GPIO0_PB6, LZGPIO_LEVEL_LOW);
}

static unsigned int adc_dev_init()
{
    unsigned int ret = 0;
    uint32_t *pGrfSocCon29 = (uint32_t *)(0x41050000U + 0x274U);
    uint32_t ulValue;

    ret = DevIoInit(m_adcKey);
    if (ret != LZ_HARDWARE_SUCCESS) {
        printf("ADC Key IO Init fail\n");
        return __LINE__;
    }
    ret = LzSaradcInit();
    if (ret != LZ_HARDWARE_SUCCESS) {
        printf("ADC Init fail\n");
        return __LINE__;
    }

    ulValue = *pGrfSocCon29;
    ulValue &= ~(0x1 << 4);
    ulValue |= ((0x1 << 4) << 16);
    *pGrfSocCon29 = ulValue;
    
    return 0;
}

static float adc_get_voltage()
{
    unsigned int ret = LZ_HARDWARE_SUCCESS;
    unsigned int data = 0;

    ret = LzSaradcReadValue(ADC_CHANNEL, &data);
    if (ret != LZ_HARDWARE_SUCCESS) return 0.0;

    return (float)(data * 3.3 / 1024.0);
}

void print_process(void *arg)
{
    while (1) {
        printf("系统运行中...\r\n");
        LOS_Msleep(1000); 
    }
}

void adc_process(void *arg)
{
    float voltage;
    uint32_t count = 0;
    uint8_t key_pressed_flag = 0; 
    int debug_timer = 0; // 用于控制 debug 打印频率

    /* 1. 先把 RGB 灯关掉 */
    turn_off_rgb_led();

    /* 2. 初始化 ADC */
    adc_dev_init();
    
    while (1)
    {
        voltage = adc_get_voltage();

        /* [修复 2]：增加心跳 Debug 打印 (每 1 秒打印一次原始电压) */
        debug_timer++;
        if (debug_timer >= 50) { // 50 * 20ms = 1000ms = 1s
            printf("[Debug] 探针当前电压: %.2fV\r\n", voltage);
            debug_timer = 0;
        }

        /* 业务逻辑判断 */
        if (voltage < 3.0) 
        {
            if (key_pressed_flag == 0) 
            {
                count++;
                printf(">>>> 按键打断！ 计数[%u] 当前电压: %.2fV\r\n", count, voltage);
                key_pressed_flag = 1; 
            }
        }
        else 
        {
            key_pressed_flag = 0;
        }

        LOS_Msleep(20); 
    }
}

void adc_example()
{
    unsigned int thread_id1, thread_id2;
    TSK_INIT_PARAM_S task1 = {0}, task2 = {0};

    task1.pfnTaskEntry = (TSK_ENTRY_FUNC)print_process;
    task1.uwStackSize = 2048;
    task1.pcName = "print process";
    task1.usTaskPrio = 25;
    LOS_TaskCreate(&thread_id1, &task1);

    task2.pfnTaskEntry = (TSK_ENTRY_FUNC)adc_process;
    task2.uwStackSize = 2048;
    task2.pcName = "adc process";
    task2.usTaskPrio = 24;
    LOS_TaskCreate(&thread_id2, &task2);
}

APP_FEATURE_INIT(adc_example);