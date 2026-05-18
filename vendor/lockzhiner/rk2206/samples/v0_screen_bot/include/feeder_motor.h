#ifndef _FEEDER_MOTOR_H_
#define _FEEDER_MOTOR_H_

#include <stdint.h>

void feeder_motor_init(void);
void feeder_motor_start(uint8_t speed_percent);

#endif /* _FEEDER_MOTOR_H_ */