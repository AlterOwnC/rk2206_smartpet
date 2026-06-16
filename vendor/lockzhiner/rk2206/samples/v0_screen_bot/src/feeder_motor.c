#include "feeder_motor.h"
#include "lz_hardware.h"
#include <stdio.h>

/* 因引入电机驱动模块, PC6 由 PWM6_M0(MUX_FUNC2) 改为普通 GPIO(MUX_FUNC0) */
#define FEEDER_MOTOR_PIN  GPIO0_PC6

static DevIo m_motor_io = {
    .isr =   {.gpio = INVALID_GPIO},
    .rst =   {.gpio = INVALID_GPIO},
    .ctrl1 = {.gpio = FEEDER_MOTOR_PIN, .func = MUX_FUNC0, .type = PULL_NONE, .drv = DRIVE_KEEP, .dir = LZGPIO_DIR_KEEP, .val = LZGPIO_LEVEL_KEEP},
    .ctrl2 = {.gpio = INVALID_GPIO},
};

/***************************************************************
 * 函数名称: feeder_motor_init
 * 说    明: 初始化喂食电机为 GPIO 输出模式
 *           因引入电机驱动模块而弃用 PWM, 改为 GPIO 高电平触发
 *           默认状态为低电平(关闭)
 * 参    数: 无
 * 返 回 值: 无
 ***************************************************************/
void feeder_motor_init(void)
{
    DevIoInit(m_motor_io);
    LzGpioInit(FEEDER_MOTOR_PIN);
    LzGpioSetDir(FEEDER_MOTOR_PIN, LZGPIO_DIR_OUT);
    LzGpioSetVal(FEEDER_MOTOR_PIN, LZGPIO_LEVEL_LOW); // 默认低电平(关)
    printf("[Motor] Feeder motor GPIO init OK (Pin=PC6, High-active).\n");
}

/***************************************************************
 * 函数名称: feeder_motor_control
 * 说    明: 喂食电机启停控制 — 与 pump_control 逻辑完全对齐
 *           HIGH = 开启电机, LOW = 关闭电机
 * 参    数:
 *       @on: 1=开启电机(高电平), 0=关闭电机(低电平)
 * 返 回 值: 无
 ***************************************************************/
void feeder_motor_control(int on)
{
    LzGpioSetVal(FEEDER_MOTOR_PIN, on ? LZGPIO_LEVEL_HIGH : LZGPIO_LEVEL_LOW);
}
