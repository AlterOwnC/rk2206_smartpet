#include "command_queue.h"
#include <stdio.h>
#include "los_mux.h"
#include "los_queue.h"

unsigned int g_sensor_mutex;
unsigned int g_rtc_mutex;
unsigned int g_cmd_queue;
EVENT_CB_S g_rtc_evt;

/***************************************************************
 * 函数名称: system_sync_init
 * 说    明: 初始化 RTOS 同步原语 (互斥锁、消息队列、事件组)
 *           统一使用 LiteOS 原生 API 替代 CMSIS-RTOS v2
 * 参    数: 无
 * 返 回 值: 无
 ***************************************************************/
void system_sync_init(void)
{
    unsigned int ret;

    /* 创建传感器互斥锁 */
    ret = LOS_MuxCreate(&g_sensor_mutex);
    if (ret != LOS_OK) {
        printf("[FATAL] sensor mutex init failed (ret=0x%x)!\n", ret);
    }

    /* 创建 RTC 互斥锁 */
    ret = LOS_MuxCreate(&g_rtc_mutex);
    if (ret != LOS_OK) {
        printf("[FATAL] RTC mutex init failed (ret=0x%x)!\n", ret);
    }

    /* 创建命令队列, 队列长度 16, 单条消息大小为 sizeof(SystemCommand) */
    ret = LOS_QueueCreate("cmd_queue", 16, &g_cmd_queue, 0, sizeof(SystemCommand));
    if (ret != LOS_OK) {
        printf("[FATAL] cmd queue init failed (ret=0x%x)!\n", ret);
    }

    /* 创建 RTC 同步事件控制块 (静态分配, 非指针) */
    ret = LOS_EventInit(&g_rtc_evt);
    if (ret != LOS_OK) {
        printf("[FATAL] RTC event init failed (ret=0x%x)!\n", ret);
    }

    printf("[SYNC] LiteOS sync primitives ready.\n");
}

/***************************************************************
 * 函数名称: cmd_send
 * 说    明: 向命令队列发送一条系统指令 (阻塞等待直到入队成功)
 * 参    数:
 *       @cmd: 指向 SystemCommand 结构体的指针
 * 返 回 值: LOS_OK(0) 表示成功, -1 表示参数无效
 ***************************************************************/
int cmd_send(SystemCommand *cmd)
{
    if (!g_cmd_queue || !cmd) return -1;
    return LOS_QueueWriteCopy(g_cmd_queue, cmd, sizeof(SystemCommand), LOS_WAIT_FOREVER);
}

/***************************************************************
 * 函数名称: cmd_recv
 * 说    明: 从命令队列读取一条系统指令
 * 参    数:
 *       @cmd:        接收 SystemCommand 的缓冲区指针
 *       @timeout_ms: 超时时间(ms), 0 表示非阻塞
 * 返 回 值: LOS_OK(0) 表示成功, 其他值表示失败或超时
 ***************************************************************/
int cmd_recv(SystemCommand *cmd, uint32_t timeout_ms)
{
    if (!g_cmd_queue || !cmd) return -1;
    UINT32 size = sizeof(SystemCommand);
    return LOS_QueueReadCopy(g_cmd_queue, cmd, &size, timeout_ms);
}
