/***************************************************************
 * 文件名: adc_key.c
 * 说    明: ADC 按键扫描驱动 — 四键电阻分压键盘
 *           使用 SARADC CH7 (GPIO0_PC7) 采集分压电压值,
 *           通过电压区间映射识别 UP/ENTER/DOWN/BACK 四个按键。
 *
 *           按键   | 实测电压 | 宽容区间
 *           :---   | :---     | :---
 *           UP     | 0.01V    | 0.0V ~ 0.2V
 *           ENTER  | 0.55V    | 0.3V ~ 0.8V
 *           DOWN   | 1.16V    | 0.9V ~ 1.4V
 *           BACK   | 1.67V    | 1.45V ~ 1.9V
 *           无按键 | 3.3V     | > 2.5V (返回 KEY_NONE)
 ***************************************************************/

#include "adc_key.h"
#include "lz_hardware.h"

/* SARADC 通道 7, 对应 GPIO0_PC7 */
#define ADC_CHANNEL 7

/* ADC 引脚配置: PC7 → MUX_FUNC0 (模拟ADC模式) */
static DevIo m_adcKey = {
    .isr =   {.gpio = INVALID_GPIO},
    .rst =   {.gpio = INVALID_GPIO},
    .ctrl1 = {.gpio = GPIO0_PC7, .func = MUX_FUNC0, .type = PULL_NONE, .drv = DRIVE_KEEP, .dir = LZGPIO_DIR_KEEP, .val = LZGPIO_LEVEL_KEEP},
    .ctrl2 = {.gpio = INVALID_GPIO},
};

/***************************************************************
 * 函数名称: adc_key_init
 * 说    明: 初始化 SARADC 外设, 配置 PC7 为模拟输入
 *           操作 GRF_SOC_CON29 寄存器切换 ADC 外部参考电压
 * 参    数: 无
 * 返 回 值: 无
 ***************************************************************/
void adc_key_init(void)
{
    /* GRF_SOC_CON29 寄存器基址: 0x41050000 + 0x274 */
    uint32_t *pGrfSocCon29 = (uint32_t *)(0x41050000U + 0x274U);

    DevIoInit(m_adcKey);
    LzSaradcInit();

    /* 配置 ADC 参考电压 (内部参考, grf_saradc_vol_sel=1) */
    uint32_t ulValue = *pGrfSocCon29;
    ulValue &= ~(0x1 << 4);
    ulValue |= ((0x1 << 4) << 16);
    *pGrfSocCon29 = ulValue;
}

/***************************************************************
 * 函数名称: adc_key_scan
 * 说    明: 读取 ADC 通道7 的电压值, 映射到对应按键
 *           ADC 分辨率 10bit, 参考电压 3.3V
 *           voltage = data * 3.3 / 1024
 * 参    数: 无
 * 返 回 值: KeyCode — 当前按下的按键枚举值, 无按键返回 KEY_NONE
 ***************************************************************/
KeyCode adc_key_scan(void)
{
    unsigned int data = 0;
    if (LzSaradcReadValue(ADC_CHANNEL, &data) != LZ_HARDWARE_SUCCESS) return KEY_NONE;

    float voltage = (float)(data * 3.3 / 1024.0);

    /* 电压区间映射 (宽容区间防止电阻误差和电压波动) */
    if (voltage >= 0.0 && voltage <= 0.2) return KEY_UP;         // 实测 0.01V
    if (voltage >= 0.3 && voltage <= 0.8) return KEY_ENTER;      // 实测 0.55V
    if (voltage >= 0.9 && voltage <= 1.4) return KEY_DOWN;       // 实测 1.16V
    if (voltage >= 1.45 && voltage <= 1.9) return KEY_BACK;      // 实测 1.67V

    return KEY_NONE; // 大于 2.5V 视为未按下 (分压为 VCC=3.3V)
}
