/*
 * adc_lm35.h
 *
 *  Leitura do LM35 via ADC1_IN0 (PA0) com DMA circular.
 *  Pipeline: TIM2 TRGO (1kHz) -> ADC1 -> DMA2 Str0 Ch0 -> task notify
 */

#ifndef INC_ADC_LM35_H_
#define INC_ADC_LM35_H_

#include <stdint.h>

/* Numero de amostras por lote DMA (10ms a 1kHz) */
#define LM35_BATCH_SIZE   10

/* Retorna temperatura filtrada em °C.
 * Thread-safe: leitura 32-bit alinhada é atômica em Cortex-M4. */
float adc_lm35_get_temp(void);

void adc_lm35_task(void *param);

#endif /* INC_ADC_LM35_H_ */
