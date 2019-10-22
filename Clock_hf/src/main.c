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

struct Buffer {
	unsigned char character[16];
	uint8_t length;
};

struct Clock {
	uint8_t hour, min, sec;
};

struct Buffer buff = {.length = 0, .character = ""};
struct Clock alert;
struct Clock clock = {.hour = 0, .min = 0, .sec = 0};

void resetBuffer(void){
	buff.length = 0;
	memset(buff.character, 0, sizeof buff.character);
}

volatile bool newCommand = false; // erkezett-e uj parancs ( \n karakter )
volatile bool start = false; // init megtortent-e
volatile bool commandError = false; // hibas-e a parancs

volatile bool isAlert = false; //


char checkCommantIsValid(void){
	newCommand = false;
	re_t regex;
	if(buff.length > 0){
		switch (buff.character[0]){
			case 'C':
				regex = re_compile("^C [0-2]?[0-9]:[0-5]?[0-9]:[0-5]?[0-9] \r\n$");
				if(re_matchp(regex, buff.character) == 0){
					return 'C';
				}
				break;
			case 'W':
				regex = re_compile("^W [0-2]?[0-9]:[0-5]?[0-9] \r\n$");
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

bool setClockTime(void){
	char *p = strchr(buff.character, ' ');
	uint8_t hour  = atoi(++p);
	p = strchr(p, ':');
	uint8_t min = atoi(++p);
	p = strchr(p, ':');
	uint8_t sec = atoi(++p);

	if((hour >= 0 && hour < 24) && (min >= 0 && min < 60) && (sec >= 0 && sec < 60)){
		clock.hour = hour;
		clock.min = min;
		clock.sec = sec;
		USART_Tx(UART0, 'C');
		return true;
	}
	commandError = true;
	return false;
}

bool setAlertTime(void){
	char *p = strchr(buff.character, ' ');
	uint8_t hour  = atoi(++p);
	p = strchr(p, ':');
	uint8_t min = atoi(++p);
	if((hour >= 0 && hour < 24) && (min >= 0 && min < 60)){
		alert.hour = hour;
		alert.min = min;
		USART_Tx(UART0, 'W');
		return true;
	}
	commandError = true;
	return false;
}

void setAlertOnOff(void){
	if(strcmp(buff.character, "R ON \r\n")==0){
		isAlert = true;
		USART_Tx(UART0, '1');
	}
	else if(strcmp(buff.character, "R OFF \r\n")==0){
		isAlert = false;
		USART_Tx(UART0, '0');
	}
}

void sendError(void){
 char err[] = "Error \r\n";
 int i = 0;
 if(commandError){
	 while(err[i] != '\0'){
	 	 USART_Tx(UART0, err[i++]);
	  }
 }
 commandError = false;
}

void RTC_IRQHandler(void)
{
  /* Clear interrupt source */
  RTC_IntClear(RTC_IFC_COMP0);

  /* Increase time by one sec */
  ++clock.sec;
  if (clock.sec > 59) {
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
}

void UART0_RX_IRQHandler(void){
	unsigned char ch;
	ch = USART_RxDataGet(UART0);
	USART_Tx(UART0, ch); //echo
	if(buff.length < 15){
		buff.character[buff.length++] = ch;
		if(ch == '\n'){
			newCommand = true;
		}
	}
	else{
		resetBuffer();
		commandError = true;
	}
}

int main(void)
{
  CHIP_Init();

  InitClk();
  InitUart();
  InitGpio();
  InitRtc();

  BSP_LedsInit();
  SegmentLCD_Init(false);

  NVIC_ClearPendingIRQ(UART0_RX_IRQn);
  NVIC_ClearPendingIRQ(UART0_TX_IRQn);
  NVIC_EnableIRQ(UART0_RX_IRQn);

  NVIC_ClearPendingIRQ(GPIO_EVEN_IRQn); // clear buttons it flags
  NVIC_EnableIRQ(GPIO_EVEN_IRQn);
  NVIC_ClearPendingIRQ(GPIO_ODD_IRQn);
  NVIC_EnableIRQ(GPIO_ODD_IRQn);

  NVIC_ClearPendingIRQ(RTC_IRQn);
  NVIC_EnableIRQ(RTC_IRQn);

  /* Infinite loop */
  while (1) {
	  BSP_LedSet(1);
	  //BSP_LedToggle(0);
	  //BSP_LedToggle(1);
	  if(!start){
		  if(commandError){
			  sendError();
		  }
		  if(newCommand){
			  switch(checkCommantIsValid()){
			  case 'C':
			  	if(setClockTime()){

			  	}
			  	else{
			  		sendError();
			  	}
			  	break;
			  case 'W':
			  	if(setAlertTime()){
			  		start = true;
			  		SegmentLCD_Symbol(LCD_SYMBOL_COL10, 1); // lcd number :
			  		SegmentLCD_Symbol(LCD_SYMBOL_COL3, 1);  // hexa lcd :
			  		SegmentLCD_Symbol(LCD_SYMBOL_COL5, 1);	// hexa lcd :
			  		SegmentLCD_Number(alert.hour*100 + alert.min);
			  		RTC_Enable(true);
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
			  resetBuffer();
		  }
	  }
	  else{
		  char time[6];
		  int timeNumbers = clock.hour*10000 + clock.min * 100 + clock.sec;
		  sprintf(time, "%d", timeNumbers);
		  SegmentLCD_Write(time);
	  }
  }
}
