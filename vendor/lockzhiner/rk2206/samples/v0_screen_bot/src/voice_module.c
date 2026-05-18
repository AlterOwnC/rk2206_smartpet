#include "voice_module.h"
#include <stdio.h>
#include <string.h>
#include "los_task.h"
#include "lz_hardware.h"
#include "cmsis_os2.h"

#include "menu_ui.h"
#include "ds3231.h"
#include "hardware_control.h"
#include "command_queue.h"

#define UART_ID         2
#define STRING_MAXSIZE  128

void voice_uart_thread(void *arg)
{
    unsigned int ret;
    UartAttribute attr;
    unsigned char recv_buffer[STRING_MAXSIZE];
    unsigned int recv_length = 0;

    LzUartDeinit(UART_ID);

    attr.baudRate = 115200;
    attr.dataBits = UART_DATA_BIT_8;
    attr.pad = FLOW_CTRL_NONE;
    attr.parity = UART_PARITY_NONE;
    attr.rxBlock = UART_BLOCK_STATE_NONE_BLOCK;
    attr.stopBits = UART_STOP_BIT_1;
    attr.txBlock = UART_BLOCK_STATE_NONE_BLOCK;

    PinctrlSet(GPIO0_PB2, MUX_FUNC3, PULL_KEEP, DRIVE_LEVEL2);
    PinctrlSet(GPIO0_PB3, MUX_FUNC3, PULL_KEEP, DRIVE_LEVEL2);

    ret = LzUartInit(UART_ID, &attr);
    if (ret != LZ_HARDWARE_SUCCESS) {
        printf("[Voice] UART2 Init failed! Error code: %d\r\n", ret);
        return;
    }

    printf("\n[Voice] UART2 Started! Waiting for voice commands...\r\n");

    while (1)
    {
        recv_length = 0;
        memset(recv_buffer, 0, sizeof(recv_buffer));

        recv_length = LzUartRead(UART_ID, recv_buffer, sizeof(recv_buffer));

        if (recv_length > 0)
        {
            for (unsigned int i = 0; i < recv_length - 1; i++)
            {
                // ============ 查询类指令 (直接 UART 回复 + 互斥锁保护) ============

                // 1. 请求温度: 06 01
                if (recv_buffer[i] == 0x06 && recv_buffer[i+1] == 0x01)
                {
                    unsigned char send_buf[7] = {0xAA, 0x55, 0x01, 0x00, 0x00, 0x55, 0xAA};
                    osMutexAcquire(g_sensor_mutex, osWaitForever);
                    send_buf[3] = (unsigned char)g_sensor_temp;
                    osMutexRelease(g_sensor_mutex);
                    LzUartWrite(UART_ID, send_buf, 7);
                    printf("[Voice] Temp Cmd! Send: %dC\r\n", send_buf[3]);
                    break;
                }

                // 2. 请求湿度: 06 02
                else if (recv_buffer[i] == 0x06 && recv_buffer[i+1] == 0x02)
                {
                    unsigned char send_buf[7] = {0xAA, 0x55, 0x02, 0x00, 0x00, 0x55, 0xAA};
                    osMutexAcquire(g_sensor_mutex, osWaitForever);
                    send_buf[3] = (unsigned char)g_sensor_hum;
                    osMutexRelease(g_sensor_mutex);
                    LzUartWrite(UART_ID, send_buf, 7);
                    printf("[Voice] Humi Cmd! Send: %d%%\r\n", send_buf[3]);
                    break;
                }

                // 3. 请求时间: 07 01
                else if (recv_buffer[i] == 0x07 && recv_buffer[i+1] == 0x01)
                {
                    unsigned char send_buf[7] = {0xAA, 0x55, 0x03, 0x00, 0x00, 0x55, 0xAA};
                    RTC_Time t;
                    rtc_time_read_safe(&t);
                    send_buf[3] = (unsigned char)t.hour;
                    send_buf[4] = (unsigned char)t.minute;
                    LzUartWrite(UART_ID, send_buf, 7);
                    printf("[Voice] Time Cmd! Send: %02d:%02d\r\n", send_buf[3], send_buf[4]);
                    break;
                }

                // 4. 请求日期: 07 02
                else if (recv_buffer[i] == 0x07 && recv_buffer[i+1] == 0x02)
                {
                    unsigned char send_buf[8] = {0xAA, 0x55, 0x04, 0x00, 0x00, 0x00, 0x55, 0xAA};
                    RTC_Time t;
                    rtc_time_read_safe(&t);
                    send_buf[3] = (unsigned char)(t.year % 100);
                    send_buf[4] = (unsigned char)t.month;
                    send_buf[5] = (unsigned char)t.day;
                    LzUartWrite(UART_ID, send_buf, 8);
                    printf("[Voice] Date Cmd! Send: 20%02d-%02d-%02d\r\n", send_buf[3], send_buf[4], send_buf[5]);
                    break;
                }

                // ============ 动作控制指令 (推入命令队列，由 LCD 主循环统一执行) ============

                // 5. 语音喂食: 02 01
                else if (recv_buffer[i] == 0x02 && recv_buffer[i+1] == 0x01)
                {
                    printf("[Voice] Action: Feed 1 Portion (queued)\r\n");
                    SystemCommand cmd = {.type = CMD_FEED, .param1 = 1};
                    cmd_send(&cmd);
                    break;
                }

                // 6. 开启逗猫: 03 01
                else if (recv_buffer[i] == 0x03 && recv_buffer[i+1] == 0x01)
                {
                    printf("[Voice] Action: Enter Cat Mode (queued)\r\n");
                    SystemCommand cmd = {.type = CMD_CAT_MODE_ENTER, .param1 = 0};
                    cmd_send(&cmd);
                    break;
                }

                // 7. 退出逗猫: 03 02
                else if (recv_buffer[i] == 0x03 && recv_buffer[i+1] == 0x02)
                {
                    printf("[Voice] Action: Exit Cat Mode (queued)\r\n");
                    SystemCommand cmd = {.type = CMD_CAT_MODE_EXIT};
                    cmd_send(&cmd);
                    break;
                }

                // 8. 开启风扇: 01 01
                else if (recv_buffer[i] == 0x01 && recv_buffer[i+1] == 0x01)
                {
                    printf("[Voice] Action: Fan ON (queued)\r\n");
                    SystemCommand cmd = {.type = CMD_FAN_ON};
                    cmd_send(&cmd);
                    break;
                }

                // 9. 关闭风扇: 01 02
                else if (recv_buffer[i] == 0x01 && recv_buffer[i+1] == 0x02)
                {
                    printf("[Voice] Action: Fan OFF (queued)\r\n");
                    SystemCommand cmd = {.type = CMD_FAN_OFF};
                    cmd_send(&cmd);
                    break;
                }
            }
        }

        LOS_Msleep(100);
    }
}
