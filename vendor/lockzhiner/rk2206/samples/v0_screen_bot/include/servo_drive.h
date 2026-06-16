/***************************************************************
 * 文件名: servo_drive.h
 * 说    明: 双轴舵机驱动模块 — PWM 控制 SG90 舵机
 *           X 轴: PWM0 (PB4), Y 轴: PWM1 (PB5)
 *           PWM 周期 20ms (50Hz), 脉宽 0.5ms~2.5ms 对应 0°~180°
 *           初始化失败时自动锁死 (s_servo_ready=0), 拒绝一切角度设置
 ***************************************************************/

#ifndef _SERVO_DRIVE_H_
#define _SERVO_DRIVE_H_

#include <stdint.h>

/* PWM 端口号 — PB4→PWM0_M1(MUX_FUNC1), PB5→PWM1_M1(MUX_FUNC1) */
#define SERVO_X_PWM_PORT  0
#define SERVO_Y_PWM_PORT  1

/***************************************************************
 * 函数名称: servo_init
 * 说    明: 初始化双轴舵机 PWM 通道, 回中到 90°
 *           任一轴初始化失败则设置 s_servo_ready=0 锁死舵机
 ***************************************************************/
void servo_init(void);

/***************************************************************
 * 函数名称: servo_set_angle
 * 说    明: 设置指定舵机轴的角度 (自动钳位到 0°~180°)
 *           初始化未完成时 (s_servo_ready==0) 直接返回
 * 参    数:
 *       @port:  PWM 端口号 (SERVO_X_PWM_PORT 或 SERVO_Y_PWM_PORT)
 *       @angle: 目标角度 (0~180)
 ***************************************************************/
void servo_set_angle(uint32_t port, uint8_t angle);

#endif /* _SERVO_DRIVE_H_ */
