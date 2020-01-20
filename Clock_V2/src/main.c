#include "em_device.h"
#include "em_chip.h"
#include "../inc/re.h"


// Fõprogram
int main(void) {

	// Elõször inicializálások elvégzése

	// Chip korrekciók
	CHIP_Init();

	// Az órajelfrekvencia ellenõrzése, frissítése
	SystemCoreClockUpdate();

	// Az egyes modulok inicializálása
	cmuSetup();
	rtcSetup();
	gpioSetup();
	timer0Setup();
	timer1Setup();
	uartSetup();
	BSP_LedsInit();

	// LCD inicializálása feszültség boost nélkül
	SegmentLCD_Init(0);

	// Végtelenített ciklus
	Loop();
}
