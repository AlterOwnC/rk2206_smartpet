#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "los_task.h"
#include "ohos_init.h"
#include "lz_hardware.h"

#define I2C_BUS             0
#define SLAVE_ADDRESS_MAXSIZE           0x100

/* 回归 100% 可用的 I2C0 黄金通道：PA0(SDA) 和 PA1(SCL) */
static I2cBusIo m_i2cBus = {
    .scl =  {
        .gpio = GPIO0_PA1,      
        .func = MUX_FUNC3,
        .type = PULL_UP,        // 开启上拉电阻，保障信号稳定
        .drv = DRIVE_KEEP,
        .dir = LZGPIO_DIR_KEEP,
        .val = LZGPIO_LEVEL_KEEP
    },
    .sda =  {
        .gpio = GPIO0_PA0,      
        .func = MUX_FUNC3,
        .type = PULL_UP,        // 开启上拉电阻，保障信号稳定
        .drv = DRIVE_KEEP,
        .dir = LZGPIO_DIR_KEEP,
        .val = LZGPIO_LEVEL_KEEP
    },
    .id = FUNC_ID_I2C0,
    .mode = FUNC_MODE_M2,
};

static unsigned int m_i2c_freq = 100000; // 降速到 100kHz，抵抗杜邦线干扰

void i2c_scan_process(void)
{
    unsigned short slaveAddr[SLAVE_ADDRESS_MAXSIZE];
    unsigned int slaveAddrLen;
    unsigned int i;
    
    printf("\n====== 开始扫描 I2C0 总线 (PA0/PA1) ======\n");
    
    I2cIoInit(m_i2cBus);
    LzI2cInit(I2C_BUS, m_i2c_freq);

    PinctrlSet(GPIO0_PA1, MUX_FUNC3, PULL_UP, DRIVE_KEEP);
    PinctrlSet(GPIO0_PA0, MUX_FUNC3, PULL_UP, DRIVE_KEEP);

    /* 必须给摄像头 1 秒钟的苏醒时间，等它内部初始化完成 */
    LOS_Msleep(1000);

    slaveAddrLen = LzI2cScan(I2C_BUS, slaveAddr, SLAVE_ADDRESS_MAXSIZE);

    if (slaveAddrLen == 0) {
        printf("扫描结束，没有发现任何设备。\n");
    } else {
        int found_cam = 0;
        for (i = 0; i < slaveAddrLen; i++) {
            printf(">>> 发现设备地址: 0x%02x <<<\n", slaveAddr[i]);
            // OV2640 的 I2C 地址通常是 0x30，有时偏移会变成 0x60 或 0x61
            if (slaveAddr[i] == 0x30 || slaveAddr[i] == 0x60 || slaveAddr[i] == 0x61) {
                found_cam = 1;
            }
        }
        
        if (found_cam) {
            printf("\n🎉 太牛了！成功抓到 OV2640 摄像头！\n");
        } else {
            printf("\n❌ 依然只有板载芯片 (0x38/0x51)。\n");
            printf("说明 I2C 总线没问题，但摄像头没吭声。请彻底检查 PWDN 和 RESET 的杜邦线！\n");
        }
    }
}

void i2c_scan_example(void)
{
    unsigned int thread_id;
    TSK_INIT_PARAM_S task = {0};

    task.pfnTaskEntry = (TSK_ENTRY_FUNC)i2c_scan_process;
    task.uwStackSize = 20480;
    task.pcName = "camera_i2c_scan";
    task.usTaskPrio = 24;
    LOS_TaskCreate(&thread_id, &task);
}
APP_FEATURE_INIT(i2c_scan_example);