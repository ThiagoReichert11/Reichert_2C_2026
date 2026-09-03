/*! @mainpage Control de Distancia
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
 * | 12/09/2023 | Document creation                              |
 *
 * @author Albano Peñalva (albano.penalva@uner.edu.ar)
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
#define CONFIG_PERIOD_TECLAS 50
#define CONFIG_PERIOD_MEDIR 1000
#define CONFIG_PERIOD_MOSTRAR 100

/*==================[internal data definition]===============================*/
bool activo = false;
bool hold = false;
uint16_t distancia = 0;

TaskHandle_t teclas_task_handle = NULL;
TaskHandle_t medir_task_handle = NULL;
TaskHandle_t mostrar_task_handle = NULL;

/*==================[internal functions declaration]=========================*/
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

static void MedirTask(void *pvParameter){
    while(true){
        if(activo){
            /* Mide la distancia en tiempo real independientemente del estado de hold */
            distancia = HcSr04ReadDistanceInCentimeters();
        }
        vTaskDelay(CONFIG_PERIOD_MEDIR / portTICK_PERIOD_MS);
    }
}

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
void app_main(void){
    SwitchesInit();
    HcSr04Init(GPIO_3, GPIO_2);
    
    xTaskCreate(&TeclasTask, "Teclas", 2048, NULL, 5, &teclas_task_handle);
    xTaskCreate(&MedirTask, "Medir", 2048, NULL, 5, &medir_task_handle);
    xTaskCreate(&MostrarTask, "Mostrar", 2048, NULL, 5, &mostrar_task_handle);
}