#include <stdint.h>
#include <avr/io.h>
#include <util/delay.h>

#include "serialCom.h"

#define FOSC 16000000UL
#define BAUD 9600
#define MYUBRR FOSC/16/BAUD-1

uint8_t firstUse = 0;

void
USART_Init(unsigned int ubrr){
	/* Set baud rate*/
	UBRR0H = (unsigned char)(ubrr >> 8);
	UBRR0L = (unsigned char) ubrr;
	/* Enable receiver and transmitter */
	UCSR0B = (1<<RXEN0) | (1<<TXEN0);
	/* Set frame format: 8 data, 2 stop bit */
	UCSR0C = (1<<USBS0)|(3<<UCSZ00);
}

void
USART_Transmit(unsigned char data){
	/* Wait for empty transmit buffer */
	while(!(UCSR0A & (1 << UDRE0)));
	/* put data into buffer, send data */
	UDR0 = data;
}

void
putChar(uint8_t ch){
	if(firstUse == 0){
		USART_Init(MYUBRR);
		firstUse = 1;
	}
	USART_Transmit(ch);
}

void
putHex8(uint8_t hex){
	uint8_t hi = hex >> 4;
        if(hi >= 0xa){
                hi -= 0xa;
                putChar('A' + hi);
        }else{
                putChar('0' + hi);
        }

	uint8_t low = hex & 0xf;
	if(low >= 0xa){
		low -= 0xa;
		putChar('A' + low); 
	}else{
		putChar('0' + low);
	}
}

void
putHex16(uint16_t hex){
	putHex8(hex >> 8);
	putHex8(hex & 0xff);
}

void 
putBin8(uint8_t data){
	for(int32_t j=7; j >= 0; j--){
		if(data & (1<<j)){
			putChar('1');
		}else{
			putChar('0');
		}
	}
}

void
putDec16(uint16_t data){
	char str[4];
	uint16_t i = 0;

	do{
		str[i] = (data % 10) + '0';
		i++;
		data /= 10;
	}while(data > 0);
	uint32_t j;
    	for (j = i; j >= 0; j--) {
        	putChar(str[j]);
    	}
}
