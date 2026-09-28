/**
 * @file proyecto2_act3.c
 * @brief Sistema de medición de distancia con HC-SR04, LCD, LEDs e interfaz UART con función HOLD unificada.
 *
 * @mainpage Control de Distancia con Función HOLD para LCD y UART
 *
 * \section genDesc General Description
 *
 * Este programa mide la distancia utilizando un sensor ultrasónico HC-SR04, muestra el valor en un 
 * display LCD y enciende una combinación de LEDs según el rango. Permite iniciar/detener la medición 
 * mediante TEC1 o el comando 'O' por UART, congelar la lectura en el LCD y la transmisión serie (HOLD) 
 * mediante TEC2 o el comando 'H' por UART, y alternar la unidad entre centímetros y pulgadas mediante 'I'.
 * 
 * @section changelog Changelog
 *
 * |    Date    | Description                                    |
 * |:----------:|:-----------------------------------------------|
 * | 17/09/2026 | Integración de puerto serie con driver uart_mcu|
 * | 17/09/2026 | Selección de unidad (cm/pulgadas) vía UART     |
 * | 24/09/2026 | Función para mantener valor máximo              |
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
#include "uart_mcu.h"

/*==================[macros and definitions]=================================*/
/**
 * @def CONFIG_PERIOD_MEDIR
 * @brief Período de muestreo para el sensor ultrasónico HC-SR04 y transmisión UART en milisegundos.
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
 * @brief Estado de retención de lectura en pantalla y transmisión serie (true: congela valores, false: actualiza).
 */
bool hold = false;

/**
 * @var pulgadas
 * @brief Unidad de trabajo seleccionada (false: centímetros [cm], true: pulgadas [inch]).
 */
bool pulgadas = false;

/**
 * @var modo_max
 * @brief Modo de visualización de valor máximo (true: muestra distancia máxima registrada, false: lectura normal/hold).
 */
bool modo_max = false;

/**
 * @var distancia
 * @brief Almacena la distancia medida en tiempo real por el sensor HC-SR04.
 */
uint16_t distancia = 0;

/**
 * @var distancia_retenida
 * @brief Almacena el valor congelado de la distancia para mostrar en el LCD y transmitir por UART.
 */
uint16_t distancia_retenida = 0;

/**
 * @var distancia_maxima
 * @brief Almacena el valor máximo registrado desde que se inició la medición.
 */
uint16_t distancia_maxima = 0;

/**
 * @var medir_task_handle
 * @brief Manejador de la tarea encargada del disparo, lectura y transmisión UART del sensor.
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
 * - Menor a 10: Todos los LEDs apagados.
 * - De 10 a 19: Enciende LED_1.
 * - De 20 a 29: Enciende LED_1 y LED_2.
 * - Mayor o igual a 30: Enciende LED_1, LED_2 y LED_3.
 *
 * @param[in] dist Valor de la distancia medida.
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
 * Invierte el estado de la variable global 'hold' para congelar o descongelar el valor del LCD y UART.
 *
 * @param[in] pvParameter Puntero genérico a parámetros de la interrupción (no utilizado).
 */
void Tecla2(void *pvParameter){
    hold = !hold;
}

/**
 * @brief Función de callback invocada al recibir un carácter por la interfaz UART.
 *
 * - 'O' / 'o': Inicia / pausa la medición.
 * - 'H' / 'h': Activa / desactiva la congelación de pantalla y UART (HOLD).
 * - 'I' / 'i': Alterna la unidad entre cm y pulgadas (reinicia la distancia máxima).
 * - 'M' / 'm': Alterna la visualización del valor máximo registrado desde el encendido.
 *
 * @param[in] param Puntero genérico a parámetros del callback (no utilizado).
 */
void FuncUart(void *param){
    uint8_t tecla;
    UartReadByte(UART_PC, &tecla);
    if(tecla == 'O' || tecla == 'o'){
        activo = !activo;
    }
    if(tecla == 'H' || tecla == 'h'){
        hold = !hold;
    }
    if(tecla == 'I' || tecla == 'i'){
        pulgadas = !pulgadas;
        distancia_maxima = 0; /* Reinicia el máximo al cambiar la escala/unidad */
    }
    if(tecla == 'M' || tecla == 'm'){
        modo_max = !modo_max;
    }
}

/**
 * @var my_uart
 * @brief Estructura de configuración para el puerto serie UART.
 */
serial_config_t my_uart = {
    .port      = UART_PC,   /**< Puerto UART configurado para comunicación con la PC. */
    .baud_rate = 115200,    /**< Velocidad de transmisión en baudios. */
    .func_p    = FuncUart,  /**< Callback invocado al recibir datos por UART. */
    .param_p   = NULL       /**< Parámetro genérico (no utilizado). */
};

/**
 * @brief Tarea de FreeRTOS encargada de medir y transmitir datos por el puerto serie UART.
 *
 * Mide en tiempo real, actualiza la `distancia_maxima` alcanzada y transmite por UART 
 * el valor máximo si `modo_max` está activo, o el valor retenido/actual si está desactivado.
 *
 * @param[in] pvParameter Puntero genérico a parámetros de FreeRTOS (no utilizado).
 */
static void MedirTask(void *pvParameter){
    uint16_t valor_a_enviar;

    while(true){
        if(activo){
            /* Medición en tiempo real según la unidad activa */
            if(pulgadas){
                distancia = HcSr04ReadDistanceInInches();
            } else {
                distancia = HcSr04ReadDistanceInCentimeters();
            }
            
            /* Actualización de la distancia máxima registrada */
            if(distancia > distancia_maxima){
                distancia_maxima = distancia;
            }

            /* Si no está en HOLD, actualiza el valor de retención */
            if(!hold){
                distancia_retenida = distancia;
            }
            
            /* Selección del valor a enviar con condición tradicional */
            if(modo_max){
                valor_a_enviar = distancia_maxima;
            } else {
                valor_a_enviar = distancia_retenida;
            }
            
            /* Transmisión por UART */
            UartSendString(UART_PC, (char *)UartItoa(valor_a_enviar, 10));
            
            if(pulgadas){
                UartSendString(UART_PC, " in\r\n");
            } else {
                UartSendString(UART_PC, " cm\r\n");
            }
        }
        vTaskDelay(CONFIG_PERIOD_MEDIR / portTICK_PERIOD_MS);
    }
}

/**
 * @brief Tarea de FreeRTOS encargada del control del display LCD y los LEDs.
 *
 * Actualiza los LEDs en tiempo real según la medición física y muestra en el LCD el 
 * valor máximo o el valor retenido según corresponda.
 *
 * @param[in] pvParameter Puntero genérico a parámetros de FreeRTOS (no utilizado).
 */
static void MostrarTask(void *pvParameter){
    uint16_t valor_a_mostrar;

    LedsInit();
    LcdItsE0803Init();
    
    while(true){
        if(activo){
            /* Los LEDs responden a la medición real instantánea */
            ControlLeds(distancia);

            /* Selección del valor a mostrar con condición tradicional */
            if(modo_max){
                valor_a_mostrar = distancia_maxima;
            } else {
                valor_a_mostrar = distancia_retenida;
            }

            LcdItsE0803Write(valor_a_mostrar);
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
 */
void app_main(void){
    SwitchesInit();
    
    SwitchActivInt(SWITCH_1, &Tecla1, NULL);
    SwitchActivInt(SWITCH_2, &Tecla2, NULL);

    UartInit(&my_uart);

    HcSr04Init(GPIO_3, GPIO_2);
    
    xTaskCreate(&MedirTask, "Medir", 512, NULL, 5, &medir_task_handle);
    xTaskCreate(&MostrarTask, "Mostrar", 512, NULL, 5, &mostrar_task_handle);
}