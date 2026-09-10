/**
 * @file proyecto2_act1.c
 * @brief Sistema de medición de distancia con HC-SR04, pantalla LCD y escala de LEDs sobre FreeRTOS.
 *
 * @mainpage Control de Distancia
 *
 * \section genDesc General Description
 *
 * Este programa mide la distancia utilizando un sensor ultrasónico HC-SR04, muestra el valor en un 
 * display LCD y enciende una combinación de LEDs según el rango. Permite iniciar/detener la medición 
 * mediante TEC1 y congelar únicamente la lectura en el LCD (HOLD) mediante TEC2, manteniendo el 
 * control de LEDs en tiempo real.
 * 
 * @section changelog Changelog
 *
 * |    Date    | Description                                    |
 * |:----------:|:-----------------------------------------------|
 * | 03/09/2026 | Document creation                              |
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
#include "led.h"
#include "switch.h"
#include "lcditse0803.h"
#include "hc_sr04.h"

/*==================[macros and definitions]=================================*/
/**
 * @def CONFIG_PERIOD_TECLAS
 * @brief Período de refresco para la lectura de teclas en milisegundos.
 */
#define CONFIG_PERIOD_TECLAS 50

/**
 * @def CONFIG_PERIOD_MEDIR
 * @brief Período de muestreo para el sensor ultrasónico HC-SR04 en milisegundos.
 */
#define CONFIG_PERIOD_MEDIR 1000

/**
 * @def CONFIG_PERIOD_MOSTRAR
 * @brief Período de refresco para la pantalla LCD y la escala de LEDs en milisegundos.
 */
#define CONFIG_PERIOD_MOSTRAR 100

/*==================[internal data definition]===============================*/
/**
 * @var activo
 * @brief Estado global del sistema (true: encendido/midiendo, false: apagado).
 */
bool activo = false;

/**
 * @var hold
 * @brief Estado de retención de lectura en el display (true: congela LCD, false: actualiza LCD).
 */
bool hold = false;

/**
 * @var distancia
 * @brief Almacena la última distancia medida por el sensor HC-SR04 expresada en centímetros.
 */
uint16_t distancia = 0;

/**
 * @var teclas_task_handle
 * @brief Manejador de la tarea encargada de la lectura de las teclas.
 */
TaskHandle_t teclas_task_handle = NULL;

/**
 * @var medir_task_handle
 * @brief Manejador de la tarea encargada del disparo y lectura del sensor ultrasónico.
 */
TaskHandle_t medir_task_handle = NULL;

/**
 * @var mostrar_task_handle
 * @brief Manejador de la tarea encargada de la interfaz gráfica y luminosa (LCD y LEDs).
 */
TaskHandle_t mostrar_task_handle = NULL;

/*==================[internal functions declaration]=========================*/
/**
 * @brief Controla el encendido de los LEDs según la distancia especificada.
 *
 * - Menor a 10 cm: Todos los LEDs apagados.
 * - De 10 a 19 cm: Enciende LED_1.
 * - De 20 a 29 cm: Enciende LED_1 y LED_2.
 * - Mayor o igual a 30 cm: Enciende LED_1, LED_2 y LED_3.
 *
 * @param[in] dist Valor de la distancia medida en centímetros.
 */
static void ControlLeds(uint16_t dist){
    if (dist < 10) {
        LedOff(LED_1);
        LedOff(LED_2);
        LedOff(LED_3);
    } else if (dist >= 10 && dist < 20) {
        LedOn(LED_1);
        LedOff(LED_2);
        LedOff(LED_3);
    } else if (dist >= 20 && dist < 30) {
        LedOn(LED_1);
        LedOn(LED_2);
        LedOff(LED_3);
    } else {
        LedOn(LED_1);
        LedOn(LED_2);
        LedOn(LED_3);
    }
}

/**
 * @brief Tarea de FreeRTOS encarga de escanear periódicamente el estado de las teclas.
 *
 * Evalúa SWITCH_1 para alternar la variable 'activo' y SWITCH_2 para alternar la variable 'hold'.
 *
 * @param[in] pvParameter Puntero genérico a parámetros de FreeRTOS (no utilizado).
 */
static void TeclasTask(void *pvParameter){
    uint8_t teclas;
    while(true){
        teclas = SwitchesRead();
        if(teclas & SWITCH_1){
            activo = !activo;
        }
        if(teclas & SWITCH_2){
            hold = !hold;
        }
        vTaskDelay(CONFIG_PERIOD_TECLAS / portTICK_PERIOD_MS);
    }
}

/**
 * @brief Tarea de FreeRTOS encargada de tomar mediciones con el sensor ultrasónico.
 *
 * Mide la distancia cada 1000 ms en tiempo real siempre que el sistema esté activo.
 *
 * @param[in] pvParameter Puntero genérico a parámetros de FreeRTOS (no utilizado).
 */
static void MedirTask(void *pvParameter){
    while(true){
        if(activo){
            /* Mide la distancia en tiempo real independientemente del estado de hold */
            distancia = HcSr04ReadDistanceInCentimeters();
        }
        vTaskDelay(CONFIG_PERIOD_MEDIR / portTICK_PERIOD_MS);
    }
}

/**
 * @brief Tarea de FreeRTOS encargada del control de periféricos de salida (LCD y LEDs).
 *
 * Inicializa las salidas físicas, actualiza los LEDs según la distancia real en todo momento 
 * y congela el valor del LCD únicamente cuando la variable 'hold' está activa. Si el sistema se 
 * desactiva, apaga todos los periféricos.
 *
 * @param[in] pvParameter Puntero genérico a parámetros de FreeRTOS (no utilizado).
 */
static void MostrarTask(void *pvParameter){
    static uint16_t distancia_lcd = 0;
    
    LedsInit();
    LcdItsE0803Init();
    
    while(true){
        if(activo){
            /* Los LEDs siempre responden a la distancia medida en tiempo real */
            ControlLeds(distancia);

            /* El valor del LCD solo se actualiza si hold está desactivado */
            if(!hold){
                distancia_lcd = distancia;
            }
            
            LcdItsE0803Write(distancia_lcd);
        } else {
            LedOff(LED_1);
            LedOff(LED_2);
            LedOff(LED_3);
            LcdItsE0803Off();
        }
        vTaskDelay(CONFIG_PERIOD_MOSTRAR / portTICK_PERIOD_MS);
    }
}

/*==================[external functions definition]==========================*/
/**
 * @brief Punto de entrada principal de la aplicación ESP-IDF.
 *
 * Inicializa el hardware de pulsadores y sensor HC-SR04, e instancia las tres tareas concurrentes en FreeRTOS.
 */
void app_main(void){
    SwitchesInit();
    HcSr04Init(GPIO_3, GPIO_2);
    
    xTaskCreate(&TeclasTask, "Teclas", 512, NULL, 5, &teclas_task_handle);
    xTaskCreate(&MedirTask, "Medir", 512, NULL, 5, &medir_task_handle);
    xTaskCreate(&MostrarTask, "Mostrar", 512, NULL, 5, &mostrar_task_handle);
}