/*
 * usb_cdc.h
 *
 * USB infra: device task, event bits, callback dispatch.
 *
 * Cada módulo que usa um CDC registra seus handlers antes de osKernelStart():
 *   usb_cdc_register_rx_handler(itf, handler)
 *   usb_cdc_register_tx_done_handler(itf, handler)
 *
 * Handlers são chamados em contexto de task (usb_device_task), NÃO em ISR.
 * Use APIs FreeRTOS normais (sem FromISR) dentro dos handlers.
 */

#ifndef SRC_USB_CDC_H_
#define SRC_USB_CDC_H_

#include <stdint.h>
#include "FreeRTOS.h"
#include "event_groups.h"

extern EventGroupHandle_t usb_event_group;

#define USB_EVT_CDC0_CONNECTED  (1u << 0)   /* CDC[0] Modbus: DTR set  */
#define USB_EVT_CDC1_CONNECTED  (1u << 1)   /* CDC[1] Console: DTR set */

/* Callbacks chamados em usb_device_task (task level, NÃO ISR). */
typedef void (*cdc_rx_handler_t)(uint8_t itf);
typedef void (*cdc_tx_done_handler_t)(uint8_t itf);

/* Cria usb_event_group. Chamar antes de osKernelStart(). */
void usb_cdc_init(void);

/* Registra handler por interface. itf: 0=Modbus, 1=Console.
 * Chamar antes de osKernelStart(). */
void usb_cdc_register_rx_handler(uint8_t itf, cdc_rx_handler_t handler);
void usb_cdc_register_tx_done_handler(uint8_t itf, cdc_tx_done_handler_t handler);

void usb_device_task(void *param);

#endif /* SRC_USB_CDC_H_ */
