/**
 * @file reaction_timer_complete.c
 * @brief Entrenador de Tiempo de Reacción con SWITCH_2, UART, selección de colores y botón externo (gpio_mcu.h).
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
#define NUM_LEDS             12
#define LED_GPIO             GPIO_9       // Pin físico del NeoPixel
#define EXTERNAL_BUTTON_GPIO GPIO_0      // PRECAUCIÓN: Puede interferir con USB. Cambiar a GPIO_22 si hay fallos de flasheo.
#define MIN_OFF_US           1000000      // Tiempo mínimo de espera al azar (1 segundo en us)
#define MAX_OFF_US           10000000     // Tiempo máximo de espera al azar (10 segundos en us)
#define TIMEOUT_MS           3000         // Tiempo límite para responder (3 segundos)

/*==================[internal data definition]===============================*/
static neopixel_color_t led_colors[NUM_LEDS];
static TaskHandle_t task_handle = NULL;

bool activo = false;

// Modos de color: 1 = Rojo, 2 = Amarillo, 3 = Verde, 4 = Aleatorio
typedef enum {
    COLOR_MODE_RED = 1,
    COLOR_MODE_YELLOW = 2,
    COLOR_MODE_GREEN = 3,
    COLOR_MODE_RANDOM = 4
} color_mode_t;

static volatile color_mode_t current_color_mode = COLOR_MODE_RED;
static volatile bool reaction_in_progress = false;
static volatile int32_t final_reaction_time = 0;

/*==================[internal functions declaration]=========================*/
/**
 * @brief Obtiene el color de los NeoPixels según el modo actual.
 */
static neopixel_color_t GetCurrentLedColor(void) {
    color_mode_t mode = current_color_mode;
    
    if (mode == COLOR_MODE_RANDOM) {
        int r_choice = rand() % 3;
        if (r_choice == 0) mode = COLOR_MODE_RED;
        else if (r_choice == 1) mode = COLOR_MODE_YELLOW;
        else mode = COLOR_MODE_GREEN;
    }

    switch (mode) {
        case COLOR_MODE_RED:
            return NeoPixelRgb2Color(255, 0, 0);
        case COLOR_MODE_YELLOW:
            return NeoPixelRgb2Color(255, 150, 0);
        case COLOR_MODE_GREEN:
            return NeoPixelRgb2Color(0, 255, 0);
        default:
            return NeoPixelRgb2Color(255, 0, 0);
    }
}

/**
 * @brief Callback del Timer A (dispara el fin del intervalo de espera aleatorio).
 */
void TimerACallback(void *param) {
    vTaskNotifyGiveFromISR(task_handle, NULL);
}

/**
 * @brief Función común para procesar la respuesta al estímulo desde cualquier botón.
 */
static void RecordReactionISR(void) {
    if (activo && reaction_in_progress) {
        uint32_t elapsed_us = TimerRead(TIMER_B);
        TimerStop(TIMER_B);
        
        final_reaction_time = (int32_t)(elapsed_us / 1000);
        reaction_in_progress = false;

        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        vTaskNotifyGiveFromISR(task_handle, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken) {
            portYIELD_FROM_ISR();
        }
    }
}

/**
 * @brief Callback de interrupción del SWITCH_1.
 */
void Switch1Callback(void *param) {
    RecordReactionISR();
}

/**
 * @brief Callback de interrupción del botón externo en GPIO_12.
 */
void GpioButtonCallback(void *param) {
    RecordReactionISR();
}

/**
 * @brief Callback de interrupción del SWITCH_2 (Control de encendido/apagado físico).
 */
void Switch2Callback(void *param) {
    activo = !activo;
    vTaskNotifyGiveFromISR(task_handle, NULL);
}

/**
 * @brief Callback de recepción UART (Control de encendido con 'a' y modos de color).
 */
void UartReceive(void *param) {
    uint8_t caracter;
    UartReadByte(UART_PC, &caracter);
    
    if (caracter == 'a' || caracter == 'A') {
        activo = !activo;
        vTaskNotifyGiveFromISR(task_handle, NULL);
    } else if (caracter == '1') {
        current_color_mode = COLOR_MODE_RED;
        printf("\n[MODO] Color fijo: ROJO\r\n");
    } else if (caracter == '2') {
        current_color_mode = COLOR_MODE_YELLOW;
        printf("\n[MODO] Color fijo: AMARILLO\r\n");
    } else if (caracter == '3') {
        current_color_mode = COLOR_MODE_GREEN;
        printf("\n[MODO] Color fijo: VERDE\r\n");
    } else if (caracter == '4') {
        current_color_mode = COLOR_MODE_RANDOM;
        printf("\n[MODO] Color: ALEATORIO\r\n");
    }
}

/**
 * @brief Tarea principal que coordina los ensayos de reacción.
 */
static void ReactionTask(void *pvParameter) {
    bool last_activo = false;

    printf("=== ENTRENADOR DE TIEMPO DE REACCION ===\r\n");
    printf("Presione SWITCH_2, Botón GPIO o envíe 'a' para encender/apagar.\r\n");
    printf("Teclas UART: '1' (Rojo), '2' (Amarillo), '3' (Verde), '4' (Aleatorio).\r\n");

    while (true) {
        if (activo) {
            if (!last_activo) {
                printf("\n--- DISPOSITIVO ENCENDIDO ---\r\n");
                last_activo = true;
            }

            // 1. Apagar LEDs al iniciar el ensayo
            NeoPixelAllOff();

            // 2. Generar intervalo de espera al azar en microsegundos
            uint32_t random_delay_us = MIN_OFF_US + (rand() % (MAX_OFF_US - MIN_OFF_US + 1));

            TimerStop(TIMER_A);
            TimerUpdatePeriod(TIMER_A, random_delay_us);
            TimerReset(TIMER_A);
            TimerStart(TIMER_A);

            printf("Esperando estímulo...\r\n");

            // 3. Bloquear la tarea hasta que el Timer A expire o se modifique el estado
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            TimerStop(TIMER_A);

            // Si se apagó el dispositivo durante la espera, reiniciamos el ciclo
            if (!activo) continue;

            // 4. ESTÍMULO: Obtener el color correspondiente y arrancar Timer B
            neopixel_color_t stimulus_color = GetCurrentLedColor();
            NeoPixelAllColor(stimulus_color);
            
            TimerStop(TIMER_B);
            TimerReset(TIMER_B);
            TimerStart(TIMER_B);
            reaction_in_progress = true;

            // 5. Esperar la respuesta (SWITCH_1 o botón GPIO) con un timeout de 3 segundos
            uint32_t notification = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(TIMEOUT_MS));

            // Apagar los LEDs
            NeoPixelAllOff();

            if (!activo) continue;

            if (notification > 0 && !reaction_in_progress) {
                printf("¡Acierto! Tiempo de reacción: %ld ms\r\n", (long)final_reaction_time);
            } else {
                if (reaction_in_progress) {
                    TimerStop(TIMER_B);
                    reaction_in_progress = false;
                    printf("Tiempo agotado. Demasiado lento.\r\n");
                }
            }

            // Pausa antes del siguiente ensayo (siempre que siga activo)
            if (activo) {
                vTaskDelay(pdMS_TO_TICKS(2000));
            }
        } else {
            if (last_activo) {
                printf("\n--- DISPOSITIVO APAGADO ---\r\n");
                last_activo = false;
            }
            // Sistema apagado: mantener LEDs apagados y bloquear la tarea hasta recibir activación
            NeoPixelAllOff();
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        }
    }
}

/*==================[external functions definition]==========================*/
void app_main(void) {
    // Inicialización de periféricos
    NeoPixelInit(LED_GPIO, NUM_LEDS, led_colors);
    NeoPixelBrightness(100);
    
    // Switches de la placa
    SwitchesInit();
    SwitchActivInt(SWITCH_1, Switch1Callback, NULL);
    SwitchActivInt(SWITCH_2, Switch2Callback, NULL);

    // Configuración del botón externo utilizando el driver gpio_mcu.h
    // GPIO_INPUT ya incluye la resistencia pull-up según la documentación de la cátedra
    GPIOInit(EXTERNAL_BUTTON_GPIO, GPIO_INPUT);
    GPIOInputFilter(EXTERNAL_BUTTON_GPIO); // Filtro anti-rebote
    GPIOActivInt(EXTERNAL_BUTTON_GPIO, GpioButtonCallback, false, NULL); // false = Interrupción por flanco de bajada

    // Configuración del puerto UART
    serial_config_t my_uart = {
        .port = UART_PC,
        .baud_rate = 115200,
        .func_p = UartReceive,
        .param_p = NULL
    };
    UartInit(&my_uart);

    // Configuración del Timer A (gestionará los retardos aleatorios)
    timer_config_t timer_a_conf = {
        .timer = TIMER_A,
        .period = 1000000,
        .func_p = TimerACallback,
        .param_p = NULL
    };
    TimerInit(&timer_a_conf);

    // Configuración del Timer B (cronómetro de alta precisión en microsegundos)
    timer_config_t timer_b_conf = {
        .timer = TIMER_B,
        .period = 0xFFFFFFFF,
        .func_p = NULL,
        .param_p = NULL
    };
    TimerInit(&timer_b_conf);

    // Crear la tarea principal de control en FreeRTOS
    xTaskCreate(
        &ReactionTask,
        "ReactionTask",
        2048,
        NULL,
        4,
        &task_handle
    );
}