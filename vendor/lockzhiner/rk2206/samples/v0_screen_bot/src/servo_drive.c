#include "servo_drive.h"
#include "lz_hardware.h"
#include <stdio.h>

#define PWM_PERIOD_NS     20000000  

/* ================== 将舵机路由到 PB4 和 PB5 ================== */
static DevIo m_pwm0_io = {
    .isr =   {.gpio = INVALID_GPIO},
    .rst =   {.gpio = INVALID_GPIO},
    // PB4 配置为 MUX_FUNC5，即 PWM0_M1
    .ctrl1 = {.gpio = GPIO0_PB4, .func = MUX_FUNC1, .type = PULL_NONE, .drv = DRIVE_KEEP, .dir = LZGPIO_DIR_KEEP, .val = LZGPIO_LEVEL_KEEP},
    .ctrl2 = {.gpio = INVALID_GPIO},
};

static DevIo m_pwm1_io = {
    .isr =   {.gpio = INVALID_GPIO},
    .rst =   {.gpio = INVALID_GPIO},
    // PB5 配置为 MUX_FUNC5，即 PWM1_M1
    .ctrl1 = {.gpio = GPIO0_PB5, .func = MUX_FUNC1, .type = PULL_NONE, .drv = DRIVE_KEEP, .dir = LZGPIO_DIR_KEEP, .val = LZGPIO_LEVEL_KEEP},
    .ctrl2 = {.gpio = INVALID_GPIO},
};

void servo_init(void)
{
    unsigned int ret;

    // 1. 初始化 X 轴舵机 (PB4)
    DevIoInit(m_pwm0_io);
    ret = LzPwmInit(SERVO_X_PWM_PORT);
    if (ret != LZ_HARDWARE_SUCCESS) {
        printf("Servo X (PB4) PWM Init Failed!\n");
    }

    // 2. 初始化 Y 轴舵机 (PB5)
    DevIoInit(m_pwm1_io);
    ret = LzPwmInit(SERVO_Y_PWM_PORT);
    if (ret != LZ_HARDWARE_SUCCESS) {
        printf("Servo Y (PB5) PWM Init Failed!\n");
    }

    // 默认居中
    servo_set_angle(SERVO_X_PWM_PORT, 90);
    servo_set_angle(SERVO_Y_PWM_PORT, 90);
}

void servo_set_angle(uint32_t port, uint8_t angle)
{
    if (angle > 180) angle = 180;
    uint32_t duty_ns = 500000 + (angle * 2000000 / 180);
    LzPwmStart(port, duty_ns, PWM_PERIOD_NS);
}