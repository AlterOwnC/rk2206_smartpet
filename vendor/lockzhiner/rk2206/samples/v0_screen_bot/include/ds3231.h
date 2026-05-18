#ifndef _DS3231_H_
#define _DS3231_H_

#include <stdint.h>

// 完整的时间结构体
typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} RTC_Time;

// 暴露给其他任务的全局时间变量
extern volatile RTC_Time g_rtc_time;

void ds3231_init(void);

// 设置时间 (示例: 2026, 4, 6, 16, 35, 0)
void ds3231_set_datetime(uint16_t year, uint8_t month, uint8_t day, uint8_t h, uint8_t m, uint8_t s);

void ds3231_get_datetime(RTC_Time *time);

// 线程安全读取全局时间 (带互斥锁保护)
void rtc_time_read_safe(RTC_Time *out);

#endif /* _DS3231_H_ */