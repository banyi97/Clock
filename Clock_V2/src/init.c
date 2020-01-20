#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "em_device.h"
#include "em_chip.h"
#include "em_cmu.h"
#include "em_gpio.h"
#include "em_rtc.h"
#include "em_timer.h"
#include "em_core.h"
#include "em_usart.h"
#include "segmentlcd.h"
#include "bsp.h"
#include "../inc/re.h"

// Az RTC �rajelfrekvenci�ja
#define RTC_FREQ    32768

// Szundi idej�nek a n�vel�s�nek m�rt�ke percben
#define SZUNDI_NOVELES 10

///////////////////////
// Glob�lis v�ltoz�k //
///////////////////////

// 3 id�pontot kell t�rolnunk:
volatile uint8_t ora = 0, perc = 0, masodperc = 0;	// val�s id�, mely m�sodpercenk�nt v�ltozik
uint8_t riasztas_ora = 0, riasztas_perc = 0;		// riaszt�s ideje
uint8_t szundi_ora, szundi_perc;					// szundi ideje

// RTC megszak�t�si esem�ny flag-je, ha megh�v�dik RTC IT akkor be�ll�t�dik
// a lekezel�s hely�n t�rl�dik
volatile bool rtc_megszakitas = false;	// rtc IT esem�ny jelz�se

// A k�t nyom�gomb �ll�pota, melyet a GPIO IT-k �ll�tanak, �s a riaszt�s sor�n haszn�lunk fel
volatile bool gomb0_lefut = false;
volatile bool gomb1_lefut = false;

// A riaszt�s �llapot�t jelz� flag, alap�telmezetten, indul�skor nincs riaszt�s, ez�rt false
// mivel t�bb f�ggv�ny is �ll�thatja, k�zt�k IT esem�ny is, ez�rt lett glob�lis-nak defini�lva
volatile bool riasztas = false;
// Erre a flag-re akkor van sz�ks�g, mikor egy IT kikapcsolja a riaszt�st, akkor
// bizonyos flagek null�z�s�t el kell v�gezni, de el�g egyszer. �gy � a riaszt�s
// kikapcsol�s�t jelzi
volatile bool riasztas_kikapcsolas = false;

// Buffer struk�ra UART-hoz
struct Buffer {
	unsigned char character[16];
	uint8_t length;
};

// UART IT v�ltoz�i
// Megszak�t�s esem�ny jelz�se a f�programnak
volatile bool uart_megszakitas = false;

// True, ha perencs �rkezett
volatile bool uart_parancs = false;

// True, ha �rv�nytelen parancs �rkezett
volatile bool uart_parancs_hiba = false;

// 1 byte fogad�sa UART-on kerszt�l
volatile unsigned char uart_byte;

// UART kommunik�ci� sor�n t�ltj�k fel az �rkez� adatokkal
struct Buffer uartBuff = {.length = 0, .character = ""};

// Ebbe t�ltj�k �t az �rkez� parancsot feldolgoz�sra
struct Buffer parancs_Buff = {.length = 0, .character = ""};

// Ez �r�dik ki UART-on, ha �rv�nytelen parancs �rkezik
const char hiba_uzenet[] = "Error \r\n";

// TIMER1-hez tartoz� v�ltoz�
volatile bool gecko_kikapcsolas = false;

// Ha true, riaszt�s enged�lyezve
bool riasztas_engedelyezes = false;

// Ha true, akkor egy riaszt�s kikapcsol�sa sor�n vissza�ll�tjuk a szunit a riaszt�si id�re
bool szundi_torles = true;

// �rajelek be�ll�t�sa
void cmuSetup(void) {
	CMU_ClockSelectSet(cmuClock_HF, cmuSelect_HFRCO);

	// be�ll�tjuk LFACLK forr�sanak a LFXO-t
	CMU_ClockSelectSet(cmuClock_LFA, cmuSelect_LFXO);

	// Alacsony fogyaszt�s� perif�ri�k sz�m�ra az �rajel enged�lyez�se
	CMU_ClockEnable(cmuClock_CORELE, true);

	// �rajeloszt�s be�ll�t�sa 32-vel
	CMU_ClockDivSet(cmuClock_RTC, cmuClkDiv_32);

	// �rajel enged�lyez�se az RTC perif�ri�n
	CMU_ClockEnable(cmuClock_RTC, true);

	// GPIO perif�ri�nak �rajel enged�lyez�se
	CMU_ClockEnable(cmuClock_GPIO, true);

	// id�z�t� �rajel�nek enged�lyez�se
	CMU_ClockEnable(cmuClock_TIMER0, true);

	// id�z�t� �rajel�nek enged�lyez�se
	CMU_ClockEnable(cmuClock_TIMER1, true);

	// UART perif�ri�nak �rajel enged�lyez�se
	CMU_ClockEnable(cmuClock_UART0, true);
}

// RTC perif�ria enged�lyez�se, IT be�ll�t�sa
void rtcSetup(void) {
	// Inicializ�l�si param�tereket t�rol� strukt�ra
	// Alap�rt�kekkel t�ltj�k fel
	RTC_Init_TypeDef rtcInit = RTC_INIT_DEFAULT;

	// RTC inicializ�l�sa
	rtcInit.enable		= false;	// akkor ind�tjuk a sz�ml�l�st ha v�ge az inicializ�l�snak
	rtcInit.debugRun	= false;	// Az RTC tartja az �rt�k�t Debug sor�n
	rtcInit.comp0Top	= true;		// sz�ml�l�s 0-r�l, ha el�rte comp0 �rt�ket
	RTC_Init(&rtcInit);

	// Megszak�t�s minden m�sodpercben
	RTC_CompareSet(0, (RTC_FREQ / 32)-1);

	// Megszak�t�s enged�lyez�se
	RTC_IntClear(RTC_IFC_COMP0);			// Perif�ri�n IT flag t�rl�se
	RTC_IntEnable(RTC_IEN_COMP0);			// Perif�ir�n IT enged�lyez�se
	NVIC_ClearPendingIRQ(RTC_IRQn);	// CORE-ban IT flag t�rl�se
	NVIC_EnableIRQ(RTC_IRQn);				// CORE-ban IT enged�lyez�se

	// Sz�ml�l�s ind�t�sa
	RTC_Enable(true);
}

// A GPIO perif�ria enged�lyez�se, majd az IT be�ll�t�sa
void gpioSetup(void) {

	// A PB9 �s PB10 l�bak bemenetre �ll�t�sa
	GPIO_PinModeSet(gpioPortB,  9, gpioModeInput, 0);
	GPIO_PinModeSet(gpioPortB, 10, gpioModeInput, 0);

	// Interruptok hozz�rendel�se a l�bakhoz, be�ll�t�sa �s tilt�sa a perif�ri�n
	// Megszak�t�si esem�ny fel �s lefut� �lre is megh�v�dik
	GPIO_IntConfig(gpioPortB,  9, true, true, false);
	GPIO_IntConfig(gpioPortB, 10, true, true, false);

	// A perif�ria IT flag-ek t�rl�se
	GPIO_IntClear(1 <<  9);
	GPIO_IntClear(1 << 10);

	// A core-ban a perif�ri�hoz tartoz� flagek t�rl�se, majd IT enged�lyez�se
	NVIC_ClearPendingIRQ(GPIO_EVEN_IRQn);
	NVIC_EnableIRQ(GPIO_EVEN_IRQn);

	NVIC_ClearPendingIRQ(GPIO_ODD_IRQn);
	NVIC_EnableIRQ(GPIO_ODD_IRQn);
}

// 0-�s id�z�t� enged�lyez�se, IT be�ll�t�sa
void timer0Setup(void){
	// param�terstrukt�ra l�trehoz�sa
	TIMER_Init_TypeDef TIMER0_init = TIMER_INIT_DEFAULT;

	// a prescaler-t �t�ll�tjuk
	TIMER0_init.prescale = timerPrescale1024;

	// param�terstrukt�ra inicializ�l�sa
	TIMER_Init(TIMER0, &TIMER0_init);

	// Top �rt�k be�ll�t�sa
	// C�l: 1 sec-ig sz�moljon, ut�na h�v�djon az IT f�ggv�ny
	// clk = 14MHz -> 1 sec alatt 14000000 impulzus.
	// A timer 16 bites, �gy ha 1024-�s leoszutjuk az �rajelet, akkor 13672-ig sz�mol�s
	// felel meg a val�s�gban 1-sec id�nek
	TIMER_TopSet(TIMER0, 13672);

	// IT a tilt�sa perif�ri�n
	TIMER_IntDisable(TIMER0, TIMER_IF_OF);
	// IT flag t�rl�se a perif�ri�n
	TIMER_IntClear(TIMER0, TIMER_IF_OF);

	// CORE-ban az IT fleg t�rl�se �s IT enged�lyez�se
	NVIC_ClearPendingIRQ(TIMER0_IRQn);
	NVIC_EnableIRQ(TIMER0_IRQn);

}

// 1-�s id�z�t� enged�lyez�se, IT be�ll�t�sa
void timer1Setup(void){

	// param�terstrukt�ra l�trehoz�sa
	TIMER_Init_TypeDef TIMER1_init = TIMER_INIT_DEFAULT;

	// a prescaler-t �t�ll�tjuk
	TIMER1_init.prescale = timerPrescale1024;

	// param�terstrukt�ra inicializ�l�sa
	TIMER_Init(TIMER1, &TIMER1_init);

	// Top �rt�k be�ll�t�sa
	// C�l: 1 sec-ig sz�moljon, ut�na h�v�djon az IT f�ggv�ny
	// clk = 14MHz -> 1 sec alatt 14000000 impulzus.
	// A timer 16 bites, �gy ha 1024-�s leoszutjuk az �rajelet, akkor 13672-ig sz�mol�s
	// felel meg a val�s�gban 1-sec id�nek
	TIMER_TopSet(TIMER1, 13672);

	// IT a tilt�sa perif�ri�n
	TIMER_IntDisable(TIMER1, TIMER_IF_OF);
	// IT flag t�rl�se a perif�ri�n
	TIMER_IntClear(TIMER1, TIMER_IF_OF);

	// CORE-ban az IT fleg t�rl�se �s IT enged�lyez�se
	NVIC_ClearPendingIRQ(TIMER1_IRQn);
	NVIC_EnableIRQ(TIMER1_IRQn);

}

void uartSetup(void){

	// Param�tereknek alap�rt�k �ll�t�sa
	USART_InitAsync_TypeDef initasync = USART_INITASYNC_DEFAULT;

	// Feladban el��rt UART param�terek be�ll�t�sa
	initasync.baudrate = 115200;
	initasync.databits = usartDatabits8;
	initasync.parity = usartNoParity;
	initasync.stopbits = usartStopbits1;
	initasync.oversampling = usartOVS16;
	USART_InitAsync(UART0, &initasync);

	// UART pin-ek be�ll�t�sa
	GPIO_PinModeSet(gpioPortE, 0, gpioModePushPull, 1);
	GPIO_PinModeSet(gpioPortE, 1, gpioModeInput, 0);

	// UART port be�ll�t�sa
	GPIO->P[5].DOUT |= (1 << 7); // VCOM port engedelyezese
	UART0->ROUTE = (UART0->ROUTE & ~_UART_ROUTE_LOCATION_MASK) | UART_ROUTE_LOCATION_LOC1;
	UART0->ROUTE |= UART_ROUTE_RXPEN | UART_ROUTE_TXPEN;

	// IT enged�lyez�se a perif�ri�n
	USART_IntClear(UART0, UART_IF_RXDATAV);
	USART_IntEnable(UART0, UART_IF_RXDATAV);

	// IT enged�lyez�se a CORE-on
	NVIC_ClearPendingIRQ(UART0_RX_IRQn);
	NVIC_ClearPendingIRQ(UART0_TX_IRQn);
	NVIC_EnableIRQ(UART0_RX_IRQn);

}

//////////////////////////
// Megszak�t�si rutinok //
//////////////////////////

// 0-�s nyom�gomb IT kezel� f�ggv�nye
void GPIO_ODD_IRQHandler(void) {
	// A perif�ria IT flag t�rl�se
	GPIO_IntClear(1 << 9);

	// Az interrupt rutin funkci�j�nak kifejt�se
	// Mivel a f�ggv�ny fel �s lefut��lre is megh�v�dik, �gy az �ldetekt�l�s
	// az akut�lis gomb �llapot�b�l meg�llap�that�
	if(GPIO_PinInGet(gpioPortB,  9) == 0) {
			gomb0_lefut = true;
		}
	else {
		gomb0_lefut = false;
	}
}

// 1-es nyom�gomb IT kezel� f�ggv�nye
void GPIO_EVEN_IRQHandler(void) {
	// A perif�rie IT flag t�rl�se
	GPIO_IntClear(1 << 10);

	// Az interrupt rutin funkci�j�nak kifejt�se
	// Mivel a f�ggv�ny fel �s lefut��lre is megh�v�dik, �gy az �ldetekt�l�s
	// az akut�lis gomb �llapot�b�l meg�llap�that�
	if(GPIO_PinInGet(gpioPortB, 10) == 0) {
		gomb1_lefut = true;
	}
	else {
		gomb1_lefut = false;
	}
}

// RTC megszak�t�skezel� rutin
void RTC_IRQHandler(void) {
	// Interrupt flag t�rl�se
	RTC_IntClear(RTC_IFC_COMP0);

	// Flag be�ll�t�sa, majd a f� ciklusban a hosszabb m�veleteket elv�gezz�k
	rtc_megszakitas = true;

	// Az �ra id� �rt�k�nek n�vel�se 1 mp-el
	masodperc++;
	if(masodperc > 59) {
		masodperc = 0;
		perc++;
		if(perc > 59) {
			perc = 0;
			ora++;
			if(ora > 23) {
				ora = 0;
			}
		}
	}
}

// 0-�s id�z�t� IRQ rutinja
// Ez a f�ggv�ny olyan esem�nykor h�v�dik, mikor a k�t nyom�gomb 1 sec-ig nyomva van
// Ez�rt a feladata a k�t riaszt�si flag be�ll��tsa
void TIMER0_IRQHandler(void) {
	// Perif�ri�n IT flag t�rl�se
	TIMER_IntClear(TIMER0, TIMER_IF_OF);

	// riaszt�si flag-ek be�ll�t�sa
	szundi_torles = true;
	riasztas = false;			 // riaszt�si �llapot kikapcsol�sa
	riasztas_kikapcsolas = true; // riaszt�s kikapcsol�s t�rt�nt flag �ll�t�sa

}

// 1-�s id�z�t� IRQ rutinja
// Ez a f�ggv�ny olyan esem�nykor h�v�dik, mikor hib�s parancs�rkezik az uarton, majd elteleik 1 sec
// Ez�rt a feladata a GECKO jel kikapcsol�sa
void TIMER1_IRQHandler(void) {
	// Perif�ri�n IT flag t�rl�se
	TIMER_IntClear(TIMER1, TIMER_IF_OF);

	// riaszt�si flag-ek be�ll�t�sa
	gecko_kikapcsolas = true;

}

// UART IT kezel� f�ggv�ny
void UART0_RX_IRQHandler(void) {
	// 1 byte fogad�sa
	uart_byte = USART_RxDataGet(UART0);

	// A fogadott byte visszak�ld�se - echo
	USART_Tx(UART0, uart_byte);

	// UART megszakitas flag �ll�t�sa a f�ciklushoz
	uart_megszakitas = true;


	if(uartBuff.length < 16){
		uartBuff.character[uartBuff.length++] = uart_byte;
		if(uart_byte == '\n'){
			// �j parancs �rkezett

			// Az �rkezett parancs �tt�lt�se egy folgolgozand� bufferbe
			parancs_Buff = uartBuff;

			// �j parancs flag be�ll�t�sa
			uart_parancs = true;

			// uart buffer reset-se
			uartBuff.length = 0;
			memset(uartBuff.character, 0, sizeof uartBuff.character);
		}
	}
	else{
		// Ha t�l sok karakter �rkezik, akkor biztos hib�s form�tum� a parancs
		// uart buffer reset-se
		uartBuff.length = 0;
		memset(uartBuff.character, 0, sizeof uartBuff.character);

		uart_parancs_hiba = true;
	}
}

////////////////////////////////////////
// UART parancshoz tartoz� f�ggv�nyek //
////////////////////////////////////////

// Megviszg�lja az �rkez� parancs_Buff form�tum helyess�g�t
char parancs_kod(void){
	re_t regex;
	if(parancs_Buff.length > 0){
		switch (parancs_Buff.character[0]){
			case 'C':
				regex = re_compile("^C [0-2]?[0-9]:[0-5]?[0-9]:[0-5]?[0-9] \r\n$"); // C 12:12:12 \r\n es C 1:2:3 \r\n is ok
				if(re_matchp(regex, parancs_Buff.character) == 0){
					return 'C';
				}
				break;
			case 'W':
				regex = re_compile("^W [0-2]?[0-9]:[0-5]?[0-9] \r\n$"); // W 12:12 \r\n es W 1:2 \r\n = 01:02 is ok
				if(re_matchp(regex, parancs_Buff.character) == 0){
					return 'W';
				}
				break;
			case 'R':
				if(strcmp(parancs_Buff.character, "R ON \r\n")==0 || strcmp(parancs_Buff.character, "R OFF \r\n")==0){
					return 'R';
				}
				break;
			default:
				break;
		}
	}
	return 'X';
}

bool ora_beallitas(void) {
	char *p = strchr(parancs_Buff.character, ' ');
	uint8_t hour = atoi(++p);
	p = strchr(p, ':');
	uint8_t min = atoi(++p);
	p = strchr(p, ':');
	uint8_t sec = atoi(++p);
	if((hour >= 0 && hour < 24) && (min >= 0 && min < 60) && (sec >= 0 && sec < 60)){ // ha megfelelo az ido
		ora = hour;
		perc = min;
		masodperc = sec;
		return true;
	}
	return false;
}

bool riasztasi_ido_beallitas(void) {
	char *p = strchr(parancs_Buff.character, ' ');
	uint8_t hour  = atoi(++p);
	p = strchr(p, ':');

	uint8_t min = atoi(++p);
	if((hour >= 0 && hour < 24) && (min >= 0 && min < 60)){ // ha megfelelo az ido
		riasztas_ora  = hour;
		riasztas_perc = min;
		szundi_ora = riasztas_ora;
		szundi_perc = riasztas_perc;
		riasztas_engedelyezes = true;
		SegmentLCD_Number(100*riasztas_ora+riasztas_perc);
		return true;
	}
	return false;
}

bool ebresztes_beallitas(void){
	if(strcmp(parancs_Buff.character, "R ON \r\n")==0){
		riasztas_engedelyezes = true;
		return true;
	}
	else if(strcmp(parancs_Buff.character, "R OFF \r\n")==0){
		riasztas_engedelyezes = false;
		if(riasztas)
		{
			szundi_torles = true;
			riasztas = false;
			riasztas_kikapcsolas = true;
		}
		return true;
	}
	else {
		return false;
	}
}

//////////////////////
// Egy�b f�ggv�nyek //
//////////////////////

// A szundi f�ggv�ny feladata
// kikapcsolja a folyamatban l�v� riaszt�st, �s be�ll�t egy �j riaszt�si id�pontot
// +10 perccel k�s�bbre
void szundi(void) {
	szundi_perc += SZUNDI_NOVELES;
	if(szundi_perc > 59)
	{
		szundi_perc-=60;
		szundi_ora++;
		if(szundi_ora > 23)
			szundi_ora = 0;
	}

	szundi_torles = false;
	riasztas = false;
	riasztas_kikapcsolas = true;

}

// F� ciklus
void Loop(void) {
		char aktualis_ido[7];				// az �ra id� r�gz�t�se ki�r�shoz
		bool gomb0_lefut_temp = false;		// gomb0 esem�nyek r�gz�t�se, true ha lefut, false ha fel
		bool gomb1_lefut_temp = false;		// gomb1 esem�nyek r�gz�t�se, true ha lefut, false ha fel
		bool egy_gomb_lefutott = false;		// legal�bb az egyik gomb le lett nyomva �llapot t�rol�sa
		bool mindket_gomb_lefutott = false;	// mindk�t gomb le lett nyomva �llapot t�rol�sa
		bool riasztas_inditas = false;

		// A szundi alap�rt�ke a riaszt�si id�
		szundi_ora = riasztas_ora;
		szundi_perc = riasztas_perc;

		// Alap�rt�kek ki�r�sa az LCD-re
		SegmentLCD_Number(100*riasztas_ora+riasztas_perc);
		CORE_CRITICAL_SECTION(
			sprintf(aktualis_ido, "%02d%02d%02d", ora, perc, masodperc);
		)
		SegmentLCD_Write(aktualis_ido);

		// fix �rt�kek ki�r�sa az LCD-re, ezek a kett�spontok
		SegmentLCD_Symbol(LCD_SYMBOL_COL3,  1);
		SegmentLCD_Symbol(LCD_SYMBOL_COL5,  1);
		SegmentLCD_Symbol(LCD_SYMBOL_COL10, 1);



		while(1) {

			// RTC megszak�t�si esem�ny kezel�se, m�sodpercenk�nt 1-szer h�v�dik meg
			if(rtc_megszakitas) {
				// Az IT rutinban �ll�t�d� id�pont elment�se string-k�nt a ki�r�shoz
				// Riaszt�s ind�t�s�nak vizsg�lata - Ha az �ra id� megegyezik a riaszt�si vagy szundi id�vel, akkor a riaszt�st be kell kapcsolni
				CORE_CRITICAL_SECTION(
					sprintf(aktualis_ido, "%02d%02d%02d", ora, perc, masodperc);
					riasztas_inditas = riasztas_engedelyezes && (((ora == riasztas_ora) && (perc == riasztas_perc) && (masodperc == 0)) || ((ora == szundi_ora) && (perc == szundi_perc) && (masodperc == 0)));
				)

				// Aktu�lis id� friss�t�se az lcd  kijelz�n
				SegmentLCD_Write(aktualis_ido);

				// Riaszt�s ind�t�sa ha a flag be van �ll�tva
				if(riasztas_inditas) {
					// riaszt�si flag be�ll�t�sa
					riasztas = true;
					// Az egyik LED-et be, m�sikat ki kapcsoljuk
					BSP_LedSet(0);
					BSP_LedClear(1);
					// Nyom�gomb IT ind�t�sa
					GPIO_IntClear(1 <<  9);
					GPIO_IntClear(1 << 10);
					GPIO_IntEnable(1<<9);
					GPIO_IntEnable(1<<10);
				}

				// Ha a riaszt�s akt�v, akkor a LED-eket villogtatjuk
				if(riasztas)
				{
					BSP_LedToggle(0);
					BSP_LedToggle(1);
				}

				// flag t�rl�se
				rtc_megszakitas = false;
			}

			// Riaszt�s kezel�se
			if(riasztas) {


				CORE_CRITICAL_SECTION(
					gomb0_lefut_temp = gomb0_lefut;
					gomb1_lefut_temp = gomb1_lefut;
				)

				// Legal�bb az egyik gomb meg van-e nyomva?
				if(gomb0_lefut_temp | gomb1_lefut_temp)
				{
					// Ha igen, akkor elenged�s eset�re fel kell k�sz�lni,
					// be�ll�tjuk hogy egyik gomb meg lett nyomva
					egy_gomb_lefutott = true;

					// Ha mind a k�t gomb nyomva van
					if(gomb0_lefut_temp && gomb1_lefut_temp)
					{
						// megvizsg�ljuk hogy elind�tottuk-e m�r a sz�ml�ls�t
						if( !mindket_gomb_lefutott ) {
							// ha nem akkor be�ll�tjuk a flag-et, sz�ml�l�t 0-zuk, majd enged�lyezz�k
							mindket_gomb_lefutott = true;
							TIMER_IntClear(TIMER0, TIMER_IF_OF);
							TIMER_CounterSet(TIMER0, 0);
							TIMER_IntEnable(TIMER0, TIMER_IF_OF);
						}
					}

					//Ha mindk�t gomb nyomva volt, de m�g nem telt le az 1-sec, �s az egyik gombot elengedj�k
					if(mindket_gomb_lefutott)
					{
						if(!(gomb0_lefut_temp && gomb1_lefut_temp)) {
							// TIMER0 megszak�t�s tilt�sa

							TIMER_IntDisable(TIMER0, TIMER_IF_OF);

							// flag t�rl�se
							mindket_gomb_lefutott = false;
						}
					}
				}
				else {
					// ha egyik gomb sincs jelenleg nyomva, meg kell n�zni volt-e kor�bban nyomva
					if(egy_gomb_lefutott) {
						// ha igen akkor szundit kell �ll�tani
						szundi();
					}
				}
			}
			else
			{
				// Ha a riaszt�s inakt�v meg kell n�zni megt�rt�nt-e m�r az alaphelyzetbe hozatala
				if(riasztas_kikapcsolas) {
					// gomb megszak�t�s tilt�sa
					GPIO_IntDisable(1<<9);
					GPIO_IntDisable(1<<10);

					// TIMER0 megszak�t�s tilt�sa
					TIMER_IntDisable(TIMER0, TIMER_IF_OF);

					// gomb flag-ek t�rl�se
					gomb0_lefut = false;
					gomb1_lefut = false;
					gomb0_lefut_temp = false;
					gomb1_lefut_temp = false;
					egy_gomb_lefutott = false;
					mindket_gomb_lefutott = false;

					// szundi idej�nek vissza�ll�t�sa
					if(szundi_torles) {
						szundi_ora = riasztas_ora;
						szundi_perc = riasztas_perc;
					}

					// LED-ek kikapcsol�sa
					BSP_LedClear(0);
					BSP_LedClear(1);

					// flag �ll�t�s
					riasztas_kikapcsolas = false;
				}
			}

			// UART megszak�t�shoz tartoz� feladatok
			if(uart_megszakitas) {
				// flag t�rl�se
				uart_megszakitas = false;

				// Ha �j parancs �rkezett
				if(uart_parancs) {

					uart_parancs = false;

					// Dek�doljuk a parancsot
					switch(parancs_kod()) {
					// �ra�ll�t�s
					case 'C': {
						if(!ora_beallitas()) {
							uart_parancs_hiba = true;
						}
						break;
					}
					// Riaszt�si id� �ll�t�s �s enged�lyez�s
					case 'W': {
						if(!riasztasi_ido_beallitas()) {
							uart_parancs_hiba = true;
						}
						break;
					}

					// Riaszt�s enged�lyez�s vagy tilt�s
					case 'R': {
						if(!ebresztes_beallitas()) {
							uart_parancs_hiba = true;
						}
						break;
					}

					// Egy�bk�nt pedig hib�s a parancs
					default: uart_parancs_hiba = true;
					}
				}

				// Ha hib�s a parancs
				if(uart_parancs_hiba)
				{
					uart_parancs_hiba = false;

					// Error �zenet �r�s UART-ra
					uint8_t i = 0;
					while(hiba_uzenet[i] != '\0'){
						 USART_Tx(UART0, hiba_uzenet[i++]);
					}
					// Gecko be�ll�t�s
					SegmentLCD_Symbol(LCD_SYMBOL_GECKO, 1);
					gecko_kikapcsolas = false;
					// TIMER1 ind�t�sa, mely 1 sec m�lva IT-t gener�l
					TIMER_IntClear(TIMER1, TIMER_IF_OF);
					TIMER_CounterSet(TIMER1, 0);
					TIMER_IntEnable(TIMER1, TIMER_IF_OF);
				}

				uart_megszakitas = false;
			}

			// Ha Letelt az 1 sec, akkor a Gecko jel kikapcsol�sa
			if(gecko_kikapcsolas)
			{
				TIMER_IntDisable(TIMER1, TIMER_IF_OF);
				gecko_kikapcsolas = false;
				SegmentLCD_Symbol(LCD_SYMBOL_GECKO, 0);
			}
		}
}
















