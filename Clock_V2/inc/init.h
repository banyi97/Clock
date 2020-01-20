#include <stdbool.h>

#ifndef INIT_H
#define INIT_H

// Inicializálási függvények
void cmuSetup(void);	// Beállítjuk a rendszer és a használt perifériák órajeleit
void rtcSetup(void);
void gpioSetup(void);
void timer0Setup(void);
void timer1Setup(void);
void uartSetup(void);

// megszakításai függvények
void GPIO_EVEN_IRQHandler(void);
void GPIO_ODD_IRQHandler(void);
void RTC_IRQHandler(void);
void TIMER0_IRQHandler(void);
void TIMER1_IRQHandler(void);
void UART0_RX_IRQHandler(void)

// UART parancshoz tartozó függvények
char parancs_kod(void);
bool ora_beallitas(void);
bool riasztasi_ido_beallitas(void);
bool ebresztes_beallitas(void);

// Egyéb fügvények
void szundi(void);
void Loop(void);

#endif
