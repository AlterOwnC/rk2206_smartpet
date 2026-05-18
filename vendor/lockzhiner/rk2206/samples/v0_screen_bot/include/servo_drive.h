#ifndef _SERVO_DRIVE_H_
#define _SERVO_DRIVE_H_

#include <stdint.h>

// 舵机移至 B4 和 B5，占用 PWM0 和 PWM1
#define SERVO_X_PWM_PORT  0  // 对应 PB4 (PWM0_M1)
#define SERVO_Y_PWM_PORT  1  // 对应 PB5 (PWM1_M1)

void servo_init(void);
void servo_set_angle(uint32_t port, uint8_t angle);

#endif /* _SERVO_DRIVE_H_ */