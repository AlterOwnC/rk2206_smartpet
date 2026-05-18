/*
 * Copyright (c) 2022 FuZhou Lockzhiner Electronic Co., Ltd. All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "nfc.h"
#include "los_task.h"
#include "ohos_init.h"

#define TEXT        "XiaoZhiPai!"
#define WEB         "fzlzdz.com"

/***************************************************************
* 函数名称: nfc_process
* 说    明: nfc例程 (纯读取模式)
* 参    数: 无
* 返 回 值: 无
***************************************************************/
void nfc_process(void)
{
    /* 1. 初始化NFC设备 */
    nfc_init();
    
    // 给一点时间确保芯片初始化完成
    LOS_Msleep(500); 

    /* 2. 进入主循环，持续读取NFC标签内的内容 */
    while (1) {
        printf("============== NFC Read Mode ==============\r\n");
        printf("Please use your phone to write data to the board.\r\n");
        printf("Reading User Data Pages (Page 1 to 5)...\r\n");
        
        // 调用读取函数，读取第1页到第5页的用户数据
        // (注：第0页通常是只读的制造商信息UID，所以我们从第1页开始读)
        nfc_read_raw_data(1, 5); 

        printf("\n");
        
        // 每隔 3 秒钟读取一次，避免串口打印太快刷屏
        LOS_Msleep(3000);
    }
}


/***************************************************************
* 函数名称: nfc_example
* 说    明: 开机自启动调用函数
* 参    数: 无
* 返 回 值: 无
***************************************************************/
void nfc_example()
{
    unsigned int thread_id;
    TSK_INIT_PARAM_S task = {0};
    unsigned int ret = LOS_OK;

    task.pfnTaskEntry = (TSK_ENTRY_FUNC)nfc_process;
    task.uwStackSize = 10240;
    task.pcName = "nfc process";
    task.usTaskPrio = 24;
    ret = LOS_TaskCreate(&thread_id, &task);
    if (ret != LOS_OK)
    {
        printf("Falied to create task ret:0x%x\n", ret);
        return;
    }
}


APP_FEATURE_INIT(nfc_example);
