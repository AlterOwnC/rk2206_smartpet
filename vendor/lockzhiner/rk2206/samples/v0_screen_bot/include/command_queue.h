#ifndef _COMMAND_QUEUE_H_
#define _COMMAND_QUEUE_H_

#include <stdint.h>
#include "cmsis_os2.h"

/* ================== 系统命令类型 ================== */
typedef enum {
    CMD_NONE = 0,
    CMD_FEED,              // param1=份数
    CMD_CAT_MODE_ENTER,    // param1=来源(0=本地/语音, 1=远程UDP)
    CMD_CAT_MODE_EXIT,
    CMD_FAN_ON,
    CMD_FAN_OFF,
    CMD_PUMP_ON,
    CMD_PUMP_OFF,
    CMD_SENSOR_UPDATE,     // param1=temp, param2=hum, param3=weight, param4=water
} CommandType;

typedef struct {
    CommandType type;
    int param1;
    int param2;
    int param3;
    int param4;
} SystemCommand;

/* ================== RTOS 同步原语句柄 ================== */
extern osMutexId_t g_sensor_mutex;   // 保护传感器变量 (temp/hum/weight/water)
extern osMutexId_t g_rtc_mutex;      // 保护 g_rtc_time
extern osMessageQueueId_t g_cmd_queue; // 命令队列 (UDP/语音 → LCD 主循环)
extern osEventFlagsId_t g_rtc_evt;     // RTC 同步事件组

#define EVENT_RTC_SYNC  (1 << 0)

/* ================== API ================== */
void system_sync_init(void);
int  cmd_send(SystemCommand *cmd);
int  cmd_recv(SystemCommand *cmd, uint32_t timeout_ms);

#endif /* _COMMAND_QUEUE_H_ */
