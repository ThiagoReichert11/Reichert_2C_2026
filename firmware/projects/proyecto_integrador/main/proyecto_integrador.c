/**
 * @file proyecto_integrador.c
 * @brief Entrenador de Tiempo de Reacción con arquitectura multitarea en FreeRTOS.
 * @details Implementa un sistema embebido determinístico sobre ESP32 para medir tiempos
 *          de reacción sensoriomotora y control inhibitorio (paradigma Go/No-Go).
 *          Utiliza dos tareas de FreeRTOS (`UartTask` y `ControlTask`) sincronizadas
 *          mediante notificaciones directas desde interrupciones,
 *          dos temporizadores por hardware (`TIMER_A` y `TIMER_B`), tira de LEDs
 *          NeoPixel WS2812 y comunicación serie UART a 115200 baudios.
 * 
 * * @section hardConn Hardware Connection
 *
 * |    Peripheral     |   ESP32-C6    |
 * |:-----------------:|:--------------|
 * |  NeoPixel Stripe  |    GPIO_9     |
 * |  External Button  |    GPIO_0     |
 * |  Switch 1 (Board) |    GPIO_4     |
 * |  Switch 2 (Board) |    GPIO_15    |
 * |  UART PC          |    USB        |
 * 
 * @author Thiago Reichert
 * @version 1.0
 * @date 2026-11-14
 */

/*==================[inclusions]=============================================*/
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "neopixel_stripe.h"
#include "switch.h"
#include "timer_mcu.h"
#include "uart_mcu.h"
#include "gpio_mcu.h"

/*==================[macros and definitions]=================================*/
/** @brief Cantidad total de LEDs RGB en la tira NeoPixel. */
#define NUM_LEDS             12

/** @brief Pin GPIO conectado a la línea de datos de la tira NeoPixel. */
#define LED_GPIO             GPIO_9

/** @brief Pin GPIO configurado como entrada para el pulsador externo de respuesta. */
#define EXTERNAL_BUTTON_GPIO GPIO_0

/** @brief Tiempo mínimo de espera aleatoria antes de encender el estímulo (1 segundo en us). */
#define MIN_OFF_US           1000000

/** @brief Tiempo máximo de espera aleatoria antes de encender el estímulo (10 segundos en us). */
#define MAX_OFF_US           10000000

/** @brief Tiempo límite máximo de espera para registrar una pulsación (3 segundos en ms). */
#define TIMEOUT_MS           3000

/**
 * @brief Modos de operación disponibles para el estímulo visual.
 */
typedef enum {
    MODE_RED = 1,   /**< Estímulo fijo de color rojo. */
    MODE_YELLOW,    /**< Estímulo fijo de color amarillo. */
    MODE_GREEN,     /**< Estímulo fijo de color verde. */
    MODE_RANDOM,    /**< Selección pseudoaleatoria entre rojo, amarillo y verde. */
    MODE_GONOGO     /**< Modo discriminativo Go/No-Go (Verde: presionar; Rojo: inhibir). */
} color_mode_t;

/*==================[internal data definition]===============================*/
/** @brief Arreglo que almacena el color RGB individual de cada LED de la tira NeoPixel. */
static neopixel_color_t led_colors[NUM_LEDS];

/** @brief Handle de FreeRTOS de la tarea principal de control (`ControlTask`). */
static TaskHandle_t control_task_handle = NULL;

/** @brief Handle de FreeRTOS de la tarea de recepción de comandos UART (`UartTask`). */
static TaskHandle_t uart_task_handle = NULL;

/** @brief Bandera de estado general del dispositivo (`true`: encendido, `false`: apagado). */
static volatile bool activo = false;

/** @brief Modo de estímulo visual seleccionado actualmente. */
static volatile color_mode_t current_mode = MODE_RED;

/** @brief Indica si hay un estímulo activo esperando respuesta del usuario. */
static volatile bool reaction_in_progress = false;

/** @brief Tiempo de reacción medido en el último ensayo válido, expresado en milisegundos. */
static volatile int32_t final_reaction_time = 0;

/*==================[auxiliary functions]====================================*/
/**
 * @brief Obtiene el color RGB de 24 bits correspondiente al modo de estímulo configurado.
 * @details Si el modo recibido es `MODE_RANDOM`, selecciona de forma pseudoaleatoria
 *          uno de los tres colores básicos (`MODE_RED`, `MODE_YELLOW` o `MODE_GREEN`).
 * @param mode Modo de color activo (`color_mode_t`).
 * @return neopixel_color_t Estructura de color en formato compatible con el driver NeoPixel.
 */
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
/**
 * @brief Callback de interrupción por vencimiento del temporizador de espera (`TIMER_A`).
 * @details Notifica directamente a `ControlTask` que finalizó el retardo aleatorio
 *          previo a la presentación del estímulo visual.
 * @param p Puntero genérico a parámetros de usuario (no utilizado).
 * @note Se ejecuta en contexto de interrupción (ISR).
 */
void TimerACallback(void *p) {
    if (control_task_handle != NULL) {
        vTaskNotifyGiveFromISR(control_task_handle, NULL);
    }
}

/**
 * @brief Rutina interna de captura instantánea del tiempo de reacción.
 * @details Si el equipo está activo y existe un ensayo en curso, lee el contador de
 *          microsegundos de `TIMER_B`, detiene el cronómetro, convierte el resultado a
 *          milisegundos, desactiva `reaction_in_progress` para inmunizar contra rebotes
 *          mecánicos y despierta inmediatamente a `ControlTask`.
 * @note Se ejecuta en contexto de interrupción (ISR).
 */
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

/**
 * @brief Callback de interrupción para los pulsadores de respuesta (`SWITCH_1` y `GPIO_0`).
 * @param p Puntero genérico a parámetros de usuario (no utilizado).
 * @note Se ejecuta en contexto de interrupción (ISR).
 */
void ButtonReactionISR(void *p) {
    RecordReactionISR();
}

/**
 * @brief Callback de interrupción del pulsador de encendido/apagado (`SWITCH_2`).
 * @details Conmuta el estado lógico de la variable `activo` y notifica a `ControlTask`
 *          para iniciar o detener el ciclo de ensayos.
 * @param p Puntero genérico a parámetros de usuario (no utilizado).
 * @note Se ejecuta en contexto de interrupción (ISR).
 */
void Switch2Callback(void *p) {
    activo = !activo;
    if (control_task_handle != NULL) {
        vTaskNotifyGiveFromISR(control_task_handle, NULL);
    }
}

/**
 * @brief Callback de interrupción por recepción de byte en el puerto serie (`UART_PC`).
 * @details Lee el carácter entrante y lo transfiere directamente al registro de notificación
 *          de 32 bits de `UartTask` mediante `xTaskNotifyFromISR` con sobrescritura,
 *          eliminando la necesidad de colas de mensajes.
 * @param p Puntero genérico a parámetros de usuario (no utilizado).
 * @note Se ejecuta en contexto de interrupción (ISR).
 */
void UartReceiveISR(void *p) {
    uint8_t c;
    UartReadByte(UART_PC, &c);
    if (uart_task_handle != NULL) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xTaskNotifyFromISR(uart_task_handle, (uint32_t)c, eSetValueWithOverwrite, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
    }
}

/*==================[FreeRTOS Tasks]==========================================*/
/**
 * @brief Tarea de FreeRTOS encargada de decodificar comandos recibidos por UART.
 * @details Permanece bloqueada en `xTaskNotifyWait` sin consumir tiempo de CPU hasta
 *          que `UartReceiveISR` le envía un carácter. Procesa los comandos `'a'`/`'A'`
 *          (encendido/apagado) y `'1'` a `'5'` (selección de modo), reportando cada
 *          cambio por salida estándar (`printf`).
 * @param pvParameter Puntero de parámetros de tarea de FreeRTOS (no utilizado).
 * @note Prioridad asignada: 3 (Media).
 */
static void UartTask(void *pvParameter) {
    uint32_t cmd;
    while (true) {
        if (xTaskNotifyWait(0, 0xFFFFFFFF, &cmd, portMAX_DELAY) == pdTRUE) {
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
 * @brief Tarea de FreeRTOS orquestadora del experimento, estímulos y cronometrado.
 * @details Gestiona la máquina de estados completa de cada ensayo:
 *          1. Apaga la tira NeoPixel y configura `TIMER_A` con un retardo aleatorio
 *             entre `MIN_OFF_US` y `MAX_OFF_US`.
 *          2. Determina el color del estímulo (incluyendo la probabilidad del 50 % entre
 *             Rojo/No-Go y Verde/Go cuando `current_mode == MODE_GONOGO`).
 *          3. Enciende los NeoPixels e inicia el cronómetro `TIMER_B`.
 *          4. Espera la notificación de pulsación con un límite máximo de `TIMEOUT_MS`.
 *          5. Evalúa el resultado (acierto con tiempo en ms, inhibición correcta,
 *             falsa alarma u omisión) y lo transmite por UART hacia la aplicación en PC.
 * @param pvParameter Puntero de parámetros de tarea de FreeRTOS (no utilizado).
 * @note Prioridad asignada: 4 (Alta).
 */
static void ControlTask(void *pvParameter) {
    bool last_activo = false;

    printf("=== ENTRENADOR MULTITAREA FREERTOS ===\r\n");

    while (true) {
        if (activo) {
            if (!last_activo) {
                printf("\n--- DISPOSITIVO ENCENDIDO ---\r\n");
                last_activo = true;
            }

            NeoPixelAllOff();
            uint32_t delay_us = MIN_OFF_US + (rand() % (MAX_OFF_US - MIN_OFF_US + 1));
            TimerStop(TIMER_A);
            TimerUpdatePeriod(TIMER_A, delay_us);
            TimerReset(TIMER_A);
            TimerStart(TIMER_A);

            printf("Esperando estímulo...\r\n");

            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            TimerStop(TIMER_A);
            if (!activo) continue;

            bool is_red_gonogo = false;
            neopixel_color_t color;

            if (current_mode == MODE_GONOGO) {
                if (rand() % 2 == 1) {
                    is_red_gonogo = true;
                    color = NeoPixelRgb2Color(255, 0, 0);
                } else {
                    is_red_gonogo = false;
                    color = NeoPixelRgb2Color(0, 255, 0);
                }
            } else {
                color = GetLedColor(current_mode);
            }

            NeoPixelAllColor(color);
            TimerStop(TIMER_B);
            TimerReset(TIMER_B);
            TimerStart(TIMER_B);
            reaction_in_progress = true;

            uint32_t pressed = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(TIMEOUT_MS));
            NeoPixelAllOff();
            if (!activo) continue;

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
            if (last_activo) {
                printf("\n--- DISPOSITIVO APAGADO ---\r\n");
                last_activo = false;
            }
            NeoPixelAllOff();
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        }
    }
}

/*==================[main]===================================================*/
/**
 * @brief Punto de entrada principal de la aplicación en ESP-IDF.
 * @details Inicializa todos los periféricos de hardware (tira NeoPixel, pulsadores
 *          internos `SWITCH_1`/`SWITCH_2`, pulsador externo en `GPIO_0` con filtro
 *          digital contra ruido, interfaz `UART_PC` a 115200 baudios y temporizadores
 *          `TIMER_A` y `TIMER_B`) y crea las tareas `UartTask` y `ControlTask` en el
 *          planificador de FreeRTOS.
 */
void app_main(void) {
    NeoPixelInit(LED_GPIO, NUM_LEDS, led_colors);
    NeoPixelBrightness(255);

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

    xTaskCreate(UartTask, "UartTask", 2048, NULL, 3, &uart_task_handle);
    xTaskCreate(ControlTask, "ControlTask", 3072, NULL, 4, &control_task_handle);
}