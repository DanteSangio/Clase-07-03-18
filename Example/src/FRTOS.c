/*
===============================================================================
 Name        : FRTOS.c
 Author      : $(author)
 Version     :
 Copyright   : $(copyright)
 Description : main definition
===============================================================================
*/

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Includes
#include "chip.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"
#include <cr_section_macros.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Defines
#define TICKRATE_HZ1 (4*2)	// 4: cte de frec - 2 ticks por segundo
#define TICKRATE_HZ2 (4*1)	// 4: cte de frec - 1 tick por segundo
#define PORT(x) 	((uint8_t) x)
#define PIN(x)		((uint8_t) x)
#define OUTPUT		((uint8_t) 1)
#define INPUT		((uint8_t) 0)

//PULSUP		PORT (2),PORT (9) puerto,pin para incrementar PWM SW3 baseboard
//PULSDOWN		PORT(1),PIN(4) puerto,pin para decrementar PWM SW4 baseboard


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Variables Globales
SemaphoreHandle_t Semaforo_1;
QueueHandle_t Cola_1;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/* uC_StartUp:
 * Inicializa los pines
*/
void uC_StartUp (void)
{
	Chip_GPIO_Init (LPC_GPIO);
	Chip_GPIO_SetDir (LPC_GPIO , PORT(0) , PIN(22) , OUTPUT);
	Chip_IOCON_PinMux (LPC_IOCON , PORT(0) , PIN(22), IOCON_MODE_INACT , IOCON_FUNC0);
	Chip_GPIO_SetDir (LPC_GPIO ,PORT (2),PORT (9) , INPUT);
	Chip_IOCON_PinMux (LPC_IOCON , PORT (2),PORT (9), IOCON_MODE_PULLDOWN , IOCON_FUNC0);
	Chip_GPIO_SetDir (LPC_GPIO , PORT(1),PIN(4), INPUT);
	Chip_IOCON_PinMux (LPC_IOCON , PORT(1),PIN(4), IOCON_MODE_PULLDOWN , IOCON_FUNC0);

}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/* vTaskInicTimer:
 * Tarea que se encarga de inicializar el TIMER0 y luego se autoelimina
*/
static void vTaskInicTimer(void *pvParameters)
{
	while (1)
	{
		uint32_t timerFreq;

		/* Enable timer 1 clock */
		Chip_TIMER_Init(LPC_TIMER0);	//Enciende el modulo
		/* Timer rate is system clock rate */
		timerFreq = Chip_Clock_GetSystemClockRate();					//Obtiene la frecuencia a la que esta corriendo el uC
		/* Timer setup for match and interrupt at TICKRATE_HZ */
		Chip_TIMER_Reset(LPC_TIMER0);									//Borra la cuenta

		//MATCH 0: NO RESETEA LA CUENTA
		Chip_TIMER_MatchEnableInt(LPC_TIMER0, 0);						//Habilita interrupcion del match 0 timer 0
		Chip_TIMER_SetMatch(LPC_TIMER0, 0, (timerFreq / TICKRATE_HZ1));	//Le asigna un valor al match - seteo la frec a la que quiero que el timer me interrumpa (Ej 500ms)
		Chip_TIMER_ResetOnMatchDisable(LPC_TIMER0, 0);					//Cada vez que llega al match NO resetea la cuenta

		//MATCH 1: SI RESETEA LA CUENTA
		Chip_TIMER_MatchEnableInt(LPC_TIMER0, 1);						//Habilita interrupcion del match 1 timer 0
		Chip_TIMER_SetMatch(LPC_TIMER0, 1, (timerFreq / TICKRATE_HZ2));	//Le asigna un valor al match - seteo la frec a la que quiero que el timer me interrumpa (Ej 1s)
		Chip_TIMER_ResetOnMatchEnable(LPC_TIMER0, 1);					//Cada vez que llega al match resetea la cuenta

		Chip_TIMER_Enable(LPC_TIMER0);									//Comienza a contar
		/* Enable timer interrupt */ 		//El NVIC asigna prioridades de las interrupciones (prioridad de 0 a inf)
		NVIC_ClearPendingIRQ(TIMER0_IRQn);
		NVIC_EnableIRQ(TIMER0_IRQn);		//Enciende la interrupcion que acabamos de configurar

		vTaskDelete(NULL);	//Borra la tarea, no necesitaria el while(1)
	}
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/* xTaskPWM
 * Tarea que se encarga de togglear el led
*/
static void xTaskPWM(void *pvParameters)
{
	while (1)
	{
		xSemaphoreTake(Semaforo_1, portMAX_DELAY);
		Chip_GPIO_SetPinToggle(LPC_GPIO,PORT(0),PIN(22));
	}
	vTaskDelete(NULL);	//Borra la tarea si sale del while 1
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/* xTaskMatch0:
* Tarea que se encarga de controlar el % de tiempo encendido o apagada la salida
*/
static void xTaskMatch0(void *pvParameters)
{
	uint8_t Ticks_HZ_Match0 = 8;//el PWM inicializa al 50%
	uint8_t Receive=0;
	uint32_t timerFreq;

	while (1)
	{
		xQueueReceive(Cola_1,&Receive,portMAX_DELAY);	//Para recibir de la Cola_1

		if(Receive==1 && Ticks_HZ_Match0 > 3)			//Si recibo 1 -> tengo que incrementar el duty
		{
			Ticks_HZ_Match0--;							//Incrementa el duty, va a dar cualquier porcentaje
		}
		if(Receive==2 && Ticks_HZ_Match0 < 12 )			//Si recibo 2 -> tengo que decrementar el duty
		{
			Ticks_HZ_Match0++;							//Decrementa el duty, va a dar cualquier porcentaje

		}

		if(Receive)
		{
			timerFreq = Chip_Clock_GetSystemClockRate();	//Obtiene la frecuencia a la que esta corriendo el uC
			Chip_TIMER_SetMatch(LPC_TIMER0, 0, (timerFreq /Ticks_HZ_Match0 ));	//Le cambia el valor al match
			Receive=0;										//Reestablece la variable
		}
	}
	vTaskDelete(NULL);	//Borra la tarea si sale del while 1
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/* xTaskPulsadores:
 * TAREA QUE LEE PULSADOR Y ESCRIBE EL VALOR DEL MATCH 0 EN COLA
 * */
static void xTaskPulsadores(void *pvParameters)
{
	uint8_t envio=0; // envio 1 si hay que subir pwm, 2 si hay que bajar pwm
	while (1)
	{
		if(Chip_GPIO_GetPinState(LPC_GPIO, PORT(2),PORT(9)))	//veo si presionan el pulsador up
		{
			envio=1;											//envio 1 -> subir PWM
			xQueueSendToBack(Cola_1, &envio, portMAX_DELAY);	//Para enviar a la Cola_1
			vTaskDelay(1000/portTICK_RATE_MS);					//Delay 1 seg
		}


		if(Chip_GPIO_GetPinState(LPC_GPIO, PORT(1),PIN(4)))		//veo si presionan el pulsador up
		{
			envio=2;											//envio 2 -> bajar PWM
			xQueueSendToBack(Cola_1, &envio, portMAX_DELAY);	//Para enviar a la Cola_1
			vTaskDelay(1000/portTICK_RATE_MS);					//Delay 1 seg
		}
	}
	vTaskDelete(NULL);	//Borra la tarea si sale del while 1
}



///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/* TIMER0_IRQHandler:
 * Controlador del TIMER0
*/
void TIMER0_IRQHandler(void)
{
	BaseType_t Testigo=pdFALSE;

	if (Chip_TIMER_MatchPending(LPC_TIMER0, 0))
	{
		Chip_TIMER_ClearMatch(LPC_TIMER0, 0);				//Resetea match

		xSemaphoreGiveFromISR(Semaforo_1, &Testigo);		//Devuelve si una de las tareas bloqueadas tiene mayor prioridad que la actual

		portYIELD_FROM_ISR(Testigo);						//Si testigo es TRUE -> ejecuta el scheduler
	}

	if (Chip_TIMER_MatchPending(LPC_TIMER0, 1))
	{
		Chip_TIMER_ClearMatch(LPC_TIMER0, 1);				//Resetea match

		xSemaphoreGiveFromISR(Semaforo_1, &Testigo);		//Devuelve si una de las tareas bloqueadas tiene mayor prioridad que la actual

		portYIELD_FROM_ISR(Testigo);						//Si testigo es TRUE -> ejecuta el scheduler
	}
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/*
 * main:
 * Programa principal
*/
int main(void)
{
	uC_StartUp (); //Config

	SystemCoreClockUpdate();

	vSemaphoreCreateBinary(Semaforo_1);

	xSemaphoreTake(Semaforo_1 , portMAX_DELAY );

	Cola_1 = xQueueCreate(4, sizeof(uint8_t));	//Creación de una cola de tamaño 4 y tipo uint8

	/*
	 * TAREA CON MAYOR PRIORIDAD (+2UL) QUE INICIALIZA EL TIMER Y LUEGO SE AUTOELIMINA
	 * */
	xTaskCreate(vTaskInicTimer, (char *) "vTaskInicTimer",
				configMINIMAL_STACK_SIZE, NULL, (tskIDLE_PRIORITY + 3UL),
				(xTaskHandle *) NULL);

	/*
	 * TAREA QUE LEE LA COLA Y CARGA EL VALOR DEL MATCH 0
	 * */
	xTaskCreate(xTaskMatch0, (char *) "xTaskMatch0",
				configMINIMAL_STACK_SIZE, NULL, (tskIDLE_PRIORITY + 2UL),
				(xTaskHandle *) NULL);

	/*
	 * TAREA QUE EJECUTA EL PWM
	 * */
	xTaskCreate(xTaskPWM, (char *) "xTaskPWM",
				configMINIMAL_STACK_SIZE, NULL, (tskIDLE_PRIORITY + 1UL),
				(xTaskHandle *) NULL);

	/*
	 * TAREA QUE LEE PULSADOR Y ESCRIBE EL VALOR DEL MATCH 0 EN COLA
	 * */

	xTaskCreate(xTaskPulsadores, (char *) "xTaskPulsadores",
				configMINIMAL_STACK_SIZE, NULL, (tskIDLE_PRIORITY + 1UL),
				(xTaskHandle *) NULL);

	/* Start the scheduler */
	vTaskStartScheduler();

	/* Nunca debería arribar aquí */

    return 0;
}

