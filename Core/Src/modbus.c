/*
 * modbus.c
 *
 *  Envia TT-101 (leitura real do LM35 via ADC) como mestre Modbus FC16.
 *  Frame: slave=0x01, FC=0x10, addr=0x0000, qty=2 regs, 4 bytes (float32 BE).
 */

#include <string.h>
#include "modbus.h"
#include "usb_cdc.h"
#include "tusb.h"
#include "cmsis_os.h"
#include "adc_lm35.h"

/* CRC-16/IBM (padrão Modbus) */
static uint16_t modbus_crc16(uint8_t *buf, uint16_t len) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (uint8_t j = 0; j < 8; j++) {
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
        }
    }
    return crc;
}

void modbus_task(void *param) {
    (void)param;

    uint8_t  frame[16];
    uint16_t crc;
    uint8_t  i;

    while (1) {
        if (tud_cdc_connected()) {
            float    temp = g_temp_lm35;
            uint32_t raw;
            memcpy(&raw, &temp, 4);

            /* FC16: addr=0, qty=2, byte_count=4, dados big-endian */
            i = 0;
            frame[i++] = MODBUS_SLAVE_ID;
            frame[i++] = MODBUS_FC16;
            frame[i++] = 0x00; frame[i++] = 0x00;               /* start addr = 0 */
            frame[i++] = 0x00; frame[i++] = MODBUS_TT101_REGS;  /* qty = 2        */
            frame[i++] = MODBUS_TT101_REGS * 2;                  /* byte count = 4 */
            frame[i++] = (raw >> 24) & 0xFF;
            frame[i++] = (raw >> 16) & 0xFF;
            frame[i++] = (raw >>  8) & 0xFF;
            frame[i++] =  raw        & 0xFF;
            crc = modbus_crc16(frame, i);
            frame[i++] = crc & 0xFF;
            frame[i++] = (crc >> 8) & 0xFF;
            /* i == 13 bytes — non-blocking: cabe no FIFO TX (256B), USB drena em background */
            tud_cdc_n_write(0, frame, i);
            tud_cdc_n_write_flush(0);
        }



        //osDelay(1000);
        osDelay(100);    // 10 Hz
        //osDelay(20);
       // osDelay(10);     // 100 Hz
    }
}
