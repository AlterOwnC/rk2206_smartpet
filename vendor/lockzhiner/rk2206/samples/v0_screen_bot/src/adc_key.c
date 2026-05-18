#include "adc_key.h"
#include "lz_hardware.h"

#define ADC_CHANNEL 7

static DevIo m_adcKey = {
    .isr =   {.gpio = INVALID_GPIO},
    .rst =   {.gpio = INVALID_GPIO},
    .ctrl1 = {.gpio = GPIO0_PC7, .func = MUX_FUNC0, .type = PULL_NONE, .drv = DRIVE_KEEP, .dir = LZGPIO_DIR_KEEP, .val = LZGPIO_LEVEL_KEEP},
    .ctrl2 = {.gpio = INVALID_GPIO},
};

void adc_key_init(void)
{
    uint32_t *pGrfSocCon29 = (uint32_t *)(0x41050000U + 0x274U);
    DevIoInit(m_adcKey);
    LzSaradcInit();
    
    uint32_t ulValue = *pGrfSocCon29;
    ulValue &= ~(0x1 << 4);
    ulValue |= ((0x1 << 4) << 16);
    *pGrfSocCon29 = ulValue;
}

KeyCode adc_key_scan(void)
{
    unsigned int data = 0;
    if (LzSaradcReadValue(ADC_CHANNEL, &data) != LZ_HARDWARE_SUCCESS) return KEY_NONE;
    
    float voltage = (float)(data * 3.3 / 1024.0);

    // 根据你提供的实测电压划定宽容区间
    if (voltage >= 0.0 && voltage <= 0.2) return KEY_UP;         // 实测 0.01V
    if (voltage >= 0.3 && voltage <= 0.8) return KEY_ENTER;      // 实测 0.55V
    if (voltage >= 0.9 && voltage <= 1.4) return KEY_DOWN;       // 实测 1.16V
    if (voltage >= 1.45 && voltage <= 1.9) return KEY_BACK;      // 实测 1.67V
    
    return KEY_NONE; // 大于 2.5V 视为未按下
}