#include <stdint.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include "serialCom.h"

#define OUTPUT_MIN 0x8b00
#define OUTPUT_MAX 0x8b7f

#define RAM_MIN 0x0000
#define RAM_MAX 0x01ff


uint8_t ra;
uint8_t rx;
uint8_t ry;
uint8_t rs;

volatile uint8_t wait;
volatile uint8_t ioValue;

volatile uint16_t overflow;
volatile uint8_t io;
uint8_t flags;
uint32_t cycles;
uint8_t m[512];
uint8_t rriot_ram[64];


void
set_up_timer(uint32_t start){
	 TCCR1A = 0;
	 TCCR1B |= (1 << CS10);
	 TIMSK1 |= (1 << TOIE1);
	 TCNT1 = start;
	 overflow = 0;
	 io = 0;
}

uint16_t
calculate_time(uint32_t counter){
	 return (uint16_t) counter/16;
}

void
setflag(int flag, uint8_t value){
	 if(value == 0){
		 flags &= ~(1 << flag);
	 }else{
		 flags |= (1 << flag);
	 }
}

void
write8(uint16_t address, uint8_t value){
	 if((address >= OUTPUT_MIN) && (address <= OUTPUT_MAX)){
		 uint32_t avrCycles = ((uint32_t) overflow << 16) | TCNT1;
		 TCNT1 = 0;
		 uint32_t time = (cycles * 16) - avrCycles;
		 overflow = time >> 16;
		 time = 0xffff - (time & 0xffff);
		 ioValue = value;
		 io = 1;
		 wait = 1;
		 TCNT1 += time;
		 while(wait);
		 TCNT1 = 0;
		 cycles = 0;
		 overflow = 0;
		 io = 0;
	 }else if((address >= RAM_MIN) && (address <= RAM_MAX)){
		 m[address] = value;
	 }
}


int main(void){
	 set_up_timer(0);
	 sei();
	 uint16_t temp = 0;
	 cycles = 0;
	 flags = 0;
	 ra = 0;
	 rx = 0;
	 ry = 0;
	 rs = 0xfd;
Lf000:
	 setflag(0, 0);
	 //LDA
	 ra = 65 & 0xFF;
	 setflag(7, ((ra & 0x80) > 0));
	 setflag(1, (ra == 0x00));
	 //BCC
	 if((flags & (1 << 0)) == 0){ cycles += 10;goto Lf007;}
	 cycles += 10;
Lf007:
	 //LDX
	 rx = 255 & 0xff;
	 setflag(7, ((rx & 0x80) > 0));
	 setflag(1, (rx == 0x00));
	 cycles += 2;
Lf009:
	 //LDY
	 ry = 255 & 0xff;
	 setflag(7, ((ry & 0x80) > 0));
	 setflag(1, (ry == 0x00));
	 cycles += 2;
Lf00b:
	 //DEY
	 ry = ry - 1;
	 setflag(1, (ry == 0x00));
	 setflag(7, ((ry & 0x80) > 0));
	 //BNE
	 if((flags & (1 << 1)) == 0){ cycles += 4; goto Lf00b;}
	 cycles += 4;
	 //DEX
	 rx = rx - 1;
	 setflag(1, (rx == 0x00));
	 setflag(7, ((rx & 0x80) > 0));
	 //BNE
	 if((flags & (1 << 1)) == 0){ cycles += 4; goto Lf009;}
	 cycles += 4;
	 //STA
	 write8(35584, ra);
	 cycles += 4;
	 //ADC
	 temp = ra + 1 + (flags & (1<<0));
	 setflag(6, ((((temp & 0xFF) ^ ra) & ((temp & 0xFF) ^ 1) & 0x80) > 0));
	 setflag(0, (temp > 0xFF));
	 setflag(1, (ra == 0x00));
	 setflag(7, ((ra & 0x80) > 0));
	 ra = (uint8_t) temp & 0xFF;
	 //CMP
	 temp = ra - 91;
	 setflag(0, ra >= 91);
	 setflag(1, (ra == 0x00));
	 setflag(7, ((ra & 0x80) > 0));
	 //BCC
	 if((flags & (1 << 0)) == 0){ cycles += 6;goto Lf007;}
	 cycles += 6;
	 cycles += 7;
}

ISR(TIMER1_OVF_vect){
	 if(io && (overflow == 0)){
		 putChar(ioValue);
		 putChar('\n');
		 wait = 0;
	 }else if((io && (overflow != 0))){
		 overflow--;
	 }else{
		 overflow++;
	 }
}