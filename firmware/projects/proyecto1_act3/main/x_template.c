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
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"
#include "switch.h"
/*==================[macros and definitions]=================================*/

typedef enum leds_mode
{
	OFF,
	ON,
	TOGGLE
} led_mode_t;
led_mode_t mode;

/*==================[internal data definition]===============================*/
typedef struct leds
{
    uint8_t mode;       //ON, OFF, TOGGLE
	uint8_t n_led;      /* indica el número de led a controlar*/
	uint8_t n_ciclos;  /* indica la cantidad de ciclos de encendido/apagado*/
	uint16_t periodo;   /* indica el tiempo de cada ciclo */
} my_leds; 

my_leds Placa;
my_leds *ptr_Placa = &Placa;
/*==================[internal functions declaration]=========================*/


void control_modo(my_leds *ptr_Placa)
{
	switch(ptr_Placa->mode){

		case OFF:

			if (ptr_Placa->n_led == 1)
			{
				LedOff(LED_1);
				LedOn(LED_2);
				LedOn(LED_3);	
			}
			else if (ptr_Placa->n_led == 2)
			{
				LedOff(LED_2);
				LedOn(LED_1);
				LedOn(LED_3);	
			}
			else if (ptr_Placa->n_led == 3)
			{		
				LedOff(LED_3);
				LedOn(LED_1);
				LedOn(LED_2);	
			}
			else
				LedsOffAll();
			
		break;
		case ON:

			if (ptr_Placa->n_led == 1)
				LedOn(LED_1);
			else if (ptr_Placa->n_led == 2)
				LedOn(LED_2);
			else if (ptr_Placa->n_led == 3)
				LedOn(LED_3);
			else
				LedsOffAll();
		break;
		case TOGGLE:
			
			for (size_t i = 0; i < 2*ptr_Placa->n_ciclos; i++)
			{
				printf("Ciclo %d\n", i);
				if (ptr_Placa->n_led == 1)
					LedToggle(LED_1);
				else if (ptr_Placa->n_led == 2)
					LedToggle(LED_2);
				else if (ptr_Placa->n_led == 3)
					LedToggle(LED_3);
				else
					LedsOffAll();
				vTaskDelay(ptr_Placa->periodo / portTICK_PERIOD_MS);
				
			}
			
		break;
		
	}
}

/*==================[external functions definition]==========================*/

void app_main(void)
{
	LedsInit();

	
	Placa.mode = TOGGLE; // Elige el modo de los leds
	Placa.n_led = 3; // Elige el número de led a controlar
	Placa.n_ciclos = 5; // Elige la cantidad de ciclos de encendido/apagado
	Placa.periodo = 500; // Elige el tiempo de cada ciclo en mil


	control_modo(ptr_Placa);
}
/*==================[end of file]============================================*/

