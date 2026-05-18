#include "ds3231.h"
#include "lz_hardware.h"
#include "command_queue.h"
#include <stdio.h>

#define DS3231_I2C_ADDR 0x68 
#define I2C_BUS_PORT    1    // 必须是 I2C1

#define RTC_FLAG_REG    0x07 // 借用闹钟1的秒钟寄存器作为标志位
#define RTC_MAGIC_FLAG  0xAA // 记忆标志符号

// 全局时间变量，供 UI 读取
volatile RTC_Time g_rtc_time = {2026, 4, 16, 12, 0, 0}; 

static DevIo m_i2c1_sda = {
    .isr =   {.gpio = INVALID_GPIO},
    .rst =   {.gpio = INVALID_GPIO},
    .ctrl1 = {.gpio = GPIO0_PB6, .func = MUX_FUNC4, .type = PULL_UP, .drv = DRIVE_KEEP, .dir = LZGPIO_DIR_KEEP, .val = LZGPIO_LEVEL_KEEP},
    .ctrl2 = {.gpio = INVALID_GPIO},
};

static DevIo m_i2c1_scl = {
    .isr =   {.gpio = INVALID_GPIO},
    .rst =   {.gpio = INVALID_GPIO},
    .ctrl1 = {.gpio = GPIO0_PB7, .func = MUX_FUNC4, .type = PULL_UP, .drv = DRIVE_KEEP, .dir = LZGPIO_DIR_KEEP, .val = LZGPIO_LEVEL_KEEP},
    .ctrl2 = {.gpio = INVALID_GPIO},
};

// BCD 转换
static uint8_t bcd_to_dec(uint8_t val) { return ((val >> 4) * 10) + (val & 0x0F); }
static uint8_t dec_to_bcd(uint8_t val) { return ((val / 10) << 4) | (val % 10); }

// 封装一个安全的单字节写入函数
static void ds3231_write_reg(uint8_t reg, uint8_t val) {
    uint8_t data[2] = {reg, val};
    // 强制发送 2 个字节：第一个是寄存器地址，第二个是数据
    LzI2cWrite(I2C_BUS_PORT, DS3231_I2C_ADDR, data, 2);
}

// 封装一个安全的单字节读取函数
static uint8_t ds3231_read_reg(uint8_t reg) {
    uint8_t val = 0;
    // 先写寄存器地址，再读数据
    if (LzI2cWrite(I2C_BUS_PORT, DS3231_I2C_ADDR, &reg, 1) == LZ_HARDWARE_SUCCESS) {
        LzI2cRead(I2C_BUS_PORT, DS3231_I2C_ADDR, &val, 1);
    }
    return val;
}

void ds3231_set_datetime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second)
{
    // 逐个寄存器安全写入，绝不错位！
    ds3231_write_reg(0x00, dec_to_bcd(second));
    ds3231_write_reg(0x01, dec_to_bcd(minute));
    ds3231_write_reg(0x02, dec_to_bcd(hour));
    ds3231_write_reg(0x03, 0x01); // 星期几
    ds3231_write_reg(0x04, dec_to_bcd(day));
    ds3231_write_reg(0x05, dec_to_bcd(month));
    ds3231_write_reg(0x06, dec_to_bcd(year % 100));
    
    printf("[RTC] Time SET: %04d-%02d-%02d %02d:%02d:%02d\n", year, month, day, hour, minute, second);
}

void ds3231_init(void)
{
    DevIoInit(m_i2c1_sda);
    DevIoInit(m_i2c1_scl);
    LzI2cInit(I2C_BUS_PORT, 100000); 
    
    // 安全读取标志位
    uint8_t flag_val = ds3231_read_reg(RTC_FLAG_REG);

    if (flag_val != RTC_MAGIC_FLAG) 
    {
        printf("\n[RTC] First boot (Flag=0x%02X). Setting default time...\n", flag_val);
        ds3231_set_datetime(2026, 4, 16, 12, 0, 0);
        
        // 安全写入标志位
        ds3231_write_reg(RTC_FLAG_REG, RTC_MAGIC_FLAG);
    }
    else
    {
        printf("\n[RTC] Magic flag (0xAA) found. Time restored.\n");
    }
}

void ds3231_get_datetime(RTC_Time *time)
{
    uint8_t reg = 0x00;
    uint8_t data[7] = {0};

    if (LzI2cWrite(I2C_BUS_PORT, DS3231_I2C_ADDR, &reg, 1) == LZ_HARDWARE_SUCCESS)
    {
        if (LzI2cRead(I2C_BUS_PORT, DS3231_I2C_ADDR, data, 7) == LZ_HARDWARE_SUCCESS)
        {
            time->second = bcd_to_dec(data[0] & 0x7F);
            time->minute = bcd_to_dec(data[1] & 0x7F);
            time->hour   = bcd_to_dec(data[2] & 0x3F);
            time->day    = bcd_to_dec(data[4] & 0x3F);
            time->month  = bcd_to_dec(data[5] & 0x1F);
            time->year   = bcd_to_dec(data[6]) + 2000;
        }
    }
}

void rtc_time_read_safe(RTC_Time *out)
{
    osMutexAcquire(g_rtc_mutex, osWaitForever);
    *out = g_rtc_time;
    osMutexRelease(g_rtc_mutex);
}