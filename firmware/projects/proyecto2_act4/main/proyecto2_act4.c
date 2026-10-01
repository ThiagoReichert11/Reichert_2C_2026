/**
 * @file proyecto2_act4.c
 * @brief Digitalización analógica a 500 Hz y generación de onda ECG por DAC a 250 Hz con tareas independientes.
 *
 * @mainpage Digitalización (ADC) y Generación (DAC) Multitarea
 *
 * \section genDesc Descripción General
 *
 * Este programa separa la generación de la señal ECG (DAC) y la adquisición analógica (ADC + UART)
 * en dos tareas independientes de FreeRTOS controladas por temporizadores distintos:
 * - **Timer A (500 Hz):** Dispara la lectura del ADC (CH1) y la transmisión de datos por UART.
 * - **Timer B (250 Hz):** Dispara la salida del DAC enviando las muestras del vector ECG.
 * 
 * @section changelog Changelog
 *
 * |    Date    | Description                                    |
 * |:----------:|:-----------------------------------------------|
 * | 24/09/2026 | Creación y corrección del programa             |
 *
 * @author Reichert Thiago
 *
 */

/*==================[inclusions]=============================================*/
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "analog_io_mcu.h"
#include "uart_mcu.h"
#include "timer_mcu.h"

/*==================[macros and definitions]=================================*/
/**
 * @def CONFIG_PERIOD_TIMER_A_US
 * @brief Período del Timer A en microsegundos para fs = 500 Hz (T = 2000 us) -> ADC + UART.
 */
#define CONFIG_PERIOD_TIMER_A_US 2000

/**
 * @def CONFIG_PERIOD_TIMER_B_US
 * @brief Período del Timer B en microsegundos para fs = 250 Hz (T = 4000 us) -> DAC.
 */
#define CONFIG_PERIOD_TIMER_B_US 4000

/**
 * @def BUFFER_SIZE
 * @brief Cantidad de muestras del ciclo completo de la señal ECG.
 */
#define BUFFER_SIZE 231

/*==================[internal data definition]===============================*/
/**
 * @var ecg
 * @brief Vector constante con las muestras digitalizadas de la señal ECG.
 */
const char ecg[BUFFER_SIZE] = {
    76, 77, 78, 77, 79, 86, 81, 76, 84, 93, 85, 80,
    89, 95, 89, 85, 93, 98, 94, 88, 98, 105, 96, 91,
    99, 105, 101, 96, 102, 106, 101, 96, 100, 107, 101,
    94, 100, 104, 100, 91, 99, 103, 98, 91, 96, 105, 95,
    88, 95, 100, 94, 85, 93, 99, 92, 84, 91, 96, 87, 80,
    83, 92, 86, 78, 84, 89, 79, 73, 81, 83, 78, 70, 80, 82,
    79, 69, 80, 82, 81, 70, 75, 81, 77, 74, 79, 83, 82, 72,
    80, 87, 79, 76, 85, 95, 87, 81, 88, 93, 88, 84, 87, 94,
    86, 82, 85, 94, 85, 82, 85, 95, 86, 83, 92, 99, 91, 88,
    94, 98, 95, 90, 97, 105, 104, 94, 98, 114, 117, 124, 144,
    180, 210, 236, 253, 227, 171, 99, 49, 34, 29, 43, 69, 89,
    89, 90, 98, 107, 104, 98, 104, 110, 102, 98, 103, 111, 101,
    94, 103, 108, 102, 95, 97, 106, 100, 92, 101, 103, 100, 94, 98,
    103, 96, 90, 98, 103, 97, 90, 99, 104, 95, 90, 99, 104, 100, 93,
    100, 106, 101, 93, 101, 105, 103, 96, 105, 112, 105, 99, 103, 108,
    99, 96, 102, 106, 99, 90, 92, 100, 87, 80, 82, 88, 77, 69, 75, 79,
    74, 67, 71, 78, 72, 67, 73, 81, 77, 71, 75, 84, 79, 77, 77, 76, 76,
};

/**
 * @var dac_index
 * @brief Índice para el recorrido circular del buffer de la señal ECG.
 */
uint8_t dac_index = 0;

/**
 * @var valor_adc
 * @brief Almacena el resultado de la conversión analógico-digital de 12 bits.
 */
uint16_t valor_adc = 0;

/**
 * @var adc_task_handle
 * @brief Manejador de la tarea encargada del muestreo por ADC y transmisión por UART.
 */
TaskHandle_t adc_task_handle = NULL;

/**
 * @var dac_task_handle
 * @brief Manejador de la tarea encargada de la generación analógica por DAC.
 */
TaskHandle_t dac_task_handle = NULL;

/**
 * @var my_uart
 * @brief Configuración del puerto de comunicación serie UART.
 */
serial_config_t my_uart = {
    .port      = UART_PC,
    .baud_rate = 115200,
    .func_p    = UART_NO_INT,
    .param_p   = NULL
};

/**
 * @var config_adc
 * @brief Configuración de la entrada analógica en el canal CH1.
 */
analog_input_config_t config_adc = {
    .input     = CH1,
    .mode      = ADC_SINGLE,
    .func_p    = NULL,
    .param_p   = NULL
};

/*==================[internal functions declaration]=========================*/
/**
 * @brief Callback de interrupción del Timer A disparado periódicamente a 500 Hz.
 *
 * @param[in] param Puntero genérico a parámetros de la interrupción (no utilizado).
 */
void FuncTimerA(void *param){
    vTaskNotifyGiveFromISR(adc_task_handle, NULL);
}

/**
 * @brief Callback de interrupción del Timer B disparado periódicamente a 250 Hz.
 *
 * @param[in] param Puntero genérico a parámetros de la interrupción (no utilizado).
 */
void FuncTimerB(void *param){
    vTaskNotifyGiveFromISR(dac_task_handle, NULL);
}

/**
 * @brief Tarea encargada de la lectura del ADC y envío por UART (Sincronizada a 500 Hz por Timer A).
 *
 * @param[in] pvParameter Puntero genérico a parámetros de FreeRTOS (no utilizado).
 */
static void AdcTask(void *pvParameter){
    while(true){
        /* Espera la interrupción del Timer A */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        /* Lectura del canal CH1 del conversor AD */
        AnalogInputReadSingle(CH1, &valor_adc);

        /* Transmisión serie a la PC */
        UartSendString(UART_PC, (char *)UartItoa(valor_adc, 10));
        UartSendString(UART_PC, "\r\n");
    }
}

/**
 * @brief Tarea encargada de la generación analógica por DAC (Sincronizada a 250 Hz por Timer B).
 *
 * @param[in] pvParameter Puntero genérico a parámetros de FreeRTOS (no utilizado).
 */
static void DacTask(void *pvParameter){
    uint8_t escribir = 0; /* Variable local encapsulada */

    while(true){
        /* Espera la interrupción del Timer B */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        /* Generación de muestra analógica por DAC */
        escribir = ecg[dac_index];
        AnalogOutputWrite(escribir);

        /* Actualización circular del índice */
        dac_index++;
        if (dac_index >= BUFFER_SIZE) {
            dac_index = 0;
        }
    }
}

/*==================[external functions definition]==========================*/
/**
 * @brief Punto de entrada principal de la aplicación.
 */
void app_main(void){
    /* Inicialización de periféricos */
    UartInit(&my_uart);
    AnalogInputInit(&config_adc);
    AnalogOutputInit();

    /* Configuración del Timer A (500 Hz -> ADC + UART) */
    timer_config_t timer_A = {
        .timer   = TIMER_A,
        .period  = CONFIG_PERIOD_TIMER_A_US,
        .func_p  = FuncTimerA,
        .param_p = NULL
    };
    TimerInit(&timer_A);

    /* Configuración del Timer B (250 Hz -> DAC) */
    timer_config_t timer_B = {
        .timer   = TIMER_B,
        .period  = CONFIG_PERIOD_TIMER_B_US,
        .func_p  = FuncTimerB,
        .param_p = NULL
    };
    TimerInit(&timer_B);

    /* Creación de tareas en FreeRTOS */
    xTaskCreate(&AdcTask, "ADC_UART", 2048, NULL, 5, &adc_task_handle);
    xTaskCreate(&DacTask, "DAC_ECG",  2048, NULL, 5, &dac_task_handle);

    /* Puesta en marcha de ambos temporizadores */
    TimerStart(TIMER_A);
    TimerStart(TIMER_B);
}