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
volatile register uint8_t rs asm ("r13");

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
	 rs = 0xfd;
Lf000:
	 __asm__ volatile("clc");
	 //LDA
	 __asm__ volatile("mov %0, %1" : "=r"(ra) : "r"((uint8_t)65));
	 if(ra & 0x80){
	 __asm__ volatile("sen");
	 }else{
	 __asm__ volatile("cln");
	}
	 if(ra == 0x0){
	 __asm__ volatile("sez");
	 }else{
	 __asm__ volatile("clz");
	}
	 cycles += 4;
Lf003:
	 //STA
	 temp = SREG;
	 write8(35584, ra);
	 SREG = temp;
	 cycles += 4;
	 //ADC
	 __asm__ volatile("adc %0, %1" : "=r"(ra) : "r"((uint8_t) 1));
	 //CMP
	 temp = SREG;
	 __asm__ volatile("bst %0, %1" : "=r"(temp) : "I"(3));
	 __asm__ volatile("cp %0, %1" : "=r"(ra) : "r"((uint8_t) 91));
	 if((SREG & (1 << 0))){
		 __asm__ volatile("clc");
	 }else{
		 __asm__ volatile("sec");
	 }
	 temp = 0;
	 __asm__ volatile("bld %0, %1" : "=r"(temp) : "I"(0));
	 if(temp){
 		 __asm__ volatile("sev");
	 }else{
 		 __asm__ volatile("clv");
	 }
	 //BCC
	 temp = SREG;
	 cycles += 6;
	 SREG = temp;
	 if(!(SREG & (1 << 0))){
		 goto Lf003;
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
