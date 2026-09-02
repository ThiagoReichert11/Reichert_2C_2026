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
/*==================[macros and definitions]=================================*/

/*==================[internal data definition]===============================*/

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
/*==================[external functions definition]==========================*/
void app_main(void)
{
// Variables
    uint32_t data = 74925;
    uint8_t digits = 5;
    uint8_t bcd_number[5];

    // Mostrar los datos originales
    printf("Numero original: %lu\n", (unsigned long)data);
    printf("Cantidad de digitos: %u\n", digits);

    // Convertir a BCD
    convertToBcdArray(data, digits, bcd_number);

    // Mostrar el resultado
    printf("Numero en BCD: ");

	for (int i = 0; i < digits; i++)
    {
        printf("%u ", bcd_number[i]);
    }

    printf("\n");

}
/*==================[end of file]============================================*/

