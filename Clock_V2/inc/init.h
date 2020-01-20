#include <stdbool.h>

#ifndef INIT_H
#define INIT_H

// Inicializ�l�si f�ggv�nyek
void cmuSetup(void);	// Be�ll�tjuk a rendszer �s a haszn�lt perif�ri�k �rajeleit
void rtcSetup(void);
void gpioSetup(void);
void timer0Setup(void);
void timer1Setup(void);
void uartSetup(void);

// megszak�t�sai f�ggv�nyek
void GPIO_EVEN_IRQHandler(void);
void GPIO_ODD_IRQHandler(void);
void RTC_IRQHandler(void);
void TIMER0_IRQHandler(void);
void TIMER1_IRQHandler(void);
void UART0_RX_IRQHandler(void)

// UART parancshoz tartoz� f�ggv�nyek
char parancs_kod(void);
bool ora_beallitas(void);
bool riasztasi_ido_beallitas(void);
bool ebresztes_beallitas(void);

// Egy�b f�gv�nyek
void szundi(void);
void Loop(void);

#endif
