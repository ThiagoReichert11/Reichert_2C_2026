/**
 * @file proyecto2_act1.c
 * @brief Sistema de medición de distancia con HC-SR04, pantalla LCD y escala de LEDs sobre FreeRTOS mediante interrupciones.
 *
 * @mainpage Control de Distancia
 *
 * \section genDesc General Description
 *
 * Este programa mide la distancia utilizando un sensor ultrasónico HC-SR04, muestra el valor en un 
 * display LCD y enciende una combinación de LEDs según el rango. Permite iniciar/detener la medición 
 * mediante TEC1 y congelar únicamente la lectura en el LCD (HOLD) mediante TEC2 utilizando interrupciones, 
 * manteniendo el control de LEDs en tiempo real.
 * 
 * @section changelog Changelog
 *
 * |    Date    | Description                                    |
 * |:----------:|:-----------------------------------------------|
 * | 10/09/2026 | Document creation                              |
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
 * @brief Función de callback/ISR invocada por la interrupción de TEC1 (SWITCH_1).
 *
 * Invierte el estado de la variable global 'activo' para pausar o reanudar la medición.
 *
 * @param[in] pvParameter Puntero genérico a parámetros de la interrupción (no utilizado).
 */
void Tecla1(void *pvParameter){
    activo = !activo;
}

/**
 * @brief Función de callback/ISR invocada por la interrupción de TEC2 (SWITCH_2).
 *
 * Invierte el estado de la variable global 'hold' para congelar o descongelar la pantalla LCD.
 *
 * @param[in] pvParameter Puntero genérico a parámetros de la interrupción (no utilizado).
 */
void Tecla2(void *pvParameter){
    hold = !hold;
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
 * Inicializa los pulsadores, configura las interrupciones asociadas a TEC1 y TEC2,
 * inicializa el sensor HC-SR04 e instancia las tareas concurrentes de medición y visualización.
 */
void app_main(void){
    SwitchesInit();
    
    /* Configuración de interrupciones para las teclas TEC1 y TEC2 */
    SwitchActivInt(SWITCH_1, &Tecla1, NULL);
    SwitchActivInt(SWITCH_2, &Tecla2, NULL);

    HcSr04Init(GPIO_3, GPIO_2);
    
    /* Se crean únicamente las tareas de medición y visualización */
    xTaskCreate(&MedirTask, "Medir", 512, NULL, 5, &medir_task_handle);
    xTaskCreate(&MostrarTask, "Mostrar", 512, NULL, 5, &mostrar_task_handle);
}