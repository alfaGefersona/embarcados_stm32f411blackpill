/*
 * usb_cdc.c
 *
 * USB device task + callbacks TinyUSB.
 *
 * Pipeline TX: módulo chama tud_cdc_n_write(itf, ...) diretamente
 *              -> usb_device_task faz flush após tud_task_ext (único dono do flush)
 *
 * Pipeline RX: hardware IRQ -> tusb_int_handler -> fila interna TinyUSB
 *              -> tud_task_ext processa -> tud_cdc_rx_cb -> rx_handlers[itf](itf)
 *              -> módulo lê tud_cdc_n_read() dentro do handler
 *
 * Event group bits:
 *   USB_EVT_CDC0_CONNECTED — set quando host abre CDC[0] (DTR)
 *   USB_EVT_CDC1_CONNECTED — set quando host abre CDC[1] (DTR)
 */

#include <stdbool.h>
#include "usb_cdc.h"
#include "main.h"
#include "cmsis_os.h"
#include "tusb.h"

EventGroupHandle_t usb_event_group;

static cdc_rx_handler_t      rx_handlers[2];
static cdc_tx_done_handler_t tx_done_handlers[2];

/* -------------------------------------------------------------------------
 * usb_cdc_init — cria usb_event_group. Chamar antes de osKernelStart().
 * ------------------------------------------------------------------------- */
void usb_cdc_init(void) {
    usb_event_group = xEventGroupCreate();
    configASSERT(usb_event_group != NULL);
}

void usb_cdc_register_rx_handler(uint8_t itf, cdc_rx_handler_t handler) {
    if (itf < 2) {
    	rx_handlers[itf] = handler;
    }
}

void usb_cdc_register_tx_done_handler(uint8_t itf, cdc_tx_done_handler_t handler) {
    if (itf < 2) {
    	tx_done_handlers[itf] = handler;
    }
}

/* -------------------------------------------------------------------------
 * usb_device_task — pump TinyUSB + flush CDCs
 * Único dono do tud_cdc_n_write_flush — sem acesso concorrente ao flush.
 * ------------------------------------------------------------------------- */
void usb_device_task(void *param) {
    (void)param;

    tusb_rhport_init_t dev_init = {
        .role  = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_FULL,
    };
    bool ok = tusb_init(BOARD_TUD_RHPORT, &dev_init);
    configASSERT(ok);

    while (1) {
        /* max 2ms wait — garante flush rápido mesmo sem novo evento USB */
        tud_task_ext(pdMS_TO_TICKS(2), false);
        /* flush ambos CDCs após processar eventos */
        tud_cdc_n_write_flush(0);
        tud_cdc_n_write_flush(1);
    }
}

/* -------------------------------------------------------------------------
 * Device callbacks
 * ------------------------------------------------------------------------- */
void tud_mount_cb(void) {}

void tud_umount_cb(void) {
    xEventGroupClearBits(usb_event_group, USB_EVT_CDC0_CONNECTED | USB_EVT_CDC1_CONNECTED);
}

void tud_suspend_cb(bool remote_wakeup_en) {
    (void)remote_wakeup_en;
    xEventGroupClearBits(usb_event_group, USB_EVT_CDC0_CONNECTED | USB_EVT_CDC1_CONNECTED);
}

void tud_resume_cb(void) {}

/* -------------------------------------------------------------------------
 * CDC callbacks — dispatch para handler registrado pelo módulo dono
 * Chamados em contexto de usb_device_task (task level, NÃO ISR hardware).
 * Use APIs FreeRTOS normais dentro dos handlers (sem FromISR).
 * ------------------------------------------------------------------------- */
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts) {
    (void)rts;
    EventBits_t bit = (itf == 0) ? USB_EVT_CDC0_CONNECTED : USB_EVT_CDC1_CONNECTED;
    if (dtr) {
    	xEventGroupSetBits(usb_event_group, bit);
    }
    else {
    	xEventGroupClearBits(usb_event_group, bit);
    }
}

void tud_cdc_rx_cb(uint8_t itf) {
    if (itf < 2 && rx_handlers[itf] != NULL) {
        rx_handlers[itf](itf);
    }
}

void tud_cdc_tx_complete_cb(uint8_t itf) {
    if (itf < 2 && tx_done_handlers[itf] != NULL) {
        tx_done_handlers[itf](itf);
    }
}
