/*
 * usb_cdc.c
 *
 *  Created on: Jul 2, 2026
 *      Author: gefersonartuzo
 */

#include <stdbool.h>
#include "main.h"
#include "cmsis_os.h"
#include "tusb.h"

//#include "pb_encode.h"
//#include "cobs.h"
//#include "dados.pb.h"

// USB Device Driver task
// This top level thread process all usb events and invoke callbacks
void usb_device_task(void* param)
{
  (void) param;

  // init device stack on configured roothub port
  // This should be called after scheduler/kernel is started.
  // Otherwise it could cause kernel issue since USB IRQ handler does use RTOS queue API.
  // Define the device configuration structure
  tusb_rhport_init_t dev_init = {
      .role = TUSB_ROLE_DEVICE,
      .speed = TUSB_SPEED_FULL // or TUSB_SPEED_HIGH
  };

  // Initialize the USB stack
  tusb_init(BOARD_TUD_RHPORT, &dev_init);

  // RTOS forever loop
  while (1)
  {
    // max 2ms wait — garante flush rapido mesmo sem novo evento USB
    tud_task_ext(pdMS_TO_TICKS(2), false);

    // flush ambos CDCs apos processar eventos
    tud_cdc_n_write_flush(0);
    tud_cdc_n_write_flush(1);
  }
}

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void)
{
  //xTimerChangePeriod(blinky_tm, pdMS_TO_TICKS(BLINK_MOUNTED), 0);
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
  //xTimerChangePeriod(blinky_tm, pdMS_TO_TICKS(BLINK_NOT_MOUNTED), 0);
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
  (void) remote_wakeup_en;
  //xTimerChangePeriod(blinky_tm, pdMS_TO_TICKS(BLINK_SUSPENDED), 0);
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
  //xTimerChangePeriod(blinky_tm, pdMS_TO_TICKS(BLINK_MOUNTED), 0);
}


/* --- CDC[0] Modbus --- */
SemaphoreHandle_t cdc_tx_sem;
QueueHandle_t     cdc_rx_queue;
QueueHandle_t     queue_button;

/* --- CDC[1] Console — fila preenchida por console_cdc.c --- */
QueueHandle_t cdc_console_rx_queue;

#define USB_PACKET_SIZE 64

/* Envio pelo CDC[0] (Modbus) */
void tud_cdc_send(uint8_t *buffer, uint32_t bufsize, TickType_t timeout) {
    uint32_t len = 0;
    while (bufsize) {
        len = (bufsize > USB_PACKET_SIZE) ? USB_PACKET_SIZE : bufsize;
        tud_cdc_n_write(0, buffer, len);
        tud_cdc_n_write_flush(0);
        xSemaphoreTake(cdc_tx_sem, timeout);
        buffer  += len;
        bufsize -= len;
    }
}

uint32_t tud_cdc_receive(uint8_t *buffer, uint32_t bufsize, TickType_t timeout){
	uint32_t len;
	xQueueReceive(cdc_rx_queue, &len, timeout);
	if (len > bufsize){
		len = bufsize;
	}
	uint32_t count = tud_cdc_read(buffer, len);
	return count;
}



//// Tamanho do Protobuf (calculado automaticamente pela Nanopb)
//#define BUFFER_PROTOBUF_SIZE   BlocoDados_size
//
//// Tamanho do COBS aplicando a fórmula do pior cenário + 1 byte do 0x00 final
//#define BUFFER_COBS_SIZE       (BUFFER_PROTOBUF_SIZE + (BUFFER_PROTOBUF_SIZE / 254))
//
//uint8_t buffer_protobuf[BUFFER_PROTOBUF_SIZE];
//uint8_t buffer_cobs[BUFFER_COBS_SIZE];
//void enviar_dados_massivos(uint32_t idx) {
//    BlocoDados mensagem = BlocoDados_init_zero;
//    mensagem.timestamp = xTaskGetTickCount();
//    mensagem.id_bloco = idx;
//    mensagem.leituras[0] = 1.0f;
//    mensagem.leituras[1] = 2.0f;
//    mensagem.leituras[2] = 3.0f;
//    mensagem.leituras[3] = 4.0f;
//    mensagem.leituras[4] = 5.0f;
//    mensagem.leituras[5] = 6.0f;
//    mensagem.leituras[6] = 7.0f;
//    mensagem.leituras[7] = 8.0f;
//    mensagem.leituras[8] = 9.0f;
//    mensagem.leituras[9] = 10.0f;
//    mensagem.leituras_count = 10;
//
//    pb_ostream_t stream = pb_ostream_from_buffer(buffer_protobuf, sizeof(buffer_protobuf));
//    if (pb_encode(&stream, BlocoDados_fields, &mensagem)) {
//        size_t tamanho_protobuf = stream.bytes_written;
//
//        // Codifica com COBS (garante que não haverá 0x00 nos dados)
//        size_t tamanho_cobs = cobs_encode(buffer_protobuf, tamanho_protobuf, buffer_cobs);
//
//        // Adiciona o marcador de fim de pacote
//        buffer_cobs[tamanho_cobs] = 0x00;
//        tamanho_cobs++;
//
//        // Envia pela Serial/UART (Ex: HAL_UART_Transmit, Serial.write...)
//        tud_cdc_send(buffer_cobs, tamanho_cobs, portMAX_DELAY);
//    }
//}
//

//--------------------------------------------------------------------+
// USB CDC
//--------------------------------------------------------------------+


void cdc_task(void* params)
{
  (void) params;
  uint8_t buffer[64];

  cdc_rx_queue = xQueueCreate(8, sizeof(uint32_t));
  cdc_tx_sem = xSemaphoreCreateBinary();

  do {
    vTaskDelay(10);
  } while (!tud_cdc_connected());

  // RTOS forever loop — aguarda dados do host (RX)
  while (1)
  {
    uint32_t count = tud_cdc_receive(buffer, sizeof(buffer), portMAX_DELAY);
    if (count) {
      // placeholder: processar comando recebido do host
      (void)count;
    }
  }
}


// Invoked when cdc when line state changed e.g connected/disconnected
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
  (void) itf;
  (void) rts;

  // TODO set some indicator
  if ( dtr )
  {
    // Terminal connected
  }else
  {
    // Terminal disconnected
  }
}

// Invoked when CDC interface received data from host
void tud_cdc_rx_cb(uint8_t itf)
{
    portBASE_TYPE woken = pdFALSE;
    if (itf == 0) {
        /* CDC[0]: Modbus */
        uint32_t len = tud_cdc_n_available(0);
        xQueueSendToBackFromISR(cdc_rx_queue, &len, &woken);
    } else {
        /* CDC[1]: Console — enfileira 1 byte por vez para CLI */
        while (tud_cdc_n_available(1)) {
            uint8_t ch;
            tud_cdc_n_read(1, &ch, 1);
            xQueueSendToBackFromISR(cdc_console_rx_queue, &ch, &woken);
        }
    }
    portYIELD_FROM_ISR(woken);
}

void tud_cdc_tx_complete_cb(uint8_t itf) {
    if (itf == 0) {
        xSemaphoreGive(cdc_tx_sem);
    }
    /* itf == 1 (console): sem controle de fluxo, usb_device_task faz flush */
}
