#include <stdint.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include "serialCom.h"

#define OUTPUT_MIN 0x8b00
#define OUTPUT_MAX 0x8b7f

#define RAM_MIN 0x0000
#define RAM_MAX 0x01ff


volatile register uint8_t ra asm ("r10");
volatile register uint8_t rx asm ("r11");
volatile register uint8_t ry asm ("r12");
volatile register uint8_t temp asm ("r14");

volatile uint8_t wait;
volatile uint8_t ioValue;

volatile uint8_t io;
volatile uint16_t overflow;
uint32_t cycles;
uint8_t m[512];
uint8_t rriot_ram[64];


void
set_up_timer(uint16_t start){
	 TCCR1A = 0;
	 TCCR1B |= (1 << CS10);
	 TIMSK1 |= (1 << TOIE1);
	 TCNT1 = start;
	 overflow = 0;
	 io = 0;
}

uint16_t
calculate_time(uint16_t counter){
	 return (uint16_t) counter/16;
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
	 uint8_t temp = 0;
	 cycles = 0;
	 ra = 0;
	 rx = 0;
	 ry = 0;
Lf000:
	 __asm__ volatile("clc");
	 //LDA
	 __asm__ volatile("mov %0, %1" : "=r"(ra) : "r"((uint8_t)65));
	 //NOP
	 //NOP
	 //BCC
	 temp = SREG;
	 cycles += 11;
	 SREG = temp;
	 if(!(SREG & (1 << 0))){
		 goto Lf007;
	 }
Lf007:
	 //LDX
	 __asm__ volatile("mov %0, %1" : "=r"(rx) : "r"((uint8_t)255));
	 cycles += 2;
Lf009:
	 //LDY
	 __asm__ volatile("mov %0, %1" : "=r"(ry) : "r"((uint8_t)255));
	 cycles += 2;
Lf00b:
	 //DEY
	 __asm__ volatile("dec %0" : "=r"(ry) : );
	 //BNE
	 temp = SREG;
	 cycles += 5;
	 SREG = temp;
	 if(!(SREG & (1 << 1))){
		 goto Lf00b;
	 }
	 //DEX
	 __asm__ volatile("dec %0" : "=r"(rx));
	 //BNE
	 temp = SREG;
	 cycles += 5;
	 SREG = temp;
	 if(!(SREG & (1 << 1))){
		 goto Lf009;
	 }
	 //STA
	 temp = SREG;
	 write8(35584, ra);
	 SREG = temp;
	 cycles += 4;
	 //ADC
	 __asm__ volatile("adc %0, %1" : "=r"(ra) : "r"((uint8_t) 1));
	 //CMP
	 __asm__ volatile("cp %0, %1" : "=r"(ra) : "r"((uint8_t) 91));
	 if((SREG & (1 << 0))){
		 __asm__ volatile("clc");
	 }else{
		 __asm__ volatile("sec");
	 }
	 //BCC
	 temp = SREG;
	 cycles += 7;
	 SREG = temp;
	 if(!(SREG & (1 << 0))){
		 goto Lf007;
	 }
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
