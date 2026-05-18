#ifndef _ADC_KEY_H_
#define _ADC_KEY_H_

#include <stdint.h>

// 定义按键的枚举键值
typedef enum {
    KEY_NONE = 0,
    KEY_UP,
    KEY_DOWN,
    KEY_ENTER,
    KEY_BACK
} KeyCode;

// 初始化按键 ADC
void adc_key_init(void);

// 扫描并返回当前按下的按键
KeyCode adc_key_scan(void);

#endif /* _ADC_KEY_H_ */