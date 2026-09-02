/** 
 * @file main.c
 * @brief Controlador de display multiplexado de 7 segmentos con salida BCD para ESP-EDU.
 *
 * Este programa permite descomponer un entero de 32 bits en sus dígitos individuales 
 * BCD y mostrarlos de forma multiplexada en hasta 3 dígitos de un display de 7 segmentos.
 * Incluye además validación de errores para prevenir desbordamientos o valores inválidos.
 *
 * @author Albano Peñalva (albano.penalva@uner.edu.ar)
 *
 * @section hardConn Hardware Connection
 *
 * |    Peripheral    |   ESP32-C6    |
 * |:----------------:|:--------------|
 * |   BCD Output b0  |    GPIO_20    |
 * |   BCD Output b1  |    GPIO_21    |
 * |   BCD Output b2  |    GPIO_22    |
 * |   BCD Output b3  |    GPIO_23    |
 * |   Selector LCD 1 |    GPIO_19    |
 * |   Selector LCD 2 |    GPIO_18    |
 * |   Selector LCD 3 |    GPIO_9     |
 *
 * @section changelog Changelog
 *
 * |    Date    | Description                                    |
 * |:----------:|:-----------------------------------------------|
 * | 12/09/2023 | Document creation                              |
 *
 */

/*==================[inclusions]=============================================*/
#include <stdio.h>
#include "gpio_mcu.h"

/*==================[macros and definitions]=================================*/

/*==================[internal data definition]===============================*/
/**
 * @brief Estructura de configuración de un pin GPIO.
 * 
 * Agrupa la identificación del pin y su dirección de trabajo (Entrada o Salida).
 */
typedef struct
{
    gpio_t pin; /**< Número de pin del microcontrolador (gpio_t) */
    io_t dir;   /**< Dirección del pin (GPIO_INPUT o GPIO_OUTPUT) */
} gpioConf_t;

/*==================[internal functions declaration]=========================*/

/**
 * @brief Convierte un número entero de 32 bits a un arreglo de dígitos BCD.
 * 
 * Descompone el número pasado por parámetro realizando operaciones módulo 10 
 * y división entera, llenando el arreglo desde el dígito menos significativo al más significativo.
 * 
 * @param[in] data Número entero de 32 bits a descomponer.
 * @param[in] digits Cantidad de dígitos a extraer.
 * @param[out] bcd_number Puntero al arreglo donde se almacenarán los dígitos BCD extraídos.
 * 
 * @return int8_t Retorna 0 al completar la conversión con éxito.
 */
int8_t convertToBcdArray(uint32_t data, uint8_t digits, uint8_t *bcd_number)
{
    for (int i = digits - 1; i >= 0; i--)
    {
        bcd_number[i] = data % 10;
        data = data / 10;
    }

    return 0;
}

/**
 * @brief Escribe un dígito BCD (0-9) en el bus de 4 bits formado por los GPIO de entrada.
 * 
 * Configura secuencialmente el estado lógico de los 4 pines definidos en el vector
 * para representar el valor binario del dígito BCD ingresado. Limpia las salidas previo al cambio.
 * 
 * @param[in] bcd_digit Dígito BCD (0 a 9) que se desea escribir.
 * @param[in] gpio_array Puntero a la estructura con la configuración de los 4 pines BCD (b0 a b3).
 */
void writeBcdToGpio(uint8_t bcd_digit, gpioConf_t *gpio_array)
{
    for (uint8_t i = 0; i < 4; i++)
    {
        GPIOState(gpio_array[i].pin, false);
    }

    for (uint8_t i = 0; i < 4; i++)
    {
        bool bit_state = (bcd_digit & (1 << i)) != 0;
        GPIOState(gpio_array[i].pin, bit_state);
    }
}

/**
 * @brief Controla el refresco multiplexado del display de 7 segmentos.
 * 
 * Evalúa las condiciones de error (datos fuera de rango o cantidad excesiva de dígitos). 
 * Si no hay errores, descompone el número y lo proyecta multiplexando las salidas de selección.
 * 
 * @param[in] data Dato entero de 32 bits que se desea visualizar.
 * @param[in] digits Cantidad de dígitos disponibles/a utilizar en el display (Máximo 3).
 * @param[in] bcd_pins Vector con la configuración de los 4 pines del bus de datos BCD.
 * @param[in] lcd_digit_pins Vector con la configuración de los pines de selección de cada dígito.
 */
void displayWrite(uint32_t data, uint8_t digits, gpioConf_t *bcd_pins, gpioConf_t *lcd_digit_pins)
{
    bool is_float = (data != (uint32_t)data);
    bool is_out_of_range = (data >= 1000 || digits > 3);

    // Manejo de condición de error
    if (is_float || is_out_of_range)
    {
        writeBcdToGpio(0, bcd_pins);

        for (uint8_t i = 0; i < digits; i++)
        {
            GPIOToggle(lcd_digit_pins[i].pin);
        }
        
        return; 
    }

    uint8_t bcd_array[digits];

    // Descomposición del número a BCD
    convertToBcdArray(data, digits, bcd_array);

    // Apagado de los dígitos del display para evitar solapamientos
    for (uint8_t j = 0; j < 3; j++) 
    {
        GPIOOff(lcd_digit_pins[j].pin);
    }

    // Multiplexado del dato por pantalla
    for (uint8_t i = 0; i < digits; i++)
    {
        writeBcdToGpio(bcd_array[i], bcd_pins);

        GPIOOn(lcd_digit_pins[i].pin);

        GPIOOff(lcd_digit_pins[i].pin);
    }
}

/*==================[external functions definition]==========================*/

/**
 * @brief Punto de entrada principal de la aplicación.
 * 
 * Configura los pines de entrada/salida para el bus BCD y los selectores del display,
 * los inicializa llamando a los drivers del hardware y ejecuta la orden de refresco.
 */
void app_main(void)
{
    // Dato a mostrar (3 dígitos)
    uint32_t data = 2;
    uint8_t digits = 3;

    /** Vector de configuración del bus BCD (Mapeo: b0 -> GPIO_20 a b3 -> GPIO_23) */
    gpioConf_t bcd_pins[4] = {
        {GPIO_20, GPIO_OUTPUT}, // b0
        {GPIO_21, GPIO_OUTPUT}, // b1
        {GPIO_22, GPIO_OUTPUT}, // b2
        {GPIO_23, GPIO_OUTPUT}  // b3
    };

    /** Vector de configuración del selector de dígitos (Mapeo: Dígito 1 a 3) */
    gpioConf_t lcd_digits[3] = {
        {GPIO_19, GPIO_OUTPUT}, // Dígito 1
        {GPIO_18, GPIO_OUTPUT}, // Dígito 2
        {GPIO_9,  GPIO_OUTPUT}  // Dígito 3
    };

    // Inicialización del hardware BCD
    for (uint8_t i = 0; i < 4; i++) {
        GPIOInit(bcd_pins[i].pin, bcd_pins[i].dir);
    }

    // Inicialización del hardware de control de dígitos
    for (uint8_t i = 0; i < digits; i++) {
        GPIOInit(lcd_digits[i].pin, lcd_digits[i].dir);
    }

    // Mostrar el número en la pantalla
    displayWrite(data, digits, bcd_pins, lcd_digits);
}

/*==================[end of file]============================================*/