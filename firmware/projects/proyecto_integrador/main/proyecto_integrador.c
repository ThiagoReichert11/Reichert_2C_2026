/**
 * @file reaction_timer_multitask.c
 * @brief Entrenador de Tiempo de Reacción con Arquitectura Multitarea en FreeRTOS.
 *        Incluye reportes por UART al encender/apagar y al cambiar de modo.
 */

/*==================[inclusions]=============================================*/
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "neopixel_stripe.h"
#include "switch.h"
#include "timer_mcu.h"
#include "uart_mcu.h"
#include "gpio_mcu.h"

/*==================[macros and definitions]=================================*/
#define NUM_LEDS             12
#define LED_GPIO             GPIO_9
#define EXTERNAL_BUTTON_GPIO GPIO_0
#define MIN_OFF_US           1000000   // 1 segundo
#define MAX_OFF_US           10000000  // 10 segundos
#define TIMEOUT_MS           3000      // 3 segundos límite

typedef enum {
    MODE_RED = 1, MODE_YELLOW, MODE_GREEN, MODE_RANDOM, MODE_GONOGO
} color_mode_t;

typedef enum {
    LED_CMD_OFF,
    LED_CMD_SET_COLOR
} led_cmd_type_t;

typedef struct {
    led_cmd_type_t command;
    neopixel_color_t color;
} led_message_t;

/*==================[internal data definition]===============================*/
static neopixel_color_t led_colors[NUM_LEDS];

// Handles de FreeRTOS
static TaskHandle_t control_task_handle = NULL;
static QueueHandle_t led_queue = NULL;
static QueueHandle_t uart_rx_queue = NULL;

static volatile bool activo = false;
static volatile color_mode_t current_mode = MODE_RED;
static volatile bool reaction_in_progress = false;
static volatile int32_t final_reaction_time = 0;

/*==================[auxiliary functions]====================================*/
static void SendLedCommand(led_cmd_type_t cmd, neopixel_color_t color) {
    led_message_t msg = { .command = cmd, .color = color };
    xQueueSend(led_queue, &msg, portMAX_DELAY);
}

static neopixel_color_t GetLedColor(color_mode_t mode) {
    if (mode == MODE_RANDOM) mode = (rand() % 3) + 1;
    switch (mode) {
        case MODE_RED:    return NeoPixelRgb2Color(255, 0, 0);
        case MODE_YELLOW: return NeoPixelRgb2Color(255, 150, 0);
        case MODE_GREEN:  return NeoPixelRgb2Color(0, 255, 0);
        default:          return NeoPixelRgb2Color(255, 0, 0);
    }
}

/*==================[ISRs & Callbacks]=======================================*/
void TimerACallback(void *p) {
    vTaskNotifyGiveFromISR(control_task_handle, NULL);
}

static void RecordReactionISR(void) {
    if (activo && reaction_in_progress) {
        uint32_t elapsed_us = TimerRead(TIMER_B);
        TimerStop(TIMER_B);
        final_reaction_time = (int32_t)(elapsed_us / 1000);
        reaction_in_progress = false;

        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        vTaskNotifyGiveFromISR(control_task_handle, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
    }
}

void ButtonReactionISR(void *p) { RecordReactionISR(); }

void Switch2Callback(void *p) {
    activo = !activo;
    vTaskNotifyGiveFromISR(control_task_handle, NULL);
}

void UartReceiveISR(void *p) {
    uint8_t c;
    UartReadByte(UART_PC, &c);
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(uart_rx_queue, &c, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}

/*==================[FreeRTOS Tasks]==========================================*/

/**
 * @brief TAREA 1: Manejo exclusivo de NeoPixels (Prioridad Baja - 2)
 */
static void LedTask(void *pvParameter) {
    led_message_t msg;
    while (true) {
        if (xQueueReceive(led_queue, &msg, portMAX_DELAY) == pdTRUE) {
            if (msg.command == LED_CMD_OFF) {
                NeoPixelAllOff();
            } else if (msg.command == LED_CMD_SET_COLOR) {
                NeoPixelAllColor(msg.color);
            }
        }
    }
}

/**
 * @brief TAREA 2: Decodificación asíncrona de comandos UART con confirmación gráfica (Prioridad Media - 3)
 */
static void UartTask(void *pvParameter) {
    uint8_t cmd;
    while (true) {
        if (xQueueReceive(uart_rx_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            if (cmd == 'a' || cmd == 'A') {
                activo = !activo;
                xTaskNotifyGive(control_task_handle);
            } else if (cmd == '1') {
                current_mode = MODE_RED;
                printf("\n[MODO] Color fijo: ROJO\r\n");
            } else if (cmd == '2') {
                current_mode = MODE_YELLOW;
                printf("\n[MODO] Color fijo: AMARILLO\r\n");
            } else if (cmd == '3') {
                current_mode = MODE_GREEN;
                printf("\n[MODO] Color fijo: VERDE\r\n");
            } else if (cmd == '4') {
                current_mode = MODE_RANDOM;
                printf("\n[MODO] Color: ALEATORIO\r\n");
            } else if (cmd == '5') {
                current_mode = MODE_GONOGO;
                printf("\n[MODO] Go/No-Go (Verde: Presionar | Rojo: Ignorar)\r\n");
            }
        }
    }
}

/**
 * @brief TAREA 3: Orquestador del Experimento y Cronometrado (Prioridad Alta - 4)
 */
static void ControlTask(void *pvParameter) {
    bool last_activo = false;

    printf("=== ENTRENADOR MULTITAREA FREERTOS ===\r\n");

    while (true) {
        if (activo) {
            // Confirmación de Encendido
            if (!last_activo) {
                printf("\n--- DISPOSITIVO ENCENDIDO ---\r\n");
                last_activo = true;
            }

            // 1. Apagar LEDs y programar retardo aleatorio (Timer A)
            SendLedCommand(LED_CMD_OFF, 0);
            uint32_t delay_us = MIN_OFF_US + (rand() % (MAX_OFF_US - MIN_OFF_US + 1));
            TimerStop(TIMER_A);
            TimerUpdatePeriod(TIMER_A, delay_us);
            TimerReset(TIMER_A);
            TimerStart(TIMER_A);

            printf("Esperando estímulo...\r\n");

            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            TimerStop(TIMER_A);
            if (!activo) continue;

            // 2. Determinar estímulo y color
            bool is_red_gonogo = (current_mode == MODE_GONOGO && (rand() % 2 == 1));
            neopixel_color_t color = is_red_gonogo ? NeoPixelRgb2Color(255, 0, 0) : GetLedColor(current_mode);

            // 3. Encender LEDs y arrancar cronómetro (Timer B)
            SendLedCommand(LED_CMD_SET_COLOR, color);
            TimerStop(TIMER_B);
            TimerReset(TIMER_B);
            TimerStart(TIMER_B);
            reaction_in_progress = true;

            // 4. Esperar presionado de botón (Timeout 3s)
            uint32_t pressed = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(TIMEOUT_MS));
            SendLedCommand(LED_CMD_OFF, 0);
            if (!activo) continue;

            // 5. Evaluación de resultados
            if (current_mode == MODE_GONOGO && is_red_gonogo) {
                if (pressed) {
                    printf("¡ERROR! Falsa alarma (Presionó en ROJO)\r\n");
                } else {
                    TimerStop(TIMER_B);
                    reaction_in_progress = false;
                    printf("¡CORRECTO! Inhibición lograda en ROJO\r\n");
                }
            } else {
                if (pressed && !reaction_in_progress) {
                    printf("¡Acierto! Tiempo de reacción: %ld ms\r\n", (long)final_reaction_time);
                } else {
                    TimerStop(TIMER_B);
                    reaction_in_progress = false;
                    printf(current_mode == MODE_GONOGO ? "Tiempo agotado. Omisión en VERDE.\r\n" 
                                                       : "Tiempo agotado. Demasiado lento.\r\n");
                }
            }

            if (activo) vTaskDelay(pdMS_TO_TICKS(2000));
        } else {
            // Confirmación de Apagado
            if (last_activo) {
                printf("\n--- DISPOSITIVO APAGADO ---\r\n");
                last_activo = false;
            }
            SendLedCommand(LED_CMD_OFF, 0);
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        }
    }
}

/*==================[main]===================================================*/
void app_main(void) {
    // 1. Inicialización de periféricos
    NeoPixelInit(LED_GPIO, NUM_LEDS, led_colors);
    NeoPixelBrightness(100);

    SwitchesInit();
    SwitchActivInt(SWITCH_1, ButtonReactionISR, NULL);
    SwitchActivInt(SWITCH_2, Switch2Callback, NULL);

    GPIOInit(EXTERNAL_BUTTON_GPIO, GPIO_INPUT);
    GPIOInputFilter(EXTERNAL_BUTTON_GPIO);
    GPIOActivInt(EXTERNAL_BUTTON_GPIO, ButtonReactionISR, false, NULL);

    serial_config_t uart_cfg = { .port = UART_PC, .baud_rate = 115200, .func_p = UartReceiveISR, .param_p = NULL };
    UartInit(&uart_cfg);

    timer_config_t timer_a_cfg = { .timer = TIMER_A, .period = 1000000, .func_p = TimerACallback, .param_p = NULL };
    TimerInit(&timer_a_cfg);

    timer_config_t timer_b_cfg = { .timer = TIMER_B, .period = 0xFFFFFFFF, .func_p = NULL, .param_p = NULL };
    TimerInit(&timer_b_cfg);

    // 2. Creación de Colas IPC
    led_queue = xQueueCreate(5, sizeof(led_message_t));
    uart_rx_queue = xQueueCreate(10, sizeof(uint8_t));

    // 3. Creación de Tareas por Prioridad
    xTaskCreate(LedTask, "LedTask", 2048, NULL, 2, NULL);
    xTaskCreate(UartTask, "UartTask", 2048, NULL, 3, NULL);
    xTaskCreate(ControlTask, "ControlTask", 3072, NULL, 4, &control_task_handle);
}