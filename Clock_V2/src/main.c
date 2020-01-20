#include "em_device.h"
#include "em_chip.h"
#include "../inc/re.h"


// F�program
int main(void) {

	// El�sz�r inicializ�l�sok elv�gz�se

	// Chip korrekci�k
	CHIP_Init();

	// Az �rajelfrekvencia ellen�rz�se, friss�t�se
	SystemCoreClockUpdate();

	// Az egyes modulok inicializ�l�sa
	cmuSetup();
	rtcSetup();
	gpioSetup();
	timer0Setup();
	timer1Setup();
	uartSetup();
	BSP_LedsInit();

	// LCD inicializ�l�sa fesz�lts�g boost n�lk�l
	SegmentLCD_Init(0);

	// V�gtelen�tett ciklus
	Loop();
}
