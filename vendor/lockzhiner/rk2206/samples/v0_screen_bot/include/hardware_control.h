#ifndef _HARDWARE_CONTROL_H_
#define _HARDWARE_CONTROL_H_

// 外部可以读取当前状态，用于 UI 界面显示
extern int g_pump_state;
extern int g_fan_state;

// 统一的初始化接口
void devices_init(void);

// 统一的控制接口 (传入 1 打开，传入 0 关闭)
void laser_control(int on);
void pump_control(int on);
void fan_control(int on);

#endif /* _HARDWARE_CONTROL_H_ */