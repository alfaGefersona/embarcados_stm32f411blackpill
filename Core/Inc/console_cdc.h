/*
 * console_cdc.h
 *
 * Console interativo sobre CDC[1] (segundo COM port USB).
 * Adicionar comando: implementar funcao em console_cdc.c e registrar em process_line().
 */

#ifndef INC_CONSOLE_CDC_H_
#define INC_CONSOLE_CDC_H_

/* Inicializa filas, semaforo e tasks do console.
 * Chamar no main.c, dentro de RTOS_THREADS, antes de osKernelStart(). */
void console_cdc_init(void);

/* Envia string para o console — thread-safe, nao bloqueia caller. */
void console_print(const char *text);

#endif /* INC_CONSOLE_CDC_H_ */
