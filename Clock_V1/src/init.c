/*
 * init.c
 *
 *  Created on: Oct 21, 2019
 *      Author: User
 */

#include <stdbool.h>
#include "em_gpio.h"
#include "em_usart.h"
#include "em_emu.h"
#include "em_cmu.h"
#include "em_rtc.h"
#include "../inc/init.h"

#define RTC_FREQ    32768

void InitClk(){
	CMU_ClockSelectSet(cmuClock_HF, cmuSelect_HFRCO);
	CMU_ClockEnable(cmuClock_HFPER, true);
	CMU_ClockEnable(cmuClock_CORELE, true);
	// Enable LFXO as LFACLK in CMU. This will also start LFXO
	CMU_ClockSelectSet(cmuClock_LFA, cmuSelect_LFXO);
	// Set a clock divisor of 32 to reduce power conumption.
	CMU_ClockDivSet(cmuClock_RTC, cmuClkDiv_32);
	// Enable RTC clock
	CMU_ClockEnable(cmuClock_RTC, true);
}
void InitUart(){
	CMU_ClockEnable(cmuClock_UART0, true);
	CMU_ClockEnable(cmuClock_GPIO, true);

	USART_InitAsync_TypeDef initasync = USART_INITASYNC_DEFAULT;
	initasync.baudrate = 115200;
	initasync.databits = usartDatabits8;
	initasync.parity = usartNoParity;
	initasync.stopbits = usartStopbits1;
	initasync.oversampling = usartOVS16;
	USART_InitAsync(UART0, &initasync);

	GPIO_PinModeSet(gpioPortE, 0, gpioModePushPull, 1);
	GPIO_PinModeSet(gpioPortE, 1, gpioModeInput, 0);

	GPIO->P[5].DOUT |= (1 << 7); // VCOM port engedelyezese
	UART0->ROUTE = (UART0->ROUTE & ~_UART_ROUTE_LOCATION_MASK) | UART_ROUTE_LOCATION_LOC1;
	UART0->ROUTE |= UART_ROUTE_RXPEN | UART_ROUTE_TXPEN;

	USART_IntEnable(UART0, UART_IF_RXDATAV);

	NVIC_ClearPendingIRQ(UART0_RX_IRQn);
	NVIC_ClearPendingIRQ(UART0_TX_IRQn);
	NVIC_EnableIRQ(UART0_RX_IRQn);

}
void InitGpio(){
	// PB9 es PB10 inputkent kezelese - buttons
	GPIO_PinModeSet(gpioPortB, 9, gpioModeInput, 0);
	GPIO_PinModeSet(gpioPortB, 10, gpioModeInput, 0);

	// Felfuto es lefuto elre it engedelyezese mindket portra
	GPIO_IntConfig(gpioPortB, 9, true, true, true);
	GPIO_IntConfig(gpioPortB, 10, true, true, true);

	NVIC_ClearPendingIRQ(GPIO_EVEN_IRQn); // clear buttons it flags
	NVIC_EnableIRQ(GPIO_EVEN_IRQn);
	NVIC_ClearPendingIRQ(GPIO_ODD_IRQn);
	NVIC_EnableIRQ(GPIO_ODD_IRQn);
}
void InitRtc(){
	RTC_Init_TypeDef rtcInit = RTC_INIT_DEFAULT;
	rtcInit.enable   = false;  		// Do not start RTC after initialization is complete.
	rtcInit.debugRun = false;  		// Halt RTC when debugging.
	rtcInit.comp0Top = true;   		// Wrap around on COMP0 match.
	RTC_Init(&rtcInit);
 	RTC_CompareSet(0, ((RTC_FREQ / 32)) - 1); // Interupt in every secound
 	RTC_IntEnable(RTC_IEN_COMP0);
 	NVIC_ClearPendingIRQ(RTC_IRQn);
 	NVIC_EnableIRQ(RTC_IRQn);
	RTC_Enable(true);
}
