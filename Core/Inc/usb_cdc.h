/*
 * usb_cdc.h
 *
 *  Created on: Jul 2, 2026
 *      Author: gefersonartuzo
 */

#ifndef SRC_USB_CDC_H_
#define SRC_USB_CDC_H_

#include <stdint.h>
#include "FreeRTOS.h"

void usb_device_task(void *param);
void cdc_task(void *params);

/* Envia buffer via USB-CDC com flow control (aguarda TX complete via semáforo).
 * Frames > 64 bytes são fragmentados automaticamente em pacotes USB-FS. */
void tud_cdc_send(uint8_t *buffer, uint32_t bufsize, TickType_t timeout);

/* Recebe dados do host via USB-CDC. Retorna quantidade de bytes lidos. */
uint32_t tud_cdc_receive(uint8_t *buffer, uint32_t bufsize, TickType_t timeout);

#endif /* SRC_USB_CDC_H_ */
