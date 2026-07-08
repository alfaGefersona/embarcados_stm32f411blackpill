/*
 * console_cdc.c
 *
 * Console interativo sobre CDC[1] (segundo COM port USB).
 *
 * Pipeline TX: console_print() -> MessageBuffer -> cdc_gatekeeper_task
 *              -> tud_cdc_n_write(1) [FIFO interno TinyUSB]
 *              -> usb_device_task flush automatico (unico dono do flush)
 *
 * Pipeline RX: tud_cdc_rx_cb(itf=1) -> cdc_console_rx_queue (1 byte)
 *              -> console_task bloqueia em xQueueReceive
 *
 * Comandos disponiveis: temp, status, tasks, help
 * Para adicionar comando: seguir padrao de cmd_temp abaixo.
 */

#include "console_cdc.h"
#include "usb_cdc.h"
#include "adc_lm35.h"
#include "tusb.h"
#include "cmsis_os.h"
#include "task.h"
#include "message_buffer.h"
#include <stdio.h>
#include <string.h>

/* Buffer de mensagens TX — gatekeeper drena sequencialmente */
static MessageBufferHandle_t console_msg_buf;

/* Fila RX — preenchida por cdc1_rx_handler, consumida por console_task */
static QueueHandle_t cdc_console_rx_queue;

#define CONSOLE_LINE_MAX   80
#define CONSOLE_MSG_BUF_SZ 512
#define CONSOLE_GKPR_STACK 512
#define CONSOLE_TASK_STACK 512

/* -------------------------------------------------------------------------
 * console_print — thread-safe, nao bloqueia caller alem do envio ao buffer
 * ------------------------------------------------------------------------- */
void console_print(const char *text) {
    size_t len = strlen(text);
    if (len > 0 && console_msg_buf != NULL) {
        xMessageBufferSend(console_msg_buf, text, len, pdMS_TO_TICKS(100));
    }
}

/* -------------------------------------------------------------------------
 * cdc_gatekeeper_task — unico dono do CDC[1] TX
 * ------------------------------------------------------------------------- */
static void cdc_gatekeeper_task(void *param) {
    (void)param;
    static char buf[256];
    size_t len;

    for (;;) {
        /* Bloqueia ate CDC[1] conectar — sem polling */
        xEventGroupWaitBits(usb_event_group, USB_EVT_CDC1_CONNECTED,
                            pdFALSE, pdTRUE, portMAX_DELAY);

        len = xMessageBufferReceive(console_msg_buf, buf, sizeof(buf), portMAX_DELAY);
        if (len > 0) {
            /* Escreve no FIFO interno TinyUSB.
             * usb_device_task e o unico que chama flush — sem acesso concorrente.
             * Loop necessario: FIFO TX = 256 bytes; mensagens longas exigem
             * multiplas escritas intercaladas com flush do usb_device_task. */
            uint32_t sent = 0;
            while (sent < len) {
                /* Aborta se CDC[1] desconectar durante escrita */
                if (!(xEventGroupGetBits(usb_event_group) & USB_EVT_CDC1_CONNECTED)) break;
                uint32_t n = tud_cdc_n_write(1, (uint8_t *)buf + sent, len - sent);
                sent += n;
                if (sent < len) {
                    /* FIFO cheio: cede CPU para usb_device_task drenar */
                    vTaskDelay(1);
                }
            }
        }
    }
}

/* =========================================================================
 * Comandos do console
 * ========================================================================= */

static void cmd_temp(void) {
    char buf[48];
    float   temp   = adc_lm35_get_temp();
    int32_t t_int  = (int32_t)temp;
    int32_t t_frac = (int32_t)((temp - (float)t_int) * 10.0f);
    snprintf(buf, sizeof(buf), "LM35: %ld.%ld C\r\n", t_int, t_frac);
    console_print(buf);
}

static void cmd_status(void) {
    char buf[180];
    size_t livre  = xPortGetFreeHeapSize();
    size_t minimo = xPortGetMinimumEverFreeHeapSize();
    size_t total  = configTOTAL_HEAP_SIZE;
    snprintf(buf, sizeof(buf),
             "=== STATUS ===\r\n"
             "Heap total : %u bytes\r\n"
             "Heap usado : %u bytes (%u%%)\r\n"
             "Heap livre : %u bytes\r\n"
             "Heap minimo: %u bytes\r\n"
             "Tarefas    : %u\r\n",
             (unsigned)total,
             (unsigned)(total - livre), (unsigned)((total - livre) * 100 / total),
             (unsigned)livre,
             (unsigned)minimo,
             (unsigned)uxTaskGetNumberOfTasks());
    console_print(buf);
}

static void cmd_tasks(void) {
    static TaskStatus_t tasks[12];
    uint32_t total_time;
    UBaseType_t n = uxTaskGetSystemState(tasks, 12, &total_time);
    static const char * const state_str[] = { "RUN", "PRON", "BLOQ", "SUSP", "DEL", "?" };

    console_print("Tarefa           Pri  Stack  Estado  CPU%\r\n");
    console_print("------------------------------------------\r\n");
    for (UBaseType_t i = 0; i < n; i++) {
        char line[64];
        uint32_t cpu_pct = (total_time > 0)
                           ? (tasks[i].ulRunTimeCounter * 100UL / total_time)
                           : 0;
        const char *st = (tasks[i].eCurrentState <= eInvalid)
                         ? state_str[tasks[i].eCurrentState]
                         : state_str[5];
        snprintf(line, sizeof(line), "%-16s %3u  %5u  %-6s  %3u%%\r\n",
                 tasks[i].pcTaskName,
                 (unsigned)tasks[i].uxCurrentPriority,
                 (unsigned)tasks[i].usStackHighWaterMark,
                 st,
                 (unsigned)cpu_pct);
        console_print(line);
    }
}

static void cmd_help(void) {
    console_print(
        "Comandos:\r\n"
        "  temp    - temperatura LM35 atual\r\n"
        "  status  - heap e numero de tarefas\r\n"
        "  tasks   - lista todas as tarefas\r\n"
        "  help    - esta mensagem\r\n"
    );
}

static void process_line(const char *line) {
    if      (strcmp(line, "temp")   == 0) cmd_temp();
    else if (strcmp(line, "status") == 0) cmd_status();
    else if (strcmp(line, "tasks")  == 0) cmd_tasks();
    else if (strcmp(line, "help")   == 0) cmd_help();
    else if (strlen(line) > 0) {
        char buf[48];
        snprintf(buf, sizeof(buf), "cmd desconhecido: '%s'\r\n", line);
        console_print(buf);
    }
}

/* -------------------------------------------------------------------------
 * console_task — le bytes da fila, monta linha, processa comando
 * ------------------------------------------------------------------------- */
static void console_task(void *param) {
    (void)param;

    char    line[CONSOLE_LINE_MAX];
    uint8_t idx = 0;
    uint8_t ch;

    /* Bloqueia ate CDC[1] conectar */
    xEventGroupWaitBits(usb_event_group, USB_EVT_CDC1_CONNECTED, pdFALSE, pdTRUE, portMAX_DELAY);

    console_print("\r\n=== Console Trabalho Embarcados ===\r\nDigite 'help'\r\n> ");

    for (;;) {
        /* Bloqueia ate chegar byte — CDC rx callback alimenta a fila */
        if (xQueueReceive(cdc_console_rx_queue, &ch, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (ch == '\r') {
            line[idx] = '\0';
            console_print("\r\n");
            process_line(line);
            idx = 0;
            console_print("> ");
        } else if (ch == '\n') {
            /* ignora LF — evita prompt duplo em terminais CRLF */
        } else if ((ch == '\b' || ch == 127) && idx > 0) {
            /* backspace */
            idx--;
            line[idx] = '\0';
            console_print("\b \b");
        } else if (ch >= 0x20 && idx < CONSOLE_LINE_MAX - 1) {
            /* caractere imprimivel — eco + acumula */
            line[idx++] = (char)ch;
            char echo[2] = { (char)ch, '\0' };
            console_print(echo);
        }
    }
}

/* -------------------------------------------------------------------------
 * cdc1_rx_handler — chamado por usb_device_task via dispatch table
 * Contexto: task (NÃO ISR). Lê CDC[1] e enfileira bytes para console_task.
 * ------------------------------------------------------------------------- */
static void cdc1_rx_handler(uint8_t itf) {
    (void)itf;
    while (tud_cdc_n_available(1)) {
        uint8_t ch;
        tud_cdc_n_read(1, &ch, 1);
        xQueueSendToBack(cdc_console_rx_queue, &ch, 0);
    }
}

/* -------------------------------------------------------------------------
 * console_cdc_init — criar filas, registrar callback e tasks
 * ------------------------------------------------------------------------- */
void console_cdc_init(void) {
    cdc_console_rx_queue = xQueueCreate(64, sizeof(uint8_t));
    configASSERT(cdc_console_rx_queue != NULL);
    console_msg_buf      = xMessageBufferCreate(CONSOLE_MSG_BUF_SZ);
    configASSERT(console_msg_buf != NULL);

    usb_cdc_register_rx_handler(1, cdc1_rx_handler);

    xTaskCreate(cdc_gatekeeper_task, "cdc_gkpr", CONSOLE_GKPR_STACK, NULL, osPriorityNormal, NULL);
    xTaskCreate(console_task,        "console",  CONSOLE_TASK_STACK,  NULL, osPriorityNormal, NULL);
}
