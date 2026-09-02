/*! @mainpage Template
 *
 * @section genDesc General Description
 *
 * This section describes how the program works.
 *
 * <a href="https://drive.google.com/...">Operation Example</a>
 *
 * @section hardConn Hardware Connection
 *
 * |    Peripheral  |   ESP32   	|
 * |:--------------:|:--------------|
 * | 	PIN_X	 	| 	GPIO_X		|
 *
 *
 * @section changelog Changelog
 *
 * |   Date	    | Description                                    |
 * |:----------:|:-----------------------------------------------|
 * | 12/09/2023 | Document creation		                         |
 *
 * @author Albano Peñalva (albano.penalva@uner.edu.ar)
 *
 */

/*==================[inclusions]=============================================*/
#include <stdio.h>
#include <stdint.h>
#include "gpio_mcu.h"
/*==================[macros and definitions]=================================*/

/*==================[internal data definition]===============================*/
typedef struct
{
    gpio_t pin;
    io_t dir;
} gpioConf_t;

/*==================[internal functions declaration]=========================*/
void writeBcdToGpio(uint8_t bcd_digit, gpioConf_t *gpio_array)
{

    for (uint8_t i = 0; i < 4; i++)
    {
        GPIOState(gpio_array[i].pin, false);
        // También podías usar: GPIOState(gpio_array[i].pin, false);
    }

    for (uint8_t i = 0; i < 4; i++)
    {
        // Se extrae el bit 'i' del dígito BCD usando una máscara
        bool bit_state = (bcd_digit & (1 << i)) != 0;
        
        // Se utiliza la función provista en gpio_mcu.h para cambiar el estado
        GPIOState(gpio_array[i].pin, bit_state);
    }
}
/*==================[external functions definition]==========================*/
void app_main(void){

    // 1. Se define el vector mapeando los bits según el enunciado
    gpioConf_t bcd_pins[4] = {
        {GPIO_20, GPIO_OUTPUT}, // b0 -> GPIO_20
        {GPIO_21, GPIO_OUTPUT}, // b1 -> GPIO_21
        {GPIO_22, GPIO_OUTPUT}, // b2 -> GPIO_22
        {GPIO_23, GPIO_OUTPUT}  // b3 -> GPIO_23
    };

    // 2. Se inicializan los GPIO a utilizar previo a su uso
    for (uint8_t i = 0; i < 4; i++)
    {
        GPIOInit(bcd_pins[i].pin, bcd_pins[i].dir);
    }
	writeBcdToGpio(9, bcd_pins);
}
/*==================[end of file]============================================*/