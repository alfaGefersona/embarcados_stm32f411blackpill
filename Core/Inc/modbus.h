/*
 * modbus.h
 *
 *  Created on: Jul 2, 2026
 *      Author: gefersonartuzo
 */

#ifndef INC_MODBUS_H_
#define INC_MODBUS_H_

#include <stdint.h>

#define MODBUS_SLAVE_ID     0x01
#define MODBUS_FC16         0x10

/* TT-101 (LM35): 1 sensor float32 = 2 holding registers */
#define MODBUS_TT101_REGS   2

void modbus_task(void *param);

#endif /* INC_MODBUS_H_ */
