#include "los_task.h"
#include "ohos_init.h"
#include <stdio.h>
#include "lz_hardware.h"


// 定义引脚宏
#define HX711_DOUT_PIN  GPIO0_PB0
#define HX711_SCK_PIN   GPIO0_PB1

// 适配高低电平输出
#define HX711_SCK_0()     LzGpioSetVal(HX711_SCK_PIN, LZGPIO_LEVEL_LOW)
#define HX711_SCK_1()     LzGpioSetVal(HX711_SCK_PIN, LZGPIO_LEVEL_HIGH)

// 读取DOUT电平状态
static inline int HX711_DOUT_READ(void) {
    LzGpioValue val = LZGPIO_LEVEL_LOW;
    LzGpioGetVal(HX711_DOUT_PIN, &val);
    return (val == LZGPIO_LEVEL_HIGH) ? 1 : 0;
}

// 称重参数
long offset_weight = 0;
// 比例系数：待校准
float scale_factor = 420.0f; 

// HX711 读取函数
long hx711_read(void)
{
    long count = 0;
    unsigned char i;
    
    // 初始化时钟线为低电平
    HX711_SCK_0();
    
    // 当 DOUT 为高电平时，表明 A/D 转换器还未准备好输出数据
    // 等待 DOUT 拉低
    while(HX711_DOUT_READ() == 1); 
    
    // 读取24位数据 (MSB先行)
    for (i = 0; i < 24; i++) {
        HX711_SCK_1(); 
        count = count << 1;
        HX711_SCK_0(); 
        if (HX711_DOUT_READ()) {
            count++;
        }
    }
    
    // 发送第25个脉冲：选择通道A，增益设置为128
    HX711_SCK_1();
    // 芯片数据手册提供的处理方式，将二进制补码转换为无符号数处理逻辑
    count = count ^ 0x800000; 
    HX711_SCK_0();
    
    return count;
}

// 串口数据处理任务
void serial_weight_process(void *arg)
{
    long raw_data = 0;
    float real_weight = 0.0;
    
    // ------------------- 引脚初始化开始 -------------------
    
    // 1. 初始化 DOUT (数据接收引脚)
    LzGpioInit(HX711_DOUT_PIN);
    // 配置复用为普通 GPIO，保持默认上下拉，默认驱动能力
    PinctrlSet(HX711_DOUT_PIN, MUX_FUNC0, PULL_KEEP, DRIVE_LEVEL0); 
    // 设置为输入方向
    LzGpioSetDir(HX711_DOUT_PIN, LZGPIO_DIR_IN);
    
    // 2. 初始化 SCK (时钟控制引脚)
    LzGpioInit(HX711_SCK_PIN);
    // 配置复用为普通 GPIO
    PinctrlSet(HX711_SCK_PIN, MUX_FUNC0, PULL_KEEP, DRIVE_LEVEL0); 
    // 设置为输出方向
    LzGpioSetDir(HX711_SCK_PIN, LZGPIO_DIR_OUT);
    
    // 3. 强制拉低时钟线，防止 HX711 断电休眠
    HX711_SCK_0(); 
    LOS_Msleep(100); 
    
    // ------------------- 引脚初始化结束 -------------------

    printf("AterOwnC_Starting HX711 initialization...\n");
    
    // 去皮/归零操作
    printf("Calibrating zero point. Please remove any weight...\n");
    LOS_Msleep(1000); // 延时一秒确保稳定
    offset_weight = hx711_read(); 
    printf("Zero point calibrated. Offset = %ld\n", offset_weight);

    // 循环读取并打印
    while (1)
    {
        raw_data = hx711_read();
        
        // 计算实际重量 = (当前AD值 - 初始零点AD值) / 比例系数
        real_weight = (float)(raw_data - offset_weight) / scale_factor;
        
        // 滤除微小的负数波动（例如传感器漂移）
        if (real_weight < 0 && real_weight > -1.0) {
            real_weight = 0.0;
        }

        // 打印到串口
        printf("Raw ADC: %ld | Weight: %.2f g\n", raw_data, real_weight);
        
        LOS_Msleep(500); 
    }
}

// 任务入口
void hx711_example(void)
{
    unsigned int thread_id;
    TSK_INIT_PARAM_S task = {0};
    unsigned int ret = LOS_OK;

    task.pfnTaskEntry = (TSK_ENTRY_FUNC)serial_weight_process;
    task.uwStackSize = 20480;
    task.pcName = "hx711_serial_process";
    task.usTaskPrio = 24;
    
    ret = LOS_TaskCreate(&thread_id, &task);
    if (ret != LOS_OK)
    {
        printf("Failed to create HX711 task. Error code: 0x%x\n", ret);
        return;
    }
}

// 注册应用启动
APP_FEATURE_INIT(hx711_example);