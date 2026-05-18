#include "command_queue.h"
#include <stdio.h>

osMutexId_t g_sensor_mutex = NULL;
osMutexId_t g_rtc_mutex = NULL;
osMessageQueueId_t g_cmd_queue = NULL;
osEventFlagsId_t g_rtc_evt = NULL;

void system_sync_init(void)
{
    g_sensor_mutex = osMutexNew(NULL);
    g_rtc_mutex    = osMutexNew(NULL);
    g_cmd_queue    = osMessageQueueNew(16, sizeof(SystemCommand), NULL);
    g_rtc_evt      = osEventFlagsNew(NULL);

    if (!g_sensor_mutex || !g_rtc_mutex || !g_cmd_queue || !g_rtc_evt) {
        printf("[FATAL] RTOS sync primitives init failed!\n");
    }
}

int cmd_send(SystemCommand *cmd)
{
    if (!g_cmd_queue || !cmd) return -1;
    return osMessageQueuePut(g_cmd_queue, cmd, 0, 0);
}

int cmd_recv(SystemCommand *cmd, uint32_t timeout_ms)
{
    if (!g_cmd_queue || !cmd) return -1;
    return osMessageQueueGet(g_cmd_queue, cmd, NULL, timeout_ms);
}
