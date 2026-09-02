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
#include "gpio_mcu.h"
/*==================[macros and definitions]=================================*/
#define ERROR_BLINK_DELAY 100000
/*==================[internal data definition]===============================*/
typedef struct
{
    gpio_t pin;
    io_t dir;
} gpioConf_t;
/*==================[internal functions declaration]=========================*/
int8_t convertToBcdArray(uint32_t data, uint8_t digits, uint8_t *bcd_number)
{
    for (int i = digits - 1; i >= 0; i--)
    {
        bcd_number[i] = data % 10;
        data = data / 10;
    }

    return 0;
}

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

void displayWrite(uint32_t data, uint8_t digits, gpioConf_t *bcd_pins, gpioConf_t *lcd_digit_pins)
{

    
    bool is_float = (data != (uint32_t)data);
    bool is_out_of_range = (data >= 1000 || digits > 3);

    if (is_float || is_out_of_range)
    {
        // Limpiar el bus BCD
        writeBcdToGpio(0, bcd_pins);

        // Hacer titilar (Toggle) los dígitos para indicar estado de error
        for (uint8_t i = 0; i < digits; i++)
        {
            GPIOToggle(lcd_digit_pins[i].pin);
        }
        
        return; // Finalizar la función sin intentar convertir
    }

   uint8_t bcd_array[digits];

    // 1. Descomponer el número
    convertToBcdArray(data, digits, bcd_array);

    // 2. SOLUCIÓN: Apagar explícitamente TODOS los dígitos del display primero
    // (Asumiendo que lcd_digit_pins tiene la cantidad total de pantallas del módulo)
    for (uint8_t j = 0; j < 3; j++) 
    {
        GPIOOff(lcd_digit_pins[j].pin);
    }

    // 3. Encender secuencialmente solo los dígitos que realmente se usan
    for (uint8_t i = 0; i < digits; i++)
    {
        // Enviar el valor BCD al bus
        writeBcdToGpio(bcd_array[i], bcd_pins);

        // Encender únicamente el dígito actual
        GPIOOn(lcd_digit_pins[i].pin);

        // Dejar un tiempo visible o apagar antes del siguiente ciclo
        GPIOOff(lcd_digit_pins[i].pin);
    }
}
/*==================[external functions definition]==========================*/
void app_main(void){
	// Dato de ejemplo a mostrar (3 dígitos)
    uint32_t data = 2;
    uint8_t digits = 3;


    // Vector BCD del Punto 5 (mapeo b0 -> GPIO_20 hasta b3 -> GPIO_23)
    gpioConf_t bcd_pins[4] = {
        {GPIO_20, GPIO_OUTPUT}, // b0
        {GPIO_21, GPIO_OUTPUT}, // b1
        {GPIO_22, GPIO_OUTPUT}, // b2
        {GPIO_23, GPIO_OUTPUT}  // b3
    };

    // Vector LCD del Punto 6 (mapeo según consigna)
    gpioConf_t lcd_digits[3] = {
        {GPIO_19, GPIO_OUTPUT}, // Dígito 1
        {GPIO_18, GPIO_OUTPUT}, // Dígito 2
        {GPIO_9,  GPIO_OUTPUT}  // Dígito 3
    };

    // Inicialización del hardware (Punto 5)
    for (uint8_t i = 0; i < 4; i++) {
        GPIOInit(bcd_pins[i].pin, bcd_pins[i].dir);
    }

    // Inicialización del hardware (Punto 6)
    for (uint8_t i = 0; i < digits; i++) {
        GPIOInit(lcd_digits[i].pin, lcd_digits[i].dir);
    }

    // Mostrar el número en la pantalla
    displayWrite(data, digits, bcd_pins, lcd_digits);
}
/*==================[end of file]============================================*/