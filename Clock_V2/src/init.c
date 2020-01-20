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

// Az RTC órajelfrekvenciája
#define RTC_FREQ    32768

// Szundi idejének a növelésének mértéke percben
#define SZUNDI_NOVELES 10

///////////////////////
// Globális változók //
///////////////////////

// 3 idõpontot kell tárolnunk:
volatile uint8_t ora = 0, perc = 0, masodperc = 0;	// valós idõ, mely másodpercenként változik
uint8_t riasztas_ora = 0, riasztas_perc = 0;		// riasztás ideje
uint8_t szundi_ora, szundi_perc;					// szundi ideje

// RTC megszakítási esemény flag-je, ha meghívódik RTC IT akkor beállítódik
// a lekezelés helyén törlõdik
volatile bool rtc_megszakitas = false;	// rtc IT esemény jelzése

// A két nyomógomb állípota, melyet a GPIO IT-k állítanak, és a riasztás során használunk fel
volatile bool gomb0_lefut = false;
volatile bool gomb1_lefut = false;

// A riasztás állapotát jelzõ flag, alapételmezetten, induláskor nincs riasztás, ezért false
// mivel több függvény is állíthatja, köztük IT esemény is, ezért lett globális-nak definiálva
volatile bool riasztas = false;
// Erre a flag-re akkor van szükség, mikor egy IT kikapcsolja a riasztást, akkor
// bizonyos flagek nullázását el kell végezni, de elég egyszer. Így õ a riasztás
// kikapcsolását jelzi
volatile bool riasztas_kikapcsolas = false;

// Buffer strukúra UART-hoz
struct Buffer {
	unsigned char character[16];
	uint8_t length;
};

// UART IT változói
// Megszakítás esemény jelzése a fõprogramnak
volatile bool uart_megszakitas = false;

// True, ha perencs érkezett
volatile bool uart_parancs = false;

// True, ha érvénytelen parancs érkezett
volatile bool uart_parancs_hiba = false;

// 1 byte fogadása UART-on kersztül
volatile unsigned char uart_byte;

// UART kommunikáció során töltjük fel az érkezõ adatokkal
struct Buffer uartBuff = {.length = 0, .character = ""};

// Ebbe töltjük át az érkezõ parancsot feldolgozásra
struct Buffer parancs_Buff = {.length = 0, .character = ""};

// Ez íródik ki UART-on, ha érvénytelen parancs érkezik
const char hiba_uzenet[] = "Error \r\n";

// TIMER1-hez tartozó változó
volatile bool gecko_kikapcsolas = false;

// Ha true, riasztás engedélyezve
bool riasztas_engedelyezes = false;

// Ha true, akkor egy riasztás kikapcsolása során visszaállítjuk a szunit a riasztási idõre
bool szundi_torles = true;

// Órajelek beállítása
void cmuSetup(void) {
	CMU_ClockSelectSet(cmuClock_HF, cmuSelect_HFRCO);

	// beállítjuk LFACLK forrásanak a LFXO-t
	CMU_ClockSelectSet(cmuClock_LFA, cmuSelect_LFXO);

	// Alacsony fogyasztású perifériák számára az órajel engedélyezése
	CMU_ClockEnable(cmuClock_CORELE, true);

	// Órajelosztás beállítása 32-vel
	CMU_ClockDivSet(cmuClock_RTC, cmuClkDiv_32);

	// Órajel engedélyezése az RTC periférián
	CMU_ClockEnable(cmuClock_RTC, true);

	// GPIO perifériának órajel engedélyezése
	CMU_ClockEnable(cmuClock_GPIO, true);

	// idõzítõ órajelének engedélyezése
	CMU_ClockEnable(cmuClock_TIMER0, true);

	// idõzítõ órajelének engedélyezése
	CMU_ClockEnable(cmuClock_TIMER1, true);

	// UART perifériának órajel engedélyezése
	CMU_ClockEnable(cmuClock_UART0, true);
}

// RTC periféria engedélyezése, IT beállítása
void rtcSetup(void) {
	// Inicializálási paramétereket tároló struktúra
	// Alapértékekkel töltjük fel
	RTC_Init_TypeDef rtcInit = RTC_INIT_DEFAULT;

	// RTC inicializálása
	rtcInit.enable		= false;	// akkor indítjuk a számlálást ha vége az inicializálásnak
	rtcInit.debugRun	= false;	// Az RTC tartja az értékét Debug során
	rtcInit.comp0Top	= true;		// számlálás 0-ról, ha elérte comp0 értéket
	RTC_Init(&rtcInit);

	// Megszakítás minden másodpercben
	RTC_CompareSet(0, (RTC_FREQ / 32)-1);

	// Megszakítás engedélyezése
	RTC_IntClear(RTC_IFC_COMP0);			// Periférián IT flag törlése
	RTC_IntEnable(RTC_IEN_COMP0);			// Periféirán IT engedélyezése
	NVIC_ClearPendingIRQ(RTC_IRQn);	// CORE-ban IT flag törlése
	NVIC_EnableIRQ(RTC_IRQn);				// CORE-ban IT engedélyezése

	// Számlálás indítása
	RTC_Enable(true);
}

// A GPIO periféria engedélyezése, majd az IT beállítása
void gpioSetup(void) {

	// A PB9 és PB10 lábak bemenetre állítása
	GPIO_PinModeSet(gpioPortB,  9, gpioModeInput, 0);
	GPIO_PinModeSet(gpioPortB, 10, gpioModeInput, 0);

	// Interruptok hozzárendelése a lábakhoz, beállítása és tiltása a periférián
	// Megszakítási esemény fel és lefutó élre is meghívódik
	GPIO_IntConfig(gpioPortB,  9, true, true, false);
	GPIO_IntConfig(gpioPortB, 10, true, true, false);

	// A periféria IT flag-ek törlése
	GPIO_IntClear(1 <<  9);
	GPIO_IntClear(1 << 10);

	// A core-ban a perifériához tartozó flagek törlése, majd IT engedélyezése
	NVIC_ClearPendingIRQ(GPIO_EVEN_IRQn);
	NVIC_EnableIRQ(GPIO_EVEN_IRQn);

	NVIC_ClearPendingIRQ(GPIO_ODD_IRQn);
	NVIC_EnableIRQ(GPIO_ODD_IRQn);
}

// 0-és idõzítõ engedélyezése, IT beállítása
void timer0Setup(void){
	// paraméterstruktúra létrehozása
	TIMER_Init_TypeDef TIMER0_init = TIMER_INIT_DEFAULT;

	// a prescaler-t átállítjuk
	TIMER0_init.prescale = timerPrescale1024;

	// paraméterstruktúra inicializálása
	TIMER_Init(TIMER0, &TIMER0_init);

	// Top érték beállítása
	// Cél: 1 sec-ig számoljon, utána hívódjon az IT függvény
	// clk = 14MHz -> 1 sec alatt 14000000 impulzus.
	// A timer 16 bites, így ha 1024-és leoszutjuk az órajelet, akkor 13672-ig számolás
	// felel meg a valóságban 1-sec idõnek
	TIMER_TopSet(TIMER0, 13672);

	// IT a tiltása perifárián
	TIMER_IntDisable(TIMER0, TIMER_IF_OF);
	// IT flag törlése a periférián
	TIMER_IntClear(TIMER0, TIMER_IF_OF);

	// CORE-ban az IT fleg törlése és IT engedélyezése
	NVIC_ClearPendingIRQ(TIMER0_IRQn);
	NVIC_EnableIRQ(TIMER0_IRQn);

}

// 1-és idõzítõ engedélyezése, IT beállítása
void timer1Setup(void){

	// paraméterstruktúra létrehozása
	TIMER_Init_TypeDef TIMER1_init = TIMER_INIT_DEFAULT;

	// a prescaler-t átállítjuk
	TIMER1_init.prescale = timerPrescale1024;

	// paraméterstruktúra inicializálása
	TIMER_Init(TIMER1, &TIMER1_init);

	// Top érték beállítása
	// Cél: 1 sec-ig számoljon, utána hívódjon az IT függvény
	// clk = 14MHz -> 1 sec alatt 14000000 impulzus.
	// A timer 16 bites, így ha 1024-és leoszutjuk az órajelet, akkor 13672-ig számolás
	// felel meg a valóságban 1-sec idõnek
	TIMER_TopSet(TIMER1, 13672);

	// IT a tiltása perifárián
	TIMER_IntDisable(TIMER1, TIMER_IF_OF);
	// IT flag törlése a periférián
	TIMER_IntClear(TIMER1, TIMER_IF_OF);

	// CORE-ban az IT fleg törlése és IT engedélyezése
	NVIC_ClearPendingIRQ(TIMER1_IRQn);
	NVIC_EnableIRQ(TIMER1_IRQn);

}

void uartSetup(void){

	// Paramétereknek alapérték állítása
	USART_InitAsync_TypeDef initasync = USART_INITASYNC_DEFAULT;

	// Feladban elõírt UART paraméterek beállítása
	initasync.baudrate = 115200;
	initasync.databits = usartDatabits8;
	initasync.parity = usartNoParity;
	initasync.stopbits = usartStopbits1;
	initasync.oversampling = usartOVS16;
	USART_InitAsync(UART0, &initasync);

	// UART pin-ek beállítása
	GPIO_PinModeSet(gpioPortE, 0, gpioModePushPull, 1);
	GPIO_PinModeSet(gpioPortE, 1, gpioModeInput, 0);

	// UART port beállítása
	GPIO->P[5].DOUT |= (1 << 7); // VCOM port engedelyezese
	UART0->ROUTE = (UART0->ROUTE & ~_UART_ROUTE_LOCATION_MASK) | UART_ROUTE_LOCATION_LOC1;
	UART0->ROUTE |= UART_ROUTE_RXPEN | UART_ROUTE_TXPEN;

	// IT engedélyezése a periférián
	USART_IntClear(UART0, UART_IF_RXDATAV);
	USART_IntEnable(UART0, UART_IF_RXDATAV);

	// IT engedélyezése a CORE-on
	NVIC_ClearPendingIRQ(UART0_RX_IRQn);
	NVIC_ClearPendingIRQ(UART0_TX_IRQn);
	NVIC_EnableIRQ(UART0_RX_IRQn);

}

//////////////////////////
// Megszakítási rutinok //
//////////////////////////

// 0-ás nyomógomb IT kezelõ függvénye
void GPIO_ODD_IRQHandler(void) {
	// A periféria IT flag törlése
	GPIO_IntClear(1 << 9);

	// Az interrupt rutin funkciójának kifejtése
	// Mivel a függvény fel és lefutúélre is meghívódik, így az éldetektálás
	// az akutális gomb állapotából megállapítható
	if(GPIO_PinInGet(gpioPortB,  9) == 0) {
			gomb0_lefut = true;
		}
	else {
		gomb0_lefut = false;
	}
}

// 1-es nyomógomb IT kezelõ függvénye
void GPIO_EVEN_IRQHandler(void) {
	// A periférie IT flag törlése
	GPIO_IntClear(1 << 10);

	// Az interrupt rutin funkciójának kifejtése
	// Mivel a függvény fel és lefutúélre is meghívódik, így az éldetektálás
	// az akutális gomb állapotából megállapítható
	if(GPIO_PinInGet(gpioPortB, 10) == 0) {
		gomb1_lefut = true;
	}
	else {
		gomb1_lefut = false;
	}
}

// RTC megszakításkezelõ rutin
void RTC_IRQHandler(void) {
	// Interrupt flag törlése
	RTC_IntClear(RTC_IFC_COMP0);

	// Flag beállítása, majd a fõ ciklusban a hosszabb mûveleteket elvégezzük
	rtc_megszakitas = true;

	// Az óra idõ értékének növelése 1 mp-el
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

// 0-ás idõzítõ IRQ rutinja
// Ez a függvény olyan eseménykor hívódik, mikor a két nyomógomb 1 sec-ig nyomva van
// Ezért a feladata a két riasztási flag beállíátsa
void TIMER0_IRQHandler(void) {
	// Periférián IT flag törlése
	TIMER_IntClear(TIMER0, TIMER_IF_OF);

	// riasztási flag-ek beállítása
	szundi_torles = true;
	riasztas = false;			 // riasztási állapot kikapcsolása
	riasztas_kikapcsolas = true; // riasztás kikapcsolás történt flag állítása

}

// 1-ás idõzítõ IRQ rutinja
// Ez a függvény olyan eseménykor hívódik, mikor hibás parancsérkezik az uarton, majd elteleik 1 sec
// Ezért a feladata a GECKO jel kikapcsolása
void TIMER1_IRQHandler(void) {
	// Periférián IT flag törlése
	TIMER_IntClear(TIMER1, TIMER_IF_OF);

	// riasztási flag-ek beállítása
	gecko_kikapcsolas = true;

}

// UART IT kezelõ függvény
void UART0_RX_IRQHandler(void) {
	// 1 byte fogadása
	uart_byte = USART_RxDataGet(UART0);

	// A fogadott byte visszaküldése - echo
	USART_Tx(UART0, uart_byte);

	// UART megszakitas flag állítása a fõciklushoz
	uart_megszakitas = true;


	if(uartBuff.length < 16){
		uartBuff.character[uartBuff.length++] = uart_byte;
		if(uart_byte == '\n'){
			// Új parancs érkezett

			// Az érkezett parancs áttöltése egy folgolgozandó bufferbe
			parancs_Buff = uartBuff;

			// Új parancs flag beállítása
			uart_parancs = true;

			// uart buffer reset-se
			uartBuff.length = 0;
			memset(uartBuff.character, 0, sizeof uartBuff.character);
		}
	}
	else{
		// Ha túl sok karakter érkezik, akkor biztos hibás formátumú a parancs
		// uart buffer reset-se
		uartBuff.length = 0;
		memset(uartBuff.character, 0, sizeof uartBuff.character);

		uart_parancs_hiba = true;
	}
}

////////////////////////////////////////
// UART parancshoz tartozó függvények //
////////////////////////////////////////

// Megviszgálja az érkezõ parancs_Buff formátum helyességét
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
// Egyéb függvények //
//////////////////////

// A szundi függvény feladata
// kikapcsolja a folyamatban lévõ riasztást, és beállít egy új riasztási idõpontot
// +10 perccel késõbbre
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

// Fõ ciklus
void Loop(void) {
		char aktualis_ido[7];				// az óra idõ rögzítése kiíráshoz
		bool gomb0_lefut_temp = false;		// gomb0 események rögzítése, true ha lefut, false ha fel
		bool gomb1_lefut_temp = false;		// gomb1 események rögzítése, true ha lefut, false ha fel
		bool egy_gomb_lefutott = false;		// legalább az egyik gomb le lett nyomva állapot tárolása
		bool mindket_gomb_lefutott = false;	// mindkét gomb le lett nyomva állapot tárolása
		bool riasztas_inditas = false;

		// A szundi alapértéke a riasztási idõ
		szundi_ora = riasztas_ora;
		szundi_perc = riasztas_perc;

		// Alapértékek kiírása az LCD-re
		SegmentLCD_Number(100*riasztas_ora+riasztas_perc);
		CORE_CRITICAL_SECTION(
			sprintf(aktualis_ido, "%02d%02d%02d", ora, perc, masodperc);
		)
		SegmentLCD_Write(aktualis_ido);

		// fix értékek kiírása az LCD-re, ezek a kettõspontok
		SegmentLCD_Symbol(LCD_SYMBOL_COL3,  1);
		SegmentLCD_Symbol(LCD_SYMBOL_COL5,  1);
		SegmentLCD_Symbol(LCD_SYMBOL_COL10, 1);



		while(1) {

			// RTC megszakítási esemény kezelése, másodpercenként 1-szer hívódik meg
			if(rtc_megszakitas) {
				// Az IT rutinban állítódó idõpont elmentése string-ként a kiíráshoz
				// Riasztás indításának vizsgálata - Ha az óra idõ megegyezik a riasztási vagy szundi idõvel, akkor a riasztást be kell kapcsolni
				CORE_CRITICAL_SECTION(
					sprintf(aktualis_ido, "%02d%02d%02d", ora, perc, masodperc);
					riasztas_inditas = riasztas_engedelyezes && (((ora == riasztas_ora) && (perc == riasztas_perc) && (masodperc == 0)) || ((ora == szundi_ora) && (perc == szundi_perc) && (masodperc == 0)));
				)

				// Aktuális idõ frissítése az lcd  kijelzõn
				SegmentLCD_Write(aktualis_ido);

				// Riasztás indítása ha a flag be van állítva
				if(riasztas_inditas) {
					// riasztási flag beállítása
					riasztas = true;
					// Az egyik LED-et be, másikat ki kapcsoljuk
					BSP_LedSet(0);
					BSP_LedClear(1);
					// Nyomógomb IT indítása
					GPIO_IntClear(1 <<  9);
					GPIO_IntClear(1 << 10);
					GPIO_IntEnable(1<<9);
					GPIO_IntEnable(1<<10);
				}

				// Ha a riasztás aktív, akkor a LED-eket villogtatjuk
				if(riasztas)
				{
					BSP_LedToggle(0);
					BSP_LedToggle(1);
				}

				// flag törlése
				rtc_megszakitas = false;
			}

			// Riasztás kezelése
			if(riasztas) {


				CORE_CRITICAL_SECTION(
					gomb0_lefut_temp = gomb0_lefut;
					gomb1_lefut_temp = gomb1_lefut;
				)

				// Legalább az egyik gomb meg van-e nyomva?
				if(gomb0_lefut_temp | gomb1_lefut_temp)
				{
					// Ha igen, akkor elengedés esetére fel kell készülni,
					// beállítjuk hogy egyik gomb meg lett nyomva
					egy_gomb_lefutott = true;

					// Ha mind a két gomb nyomva van
					if(gomb0_lefut_temp && gomb1_lefut_temp)
					{
						// megvizsgáljuk hogy elindítottuk-e már a számlálsát
						if( !mindket_gomb_lefutott ) {
							// ha nem akkor beállítjuk a flag-et, számlálót 0-zuk, majd engedélyezzük
							mindket_gomb_lefutott = true;
							TIMER_IntClear(TIMER0, TIMER_IF_OF);
							TIMER_CounterSet(TIMER0, 0);
							TIMER_IntEnable(TIMER0, TIMER_IF_OF);
						}
					}

					//Ha mindkét gomb nyomva volt, de még nem telt le az 1-sec, és az egyik gombot elengedjük
					if(mindket_gomb_lefutott)
					{
						if(!(gomb0_lefut_temp && gomb1_lefut_temp)) {
							// TIMER0 megszakítás tiltása

							TIMER_IntDisable(TIMER0, TIMER_IF_OF);

							// flag törlése
							mindket_gomb_lefutott = false;
						}
					}
				}
				else {
					// ha egyik gomb sincs jelenleg nyomva, meg kell nézni volt-e korábban nyomva
					if(egy_gomb_lefutott) {
						// ha igen akkor szundit kell állítani
						szundi();
					}
				}
			}
			else
			{
				// Ha a riasztás inaktív meg kell nézni megtörtént-e már az alaphelyzetbe hozatala
				if(riasztas_kikapcsolas) {
					// gomb megszakítás tiltása
					GPIO_IntDisable(1<<9);
					GPIO_IntDisable(1<<10);

					// TIMER0 megszakítás tiltása
					TIMER_IntDisable(TIMER0, TIMER_IF_OF);

					// gomb flag-ek törlése
					gomb0_lefut = false;
					gomb1_lefut = false;
					gomb0_lefut_temp = false;
					gomb1_lefut_temp = false;
					egy_gomb_lefutott = false;
					mindket_gomb_lefutott = false;

					// szundi idejének visszaállítása
					if(szundi_torles) {
						szundi_ora = riasztas_ora;
						szundi_perc = riasztas_perc;
					}

					// LED-ek kikapcsolása
					BSP_LedClear(0);
					BSP_LedClear(1);

					// flag állítás
					riasztas_kikapcsolas = false;
				}
			}

			// UART megszakításhoz tartozó feladatok
			if(uart_megszakitas) {
				// flag törlése
				uart_megszakitas = false;

				// Ha új parancs érkezett
				if(uart_parancs) {

					uart_parancs = false;

					// Dekódoljuk a parancsot
					switch(parancs_kod()) {
					// Óraállítás
					case 'C': {
						if(!ora_beallitas()) {
							uart_parancs_hiba = true;
						}
						break;
					}
					// Riasztási idõ állítás és engedélyezés
					case 'W': {
						if(!riasztasi_ido_beallitas()) {
							uart_parancs_hiba = true;
						}
						break;
					}

					// Riasztás engedélyezés vagy tiltás
					case 'R': {
						if(!ebresztes_beallitas()) {
							uart_parancs_hiba = true;
						}
						break;
					}

					// Egyébként pedig hibás a parancs
					default: uart_parancs_hiba = true;
					}
				}

				// Ha hibás a parancs
				if(uart_parancs_hiba)
				{
					uart_parancs_hiba = false;

					// Error üzenet írás UART-ra
					uint8_t i = 0;
					while(hiba_uzenet[i] != '\0'){
						 USART_Tx(UART0, hiba_uzenet[i++]);
					}
					// Gecko beállítás
					SegmentLCD_Symbol(LCD_SYMBOL_GECKO, 1);
					gecko_kikapcsolas = false;
					// TIMER1 indítása, mely 1 sec múlva IT-t generál
					TIMER_IntClear(TIMER1, TIMER_IF_OF);
					TIMER_CounterSet(TIMER1, 0);
					TIMER_IntEnable(TIMER1, TIMER_IF_OF);
				}

				uart_megszakitas = false;
			}

			// Ha Letelt az 1 sec, akkor a Gecko jel kikapcsolása
			if(gecko_kikapcsolas)
			{
				TIMER_IntDisable(TIMER1, TIMER_IF_OF);
				gecko_kikapcsolas = false;
				SegmentLCD_Symbol(LCD_SYMBOL_GECKO, 0);
			}
		}
}
















