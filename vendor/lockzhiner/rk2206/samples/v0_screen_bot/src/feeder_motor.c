#include "feeder_motor.h"
#include "lz_hardware.h"
#include <stdio.h>

// 根据引脚复用表：PC6 对应 PWM6_M0，复用功能为 MUX_FUNC2
#define FEEDER_MOTOR_PORT 6
#define FEEDER_MOTOR_PIN  GPIO0_PC6

static DevIo m_motor_io = {
    .isr =   {.gpio = INVALID_GPIO},
    .rst =   {.gpio = INVALID_GPIO},
    .ctrl1 = {.gpio = FEEDER_MOTOR_PIN, .func = MUX_FUNC2, .type = PULL_NONE, .drv = DRIVE_KEEP, .dir = LZGPIO_DIR_KEEP, .val = LZGPIO_LEVEL_KEEP},
    .ctrl2 = {.gpio = INVALID_GPIO},
};

void feeder_motor_init(void)
{
    unsigned int ret;
    DevIoInit(m_motor_io);
    ret = LzPwmInit(FEEDER_MOTOR_PORT);
    if (ret != LZ_HARDWARE_SUCCESS) {
        printf("[错误] 喂食电机 PWM 初始化失败！\n");
    }
}

// 速度控制：传入 0~100 的百分比
void feeder_motor_start(uint8_t speed_percent)
{
    if (speed_percent > 100) speed_percent = 100;
    
    // 0% 速度直接视为停机
    if (speed_percent == 0) {
        LzPwmStart(FEEDER_MOTOR_PORT, 0, 1000000);
        return;
    }

    // 设定 PWM 周期为 1,000,000 纳秒 (1kHz)，适合绝大多数直流电机
    uint32_t period_ns = 1000000; 
    uint32_t duty_ns = (period_ns / 100) * speed_percent;
    
    LzPwmStart(FEEDER_MOTOR_PORT, duty_ns, period_ns);
}