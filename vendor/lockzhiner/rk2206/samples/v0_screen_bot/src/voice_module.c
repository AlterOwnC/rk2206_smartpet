#include "voice_module.h"
#include <stdio.h>
#include <string.h>
#include "los_task.h"
#include "los_mux.h"
#include "lz_hardware.h"

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

    /* 累积缓冲区: 解决 UART 非阻塞模式下粘包/半包问题 */
    static unsigned char acc_buf[256];
    static unsigned int  acc_len = 0;
    unsigned int idle_count = 0; // 空闲计数, 用于超时检测

    /* 前置反初始化: 防止上次异常退出导致资源残留 */
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
            /* 将新数据追加到累积缓冲区 (防止溢出) */
            if (acc_len + recv_length < sizeof(acc_buf)) {
                memcpy(&acc_buf[acc_len], recv_buffer, recv_length);
                acc_len += recv_length;
            } else {
                /* 缓冲区溢出则清空重置, 从当前帧重新开始 */
                printf("[Voice] WARNING: acc buffer overflow! Dropping %u old bytes, keeping new %u bytes.\r\n",
                       acc_len, recv_length);
                acc_len = 0;
                memcpy(acc_buf, recv_buffer, recv_length);
                acc_len = recv_length;
            }
            idle_count = 0; // 有数据到达, 重置空闲计数
        }
        else
        {
            /* 超时机制: 连续 20 次无数据 (20×100ms=2s) 后清空累积缓冲区
               注意: 当前协议无帧同步头, 清空可能导致半包丢失。
               未来改进应增加帧头(如 0xAA 0x55)和校验字节。 */
            idle_count++;
            if (idle_count > 20) {
                if (acc_len > 0) {
                    printf("[Voice] WARNING: idle timeout (2s), discarding %u stale bytes.\r\n", acc_len);
                }
                acc_len = 0;
                idle_count = 0;
            }
        }

        /* 从累积缓冲区中解析命令对 (逐字节扫描) */
        if (acc_len >= 2)
        {
            for (unsigned int i = 0; i < acc_len - 1; i++)
            {
                int matched = 1; // 标记是否匹配到有效指令

                // ============ 查询类指令 (直接 UART 回复 + 互斥锁保护) ============

                // 1. 请求温度: 06 01
                if (acc_buf[i] == 0x06 && acc_buf[i+1] == 0x01)
                {
                    unsigned char send_buf[7] = {0xAA, 0x55, 0x01, 0x00, 0x00, 0x55, 0xAA};
                    LOS_MuxPend(g_sensor_mutex, LOS_WAIT_FOREVER);
                    send_buf[3] = (unsigned char)g_sensor_temp;
                    LOS_MuxPost(g_sensor_mutex);
                    LzUartWrite(UART_ID, send_buf, 7);
                    printf("[Voice] Temp Cmd! Send: %dC\r\n", send_buf[3]);
                }

                // 2. 请求湿度: 06 02
                else if (acc_buf[i] == 0x06 && acc_buf[i+1] == 0x02)
                {
                    unsigned char send_buf[7] = {0xAA, 0x55, 0x02, 0x00, 0x00, 0x55, 0xAA};
                    LOS_MuxPend(g_sensor_mutex, LOS_WAIT_FOREVER);
                    send_buf[3] = (unsigned char)g_sensor_hum;
                    LOS_MuxPost(g_sensor_mutex);
                    LzUartWrite(UART_ID, send_buf, 7);
                    printf("[Voice] Humi Cmd! Send: %d%%\r\n", send_buf[3]);
                }

                // 3. 请求时间: 07 01
                else if (acc_buf[i] == 0x07 && acc_buf[i+1] == 0x01)
                {
                    unsigned char send_buf[7] = {0xAA, 0x55, 0x03, 0x00, 0x00, 0x55, 0xAA};
                    RTC_Time t;
                    rtc_time_read_safe(&t);
                    send_buf[3] = (unsigned char)t.hour;
                    send_buf[4] = (unsigned char)t.minute;
                    LzUartWrite(UART_ID, send_buf, 7);
                    printf("[Voice] Time Cmd! Send: %02d:%02d\r\n", send_buf[3], send_buf[4]);
                }

                // 4. 请求日期: 07 02
                else if (acc_buf[i] == 0x07 && acc_buf[i+1] == 0x02)
                {
                    unsigned char send_buf[8] = {0xAA, 0x55, 0x04, 0x00, 0x00, 0x00, 0x55, 0xAA};
                    RTC_Time t;
                    rtc_time_read_safe(&t);
                    send_buf[3] = (unsigned char)(t.year % 100);
                    send_buf[4] = (unsigned char)t.month;
                    send_buf[5] = (unsigned char)t.day;
                    LzUartWrite(UART_ID, send_buf, 8);
                    printf("[Voice] Date Cmd! Send: 20%02d-%02d-%02d\r\n", send_buf[3], send_buf[4], send_buf[5]);
                }

                // ============ 动作控制指令 (推入命令队列，由 LCD 主循环统一执行) ============

                // 5. 语音喂食: 02 01
                else if (acc_buf[i] == 0x02 && acc_buf[i+1] == 0x01)
                {
                    printf("[Voice] Action: Feed 1 Portion (queued)\r\n");
                    SystemCommand cmd = {.type = CMD_FEED, .param1 = 1};
                    cmd_send(&cmd);
                }

                // 6. 开启逗猫: 03 01
                else if (acc_buf[i] == 0x03 && acc_buf[i+1] == 0x01)
                {
                    printf("[Voice] Action: Enter Cat Mode (queued)\r\n");
                    SystemCommand cmd = {.type = CMD_CAT_MODE_ENTER, .param1 = 0};
                    cmd_send(&cmd);
                }

                // 7. 退出逗猫: 03 02
                else if (acc_buf[i] == 0x03 && acc_buf[i+1] == 0x02)
                {
                    printf("[Voice] Action: Exit Cat Mode (queued)\r\n");
                    SystemCommand cmd = {.type = CMD_CAT_MODE_EXIT};
                    cmd_send(&cmd);
                }

                // 8. 开启风扇: 01 01
                else if (acc_buf[i] == 0x01 && acc_buf[i+1] == 0x01)
                {
                    printf("[Voice] Action: Fan ON (queued)\r\n");
                    SystemCommand cmd = {.type = CMD_FAN_ON};
                    cmd_send(&cmd);
                }

                // 9. 关闭风扇: 01 02
                else if (acc_buf[i] == 0x01 && acc_buf[i+1] == 0x02)
                {
                    printf("[Voice] Action: Fan OFF (queued)\r\n");
                    SystemCommand cmd = {.type = CMD_FAN_OFF};
                    cmd_send(&cmd);
                }
                else
                {
                    matched = 0; // 未匹配任何已知指令
                }

                /* 匹配成功则清除已处理的两个字节, 重新扫描缓冲区 */
                if (matched) {
                    unsigned int remaining = acc_len - i - 2;
                    if (remaining > 0) {
                        memmove(&acc_buf[i], &acc_buf[i + 2], remaining);
                    }
                    acc_len = (i + remaining > 0) ? (i + remaining) : 0;
                    i = (i > 0) ? (i - 1) : 0; // 回退一个位置, 检查重叠匹配
                }
            }
        }

        LOS_Msleep(100);
    }
}
