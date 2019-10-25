#include "em_device.h"
#include "em_chip.h"
#include "em_usart.h"
#include "em_dma.h"
#include "em_rtc.h"
#include "bsp.h"
#include "segmentlcd.h"
#include <stdbool.h>
#include <string.h>
#include "../inc/init.h"
#include "../inc/re.h"
#include <math.h>

#define tizpercSzundi 600; // 60s * 10min = 600

struct Buffer {
	unsigned char character[16];
	uint8_t length;
};
struct Clock {
	uint8_t hour, min, sec;
};

struct Buffer uartBuff = {.length = 0, .character = ""}; // uart buffer
struct Buffer buff = {.length = 0, .character = ""}; // feldolgozo buffer, feldolgozo fuggvenyek ebbol olvassak az adatokat
struct Clock alert; // ebreszto oraja
struct Clock clock = {.hour = 0, .min = 0, .sec = 0}; // 'real time' ora

void resetBuffer(void){ // alaphelyzetbe allitja az uart bufferjet
	uartBuff.length = 0;
	memset(uartBuff.character, 0, sizeof uartBuff.character);
}
enum flag {nincsHiba, hiba, kiirhato, torolheto}; // hibajelzes gecko flagek
volatile bool newCommand = false; // erkezett-e uj parancs ( \n karakter )
volatile bool start = false; // init megtortent-e
volatile bool commandError = false; // hibas-e a parancs
volatile enum flag printGecko = nincsHiba;
volatile bool alertEnable = true;
volatile bool isAlert = false; //
volatile uint16_t  szundi = tizpercSzundi;
volatile bool isSzundi = false;

char checkCommantIsValid(void){ // kapott paracs ellenorzese
	re_t regex;
	if(buff.length > 0){
		switch (buff.character[0]){
			case 'C':
				regex = re_compile("^C [0-2]?[0-9]:[0-5]?[0-9]:[0-5]?[0-9] \r\n$"); // C 12:12:12 \r\n es C 1:2:3 \r\n is ok
				if(re_matchp(regex, buff.character) == 0){
					return 'C';
				}
				break;
			case 'W':
				regex = re_compile("^W [0-2]?[0-9]:[0-5]?[0-9] \r\n$"); // W 12:12 \r\n es W 1:2 \r\n = 01:02 is ok
				if(re_matchp(regex, buff.character) == 0){
					return 'W';
				}
				break;
			case 'R':
				if(strcmp(buff.character, "R ON \r\n")==0 || strcmp(buff.character, "R OFF \r\n")==0){
					return 'R';
				}
				break;
			default:
				break;
		}
	}
	commandError = true;
	return 'X';
}

bool setClockTime(void){ // beallitja az orat
	char *p = strchr(buff.character, ' ');
	uint8_t hour = atoi(++p);
	p = strchr(p, ':');
	uint8_t min = atoi(++p);
	p = strchr(p, ':');
	uint8_t sec = atoi(++p);

	if((hour >= 0 && hour < 24) && (min >= 0 && min < 60) && (sec >= 0 && sec < 60)){ // ha megfelelo az ido
		clock.hour = hour;
		clock.min = min;
		clock.sec = sec;
		return true;
	}
	commandError = true;
	return false;
}

bool setAlertTime(void){ // beallitja az ebresztot
	char *p = strchr(buff.character, ' ');
	uint8_t hour  = atoi(++p);
	p = strchr(p, ':');
	uint8_t min = atoi(++p);

	if((hour >= 0 && hour < 24) && (min >= 0 && min < 60)){ // ha megfelelo az ido
		alert.hour = hour;
		alert.min = min;
		return true;
	}
	commandError = true;
	return false;
}

void setAlertOnOff(void){ // beallitja az ebresztes engedelyezest
	if(strcmp(buff.character, "R ON \r\n")==0){
		alertEnable = true;
	}
	else if(strcmp(buff.character, "R OFF \r\n")==0){
		alertEnable = false;
		clearAlarm();
	}
}

void sendError(void){ // elkuldi a hibauzenetet
 printGecko = hiba;
 char err[] = "Error \r\n";
 int i = 0;
 if(commandError){
	 while(err[i] != '\0'){
	 	 USART_Tx(UART0, err[i++]);
	 }
 }
 commandError = false;
}

void setCommands(void){ // beallitja a kapott parancsot a ervenyes, ellenben errort kuld vissza
	newCommand = false;
	switch(checkCommantIsValid()){
		case 'C':
			if(!setClockTime()){
				sendError();
			}
			break;
		case 'W':
			if(setAlertTime()){
				if(!start){
					start = true;
					SegmentLCD_Symbol(LCD_SYMBOL_COL10, 1); // lcd number :
					SegmentLCD_Symbol(LCD_SYMBOL_COL3, 1);  // hexa lcd :
					SegmentLCD_Symbol(LCD_SYMBOL_COL5, 1);	// hexa lcd :
				}
				SegmentLCD_Number(alert.hour*100 + alert.min);
			}
			else{
				sendError();
			}
			break;
		case 'R':
			setAlertOnOff();
			break;
		default: sendError();
	}
}

volatile bool risingEdgeEven = false;
void GPIO_EVEN_IRQHandler(void){ // it flag 10
	GPIO_IntClear(1 << 10);
	if(!risingEdgeEven){ // felfuto el
		risingEdgeEven = true;
		if(isAlert){ // ha emellett meg ebresztes is van
			setSzundi();
			return;
		}
	}
	else{ // lefuto el
		risingEdgeEven = false;
	}
}
volatile bool risingEdgeOdd = false;
void GPIO_ODD_IRQHandler(void){ // it flag 9
	GPIO_IntClear(1 << 9);
	if(!risingEdgeOdd){ // felfuto el
		risingEdgeOdd = true;
		if(isAlert){ // ha emellett meg ebresztes is van
			setSzundi();
			return;
		}
	}
	else{ // lefuto el
		risingEdgeOdd = false;
	}
}

void RTC_IRQHandler(void) // RTC it
{
  RTC_IntClear(RTC_IFC_COMP0); // it torlese
  if(start){
	  ++clock.sec; // ido novelese
	  if(clock.sec > 59) {
		  clock.sec = 0;
	      ++clock.min;
	      if (clock.min > 59) {
	      	clock.min = 0;
	      	++clock.hour;
	      	if(clock.hour > 23){
	      		clock.hour = 0;
	      	}
	      }
	    }
	    if(alertEnable){ // engedelyezett ebresztes eseten
	    	if(risingEdgeEven && risingEdgeOdd){ // egyszerre van lenyomva mind2 button es meg nem tortent meg a felengedes
	    		clearAlarm();
	    	}
	    	if(isAlert && !isSzundi){ // ha az ebresztes aktiv ledek villogtatasa
	    		BSP_LedToggle(0);
	    		BSP_LedToggle(1);
	    	}
	    	// ha ido van, ebreszt illetve ha lejart a szundi szinten beallitja az ebresztest
	    	if((clock.hour == alert.hour && clock.min  == alert.min && clock.sec == 0) || szundi == 0){
	    		BSP_LedSet(1);
	    		isAlert = true;
	    		isSzundi = false;
	    		szundi = tizpercSzundi;
	    	}
	    	if(isSzundi && !isAlert){ // ha szundi modban van szamol vissza a megadott idorol
	    		--szundi;
	    	}
	    }
  }
  if(printGecko == hiba){ // Gecko kiiratasa a fociklusban
	  printGecko = kiirhato;
	  return;
  }
  if(printGecko == kiirhato){ // Gecko torlese a fociklusban
	  printGecko = torolheto;
	  return;
  }
}

void UART0_RX_IRQHandler(void){ // uart it
	unsigned char ch;
	ch = USART_RxDataGet(UART0); // karakter fogadasa
	USART_Tx(UART0, ch); //echo
	if(uartBuff.length < 15){
		uartBuff.character[uartBuff.length++] = ch;
		if(ch == '\n'){ // uj parancs erkezhetett
			buff = uartBuff;
			newCommand = true;
			resetBuffer();
		}
	}
	else{
		resetBuffer();
		commandError = true;
	}
}

void setSzundi(void){
	isSzundi = true;
	isAlert = false;
	BSP_LedClear(0);
	BSP_LedClear(1);
}

void clearAlarm(void){
	isSzundi = false;
	isAlert = false;
	BSP_LedClear(0);
	BSP_LedClear(1);
}

int main(void)
{
  CHIP_Init();
  BSP_LedsInit();
  SegmentLCD_Init(false);

  InitClk();
  InitUart();
  InitGpio();
  InitRtc();

  /* Infinite loop */
  while (1) {
	  if(printGecko == kiirhato){
		  SegmentLCD_Symbol(LCD_SYMBOL_GECKO, 1);
	  }
	  if(printGecko == torolheto){
		  printGecko = nincsHiba;
		  SegmentLCD_Symbol(LCD_SYMBOL_GECKO, 0);
	  }
	  if(commandError){
		  sendError();
	  }
	  if(newCommand){
		  setCommands();
	  }
	  if(start){
		  char time[6];
		  sprintf(time, "%02d%02d%02d", clock.hour,clock.min,clock.sec);
		  SegmentLCD_Write(time);
	  }
	 // SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
  }
}
