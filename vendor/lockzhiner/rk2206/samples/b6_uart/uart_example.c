/*
 * Copyright (c) 2022 FuZhou Lockzhiner Electronic Co., Ltd. All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * ...
 */
#include <stdio.h>
#include <string.h>
#include "los_task.h"
#include "ohos_init.h"
#include "lz_hardware.h"

// 1. 设置为 UART2
#define UART_ID                 2
#define STRING_MAXSIZE          128

void uart_process(void)
{
    unsigned int ret;
    UartAttribute attr;
    unsigned char recv_buffer[STRING_MAXSIZE];
    unsigned int recv_length = 0;

    // 自定义温湿度数据
    float temp = 35.67;
    float humi = 50.34;

    LzUartDeinit(UART_ID);
    
    // 串口波特率配置为 9600 (与语音模块一致)
    attr.baudRate = 115200; 
    attr.dataBits = UART_DATA_BIT_8;
    attr.pad = FLOW_CTRL_NONE;
    attr.parity = UART_PARITY_NONE;
    attr.rxBlock = UART_BLOCK_STATE_NONE_BLOCK;
    attr.stopBits = UART_STOP_BIT_1;
    attr.txBlock = UART_BLOCK_STATE_NONE_BLOCK;
    
    // 【注意】这里请确保填的是你查表得到的 UART2_TX 和 UART2_RX 的真实引脚
    PinctrlSet(GPIO0_PB2, MUX_FUNC3, PULL_KEEP, DRIVE_LEVEL2); 
    PinctrlSet(GPIO0_PB3, MUX_FUNC3, PULL_KEEP, DRIVE_LEVEL2); 
    
    ret = LzUartInit(UART_ID, &attr);
    if (ret != LZ_HARDWARE_SUCCESS)
    {
        printf("%s, %d: LzUartInit(%d) failed!\r\n", __FILE__, __LINE__, ret);
        return;
    }
    
    printf("UART2 Init Success (Baud: 9600). Waiting for voice module commands...\r\n");

    while (1)
    {
        recv_length = 0;
        memset(recv_buffer, 0, sizeof(recv_buffer));
        
        // 读取串口2数据
        recv_length = LzUartRead(UART_ID, recv_buffer, sizeof(recv_buffer));
        
        if (recv_length > 0)
        {
            // 1. 打印收到的原始数据到电脑终端，方便调试
            char print_buf[256] = {0};
            int offset = sprintf(print_buf, "UART2 Recv %d bytes: ", recv_length);
            for (unsigned int i = 0; i < recv_length; i++) 
            {
                offset += sprintf(print_buf + offset, "%02X ", recv_buffer[i]);
            }
            sprintf(print_buf + offset, "\r\n");
            printf("%s", print_buf);

            // 2. 解析指令并回复数据
            // 遍历缓冲区寻找 06 01 或 06 02 (防止粘包或含有其他数据)
            for (unsigned int i = 0; i < recv_length - 1; i++)
            {
                // --------- 匹配到请求温度: 06 01 ---------
                if (recv_buffer[i] == 0x06 && recv_buffer[i+1] == 0x01) 
                {
                    unsigned char send_buf[8] = {0xAA, 0x55, 0x01, 0x00, 0x00, 0x01, 0x55, 0xAA};

                    
                    // 浮点数拆分算法 (比如 35.67)
                    int t_int = (int)temp;                                  // 整数部分: 35
                    int t_dec = (int)((temp - t_int) * 100 + 0.5);          // 小数部分: 67 (+0.5为了四舍五入防精度丢失)
                    
                    send_buf[3] = (unsigned char)t_int;                     // 第1个XX: 35 (即HEX的0x23)
                    send_buf[4] = (unsigned char)(t_dec / 10);              // 第2个XX: 6  (即HEX的0x06)

                    // 通过UART2发送给语音模块
                    LzUartWrite(UART_ID, send_buf, 8);
                    
                    printf("-> Match Temp Cmd! Send Data: AA 55 01 %02X %02X 01 55 AA\r\n", send_buf[3], send_buf[4]);
                    break; // 处理完毕，跳出解析循环
                }
                
                // --------- 匹配到请求湿度: 06 02 ---------
                else if (recv_buffer[i] == 0x06 && recv_buffer[i+1] == 0x02) 
                {
                    unsigned char send_buf[8] = {0xAA, 0x55, 0x02, 0x00, 0x00,  0x02,0x55, 0xAA};
                    
                    // 浮点数拆分算法 (比如 50.34)
                    int h_int = (int)humi;                                  // 整数部分: 50
                    int h_dec = (int)((humi - h_int) * 100 + 0.5);          // 小数部分: 34
                    
                    send_buf[3] = (unsigned char)h_int;                     // 第1个XX: 50 (即HEX的0x32)
                    send_buf[4] = (unsigned char)(h_dec / 10);              // 第2个XX: 3  (即HEX的0x03)

                    // 通过UART2发送给语音模块
                    LzUartWrite(UART_ID, send_buf, 8);
                    
                    printf("-> Match Humi Cmd! Send Data: AA 55 02 %02X %02X 02 55 AA\r\n", send_buf[3], send_buf[4]);
                    break; // 处理完毕，跳出解析循环
                }
            }
        }

        // 轮询延时 100ms
        LOS_Msleep(100);
    }
    
    return;
}

/***************************************************************
* 函数名称: uart_example
* 说    明: 开机自启动调用函数
* 参    数: 无
* 返 回 值: 无
***************************************************************/
void uart_example()
{
    unsigned int thread_id;
    TSK_INIT_PARAM_S task = {0};
    unsigned int ret = LOS_OK;

    task.pfnTaskEntry = (TSK_ENTRY_FUNC)uart_process;
    task.uwStackSize = 1024 * 1024;
    task.pcName = "uart process";
    task.usTaskPrio = 24;
    ret = LOS_TaskCreate(&thread_id, &task);
    if (ret != LOS_OK)
    {
        printf("Falied to create task ret:0x%x\r\n", ret);
        return;
    }
}

APP_FEATURE_INIT(uart_example);