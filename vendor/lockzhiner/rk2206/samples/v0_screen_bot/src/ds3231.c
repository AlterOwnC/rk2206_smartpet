#include "ds3231.h"
#include "lz_hardware.h"
#include "command_queue.h"
#include "los_mux.h"
#include <stdio.h>

#define DS3231_I2C_ADDR 0x68 
#define I2C_BUS_PORT    1    // 必须是 I2C1

#define RTC_FLAG_REG    0x07 // 借用闹钟1的秒钟寄存器作为标志位
#define RTC_MAGIC_FLAG  0xAA // 记忆标志符号

// 全局时间变量，供 UI 读取
volatile RTC_Time g_rtc_time = {2026, 4, 16, 12, 0, 0};

/* I2C1 总线配置 — 使用官方标准 I2cBusIo 结构体 (参照 b11_i2c_scan 样例) */
static I2cBusIo m_i2c1_bus = {
    .scl =  {.gpio = GPIO0_PB7, .func = MUX_FUNC4, .type = PULL_UP, .drv = DRIVE_KEEP, .dir = LZGPIO_DIR_KEEP, .val = LZGPIO_LEVEL_KEEP},
    .sda =  {.gpio = GPIO0_PB6, .func = MUX_FUNC4, .type = PULL_UP, .drv = DRIVE_KEEP, .dir = LZGPIO_DIR_KEEP, .val = LZGPIO_LEVEL_KEEP},
    .id = FUNC_ID_I2C1,
    .mode = FUNC_MODE_M2,
};

static unsigned int m_i2c1_freq = 100000; // I2C1 时钟频率 100kHz

// BCD 转换
static uint8_t bcd_to_dec(uint8_t val) { return ((val >> 4) * 10) + (val & 0x0F); }
static uint8_t dec_to_bcd(uint8_t val) { return ((val / 10) << 4) | (val % 10); }

// 封装一个安全的单字节写入函数, 返回 0=成功, -1=失败
static int ds3231_write_reg(uint8_t reg, uint8_t val) {
    uint8_t data[2] = {reg, val};
    // 强制发送 2 个字节：第一个是寄存器地址，第二个是数据
    if (LzI2cWrite(I2C_BUS_PORT, DS3231_I2C_ADDR, data, 2) == LZ_HARDWARE_SUCCESS) {
        return 0;
    }
    printf("[RTC] I2C write failed at reg 0x%02X\n", reg);
    return -1;
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

int ds3231_set_datetime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second)
{
    int ret = 0;
    // 逐个寄存器安全写入，绝不错位！
    ret |= ds3231_write_reg(0x00, dec_to_bcd(second));  // 写 seconds 同时清除 OSF (bit7=0)
    ret |= ds3231_write_reg(0x01, dec_to_bcd(minute));
    ret |= ds3231_write_reg(0x02, dec_to_bcd(hour));
    ret |= ds3231_write_reg(0x03, 0x01); // 星期几
    ret |= ds3231_write_reg(0x04, dec_to_bcd(day));
    ret |= ds3231_write_reg(0x05, dec_to_bcd(month));
    ret |= ds3231_write_reg(0x06, dec_to_bcd(year % 100));

    printf("[RTC] Time SET: %04d-%02d-%02d %02d:%02d:%02d %s\n",
           year, month, day, hour, minute, second,
           ret ? "FAILED!" : "OK");
    return ret;
}

void ds3231_init(void)
{
    /* 使用 I2cBusIo 统一初始化 I2C1 总线 (替代分散的 DevIo 方式) */
    if (I2cIoInit(m_i2c1_bus) != LZ_HARDWARE_SUCCESS) {
        printf("[RTC] I2C1 I2cIoInit failed!\n");
        return;
    }
    if (LzI2cInit(I2C_BUS_PORT, m_i2c1_freq) != LZ_HARDWARE_SUCCESS) {
        printf("[RTC] I2C1 LzI2cInit failed!\n");
        return;
    }

    /* 显式配置引脚复用 (确保 SDA/SCL 正确映射到 I2C1 功能) */
    PinctrlSet(GPIO0_PB6, MUX_FUNC4, PULL_UP, DRIVE_KEEP);
    PinctrlSet(GPIO0_PB7, MUX_FUNC4, PULL_UP, DRIVE_KEEP);
    
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
        printf("\n[RTC] Magic flag (0xAA) found.\n");
    }

    /* 【关键修复】无论是否首次上电，都检查 OSF (Oscillator Stop Flag)
     * DS3231 断电后 OSF=1，振荡器停止，必须写 0 才能走秒！
     * 否则能读写寄存器但秒数永远不变。
     * 读取 Control/Status 寄存器 (0x0F) bit 7 判断 OSF */
    uint8_t osf_status = ds3231_read_reg(0x0F);
    if (osf_status & 0x80) {
        printf("[RTC] OSF (Oscillator Stop Flag) is SET — oscillator was stopped! Clearing...\n");
        ds3231_write_reg(0x0F, osf_status & 0x7F);  // 清 OSF，保留其他位 (EN32KHZ, BSY, A2F, A1F)
        printf("[RTC] OSF cleared, oscillator should be running now.\n");
    } else {
        printf("[RTC] OSF clear — oscillator is healthy.\n");
    }
}

void ds3231_get_datetime(RTC_Time *time)
{
    uint8_t reg = 0x00;
    uint8_t data[7] = {0};

    /* 使用 LzI2cReadReg 确保 REPEATED START (标准 DS3231 多字节读时序)
     * 避免分立的 LzI2cWrite+LzI2cRead 之间 STOP→START 导致寄存器指针丢失 */
    if (LzI2cReadReg(I2C_BUS_PORT, DS3231_I2C_ADDR, &reg, 1, data, 7) == LZ_HARDWARE_SUCCESS)
    {
        time->second = bcd_to_dec(data[0] & 0x7F);
        time->minute = bcd_to_dec(data[1] & 0x7F);
        time->hour   = bcd_to_dec(data[2] & 0x3F);
        time->day    = bcd_to_dec(data[4] & 0x3F);
        time->month  = bcd_to_dec(data[5] & 0x1F);
        time->year   = bcd_to_dec(data[6]) + 2000;
    }
}

void rtc_time_read_safe(RTC_Time *out)
{
    LOS_MuxPend(g_rtc_mutex, LOS_WAIT_FOREVER);
    *out = g_rtc_time;
    LOS_MuxPost(g_rtc_mutex);
}