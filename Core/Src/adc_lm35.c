/*
 * adc_lm35.c
 *
 * Leitura do LM35 via ADC1_IN0 (PA0) com DMA circular.
 *
 * Pipeline:
 *   TIM2 TRGO (1kHz) -> ADC1_IN0 -> DMA2 Str0 Ch0 (circular, LM35_BATCH_SIZE amostras)
 *   -> HAL_ADC_ConvCpltCallback -> xTaskNotifyGiveFromISR
 *   -> adc_lm35_task acorda -> converte raw->°C -> atualiza g_temp_lm35
 *
 * Sensor: LM35 em PA0
 *   Saida: 10mV/°C, VREF = 3.3V, ADC 12-bit
 *   Conversao: temp_C = raw * (330.0f / 4095.0f)
 *
 * Nota: STM32F4 nao possui HAL_ADCEx_Calibration_Start (diferente do G4).
 */

#include "adc_lm35.h"
#include "main.h"
#include "cmsis_os.h"
#include "tusb.h"

/* Handles gerados pelo CubeMX em main.c */
extern ADC_HandleTypeDef hadc1;
extern TIM_HandleTypeDef htim2;

/* Buffer DMA — LM35_BATCH_SIZE amostras de 12 bits */
static uint16_t adc_buf[LM35_BATCH_SIZE];

/* Handle da task para notificacao via IRQ */
static TaskHandle_t adc_task_handle = NULL;

/* Variavel compartilhada com modbus_task */
volatile float g_temp_lm35 = 0.0f;

/* -------------------------------------------------------------------------
 * HAL_ADC_ConvCpltCallback
 *
 * Chamado pela IRQ DMA2 Stream0 apos LM35_BATCH_SIZE conversoes completas.
 * Notifica adc_lm35_task para processar o lote.
 * ------------------------------------------------------------------------- */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance != ADC1) {
        return;
    }
    if (adc_task_handle == NULL) {
        return;
    }

    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(adc_task_handle, &woken);
    portYIELD_FROM_ISR(woken);
}

/* -------------------------------------------------------------------------
 * adc_lm35_task
 *
 * Aguarda USB conectar, arma DMA circular + TIM2, depois bloqueia em
 * ulTaskNotifyTake ate callback sinalizar lote pronto.
 * Calcula media das amostras e converte para graus Celsius.
 * ------------------------------------------------------------------------- */
void adc_lm35_task(void *param)
{
    (void)param;

    adc_task_handle = xTaskGetCurrentTaskHandle();

    /* Aguarda USB conectar antes de iniciar */
    while (!tud_cdc_connected()) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    /* Arma DMA circular — callback dispara apos LM35_BATCH_SIZE conversoes */
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_buf, LM35_BATCH_SIZE);

    /* Liga TIM2 — gera TRGO a 1kHz, dispara ADC */
    HAL_TIM_Base_Start(&htim2);

    /* IIR: g_temp = α×nova + (1-α)×anterior
     * α=0.05, update 100x/s → τ ≈ 0.2s (suaviza ruído ADC, segue LM35 sem lag) */
    static const float IIR_ALPHA = 0.05f;

    while (1) {
        /* Bloqueia ate DMA completar lote (10ms a 1kHz) */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        /* Media das amostras do lote */
        uint32_t soma = 0;
        for (int i = 0; i < LM35_BATCH_SIZE; i++) {
            soma += adc_buf[i];
        }
        uint16_t media_raw = (uint16_t)(soma / LM35_BATCH_SIZE);

        /* LM35: 10mV/°C, VREF=3.3V, ADC 12-bit */
        float nova = media_raw * (330.0f / 4095.0f);

        /* Inicializa sem blend na primeira amostra */
        g_temp_lm35 = (g_temp_lm35 == 0.0f)
                      ? nova
                      : IIR_ALPHA * nova + (1.0f - IIR_ALPHA) * g_temp_lm35;
    }
}
