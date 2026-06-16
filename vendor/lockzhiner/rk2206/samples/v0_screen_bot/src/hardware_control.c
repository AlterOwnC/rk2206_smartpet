#include "hardware_control.h"
#include "lz_hardware.h"
#include <stdio.h>

// 彻底正名：通晓开发板的引脚全部属于 GPIO0 组！
#define LASER_PIN  GPIO0_PC4  // 激光 D0
#define PUMP_PIN   GPIO0_PB0  // 水泵 B0
#define FAN_PIN    GPIO0_PB1  // 风扇 B1

/* GPIO 有效电平语义宏 (消除 on=1 与高/低电平的歧义)
   激光: 低电平触发 (PC4 低=亮)
   水泵: 高电平触发 (PB0 高=开, 因引入电机驱动模块)
   风扇: 低电平触发 (PB1 低=开, 继电器逻辑)
   喂食: 高电平触发 (PC6 高=开, 见 feeder_motor.c) */
#define LASER_ON_LEVEL   LZGPIO_LEVEL_LOW
#define LASER_OFF_LEVEL  LZGPIO_LEVEL_HIGH
#define PUMP_ON_LEVEL    LZGPIO_LEVEL_HIGH
#define PUMP_OFF_LEVEL   LZGPIO_LEVEL_LOW
#define FAN_ON_LEVEL     LZGPIO_LEVEL_LOW
#define FAN_OFF_LEVEL    LZGPIO_LEVEL_HIGH
#define MOTOR_ON_LEVEL   LZGPIO_LEVEL_HIGH
#define MOTOR_OFF_LEVEL  LZGPIO_LEVEL_LOW

int g_pump_state = 0;
int g_fan_state = 0;
volatile int g_fan_manual_override = 0;
volatile int g_pump_manual_override = 0;

/* ================== 完整结构体配置 (拒绝隐式赋0陷阱) ================== */
static DevIo m_laser_io = { 
    .isr = {.gpio = INVALID_GPIO}, .rst = {.gpio = INVALID_GPIO}, 
    .ctrl1 = {.gpio = LASER_PIN, .func = MUX_FUNC0, .type = PULL_NONE, .drv = DRIVE_KEEP, .dir = LZGPIO_DIR_KEEP, .val = LZGPIO_LEVEL_KEEP}, 
    .ctrl2 = {.gpio = INVALID_GPIO} 
};

static DevIo m_pump_io  = { 
    .isr = {.gpio = INVALID_GPIO}, .rst = {.gpio = INVALID_GPIO}, 
    .ctrl1 = {.gpio = PUMP_PIN,  .func = MUX_FUNC0, .type = PULL_NONE, .drv = DRIVE_KEEP, .dir = LZGPIO_DIR_KEEP, .val = LZGPIO_LEVEL_KEEP}, 
    .ctrl2 = {.gpio = INVALID_GPIO} 
};

static DevIo m_fan_io   = { 
    .isr = {.gpio = INVALID_GPIO}, .rst = {.gpio = INVALID_GPIO}, 
    .ctrl1 = {.gpio = FAN_PIN,   .func = MUX_FUNC0, .type = PULL_NONE, .drv = DRIVE_KEEP, .dir = LZGPIO_DIR_KEEP, .val = LZGPIO_LEVEL_KEEP}, 
    .ctrl2 = {.gpio = INVALID_GPIO} 
};

void devices_init(void)
{
    // 1. 初始化激光
    DevIoInit(m_laser_io);
    LzGpioInit(LASER_PIN);
    LzGpioSetDir(LASER_PIN, LZGPIO_DIR_OUT);
    LzGpioSetVal(LASER_PIN, LZGPIO_LEVEL_HIGH); // 默认高电平(关)

    // 2. 初始化水泵 (B0) — 因引入电机驱动模块, 改为高电平触发
    DevIoInit(m_pump_io);
    LzGpioInit(PUMP_PIN);
    LzGpioSetDir(PUMP_PIN, LZGPIO_DIR_OUT);
    LzGpioSetVal(PUMP_PIN, LZGPIO_LEVEL_LOW); // 默认低电平(关)

    // 3. 初始化风扇 (B1)
    DevIoInit(m_fan_io);
    LzGpioInit(FAN_PIN);
    LzGpioSetDir(FAN_PIN, LZGPIO_DIR_OUT);
    LzGpioSetVal(FAN_PIN, LZGPIO_LEVEL_HIGH); // 默认高电平(关)

    printf("Hardware devices (Laser, Pump, Fan) initialized successfully.\n");
}

// ================== 动作执行 ==================
// 激光: 低电平触发 (PC4, 低电平亮)
void laser_control(int on) {
    LzGpioSetVal(LASER_PIN, on ? LZGPIO_LEVEL_LOW : LZGPIO_LEVEL_HIGH);
}

// 水泵: 高电平开启 (PB0, 因引入电机驱动模块改为高电平触发)
void pump_control(int on) {
    g_pump_state = on; // 同步记录状态
    LzGpioSetVal(PUMP_PIN, on ? LZGPIO_LEVEL_HIGH : LZGPIO_LEVEL_LOW);
}

// 风扇: 低电平触发继电器 (PB1)
void fan_control(int on) {
    g_fan_state = on; // 同步记录状态
    LzGpioSetVal(FAN_PIN, on ? LZGPIO_LEVEL_LOW : LZGPIO_LEVEL_HIGH);
}