#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "6502_instructions.h"

#define MAX_HELPER_FUNCTIONS 9
#define RRIOT_RAM_START 0x8b80
#define RRIOT_ROM_START 0x8c00
#define RRIOT_IO_START 0x8b00

extern uint16_t pc;
extern uint8_t m[65536];
BinaryInstruction program_blocks[0xffff][255];

extern struct Instructions code[256];
enum { CF=0, ZF, IF, DF, BF, XX, VF, NF };
enum { RA=0, RY, RX, RS};

/* Communication with Translator*/
enum{DEFS, IR, BYTES, DYNAMIC};

extern int toSet;
extern uint8_t defs;
extern uint8_t uses;
extern uint8_t usedRegisters;
extern uint8_t optimization;
extern char cRepresentation [1024];
extern uint16_t cycles;
/* communication for printing */
enum{FALSE,TRUE};

int dynamic = FALSE;

uint16_t rriot_rom[1024];
uint16_t rriot_addr;
uint16_t rom_addresses[4096];
uint16_t rom_addr;

uint16_t jsr_counter;
uint16_t jsr[2048];
//TODO add and sub should be deleted
extern void (*used_helper_functions[9])(void);
int helperFunctions = 0;

/* Helper Functions */
uint16_t parameter; //set by addressing mode
uint8_t bytes;

extern uint8_t bcd;

void
add_used_helper_function(void (*helper_function)(void)){
	if(helperFunctions == MAX_HELPER_FUNCTIONS){
		return;
	}else{
		for(int i = 0; i < helperFunctions; i++){
			if(helper_function == used_helper_functions[i]){
				return;
			}
		}
		used_helper_functions[helperFunctions] = helper_function;
		helperFunctions++;
	}
}

int
is_absolute_address(uint8_t opcode){
	uint16_t addressingMode = code[opcode].addressingMode;
       	if((addressingMode == 0x9) || (addressingMode == 0x8) || (addressingMode == 0xa)){
		return 0;
	}else{
		return 1;
	}
}

int
is_in_ROM(uint16_t parameter){
	//in RRIOT_ROM or ROM
	return ((parameter >= RRIOT_ROM_START) && (parameter <= 0x8FFF)) || ((parameter >= 0xF000) && (parameter <= 0xFFFF));
}

void
check_for_indexed_addressing(uint8_t opcode){
	if((code[opcode].addressingMode == 0x15) || (code[opcode].addressingMode == 0x1d) || (code[opcode].addressingMode == 0x1)){
		//X-Register needed
		usedRegisters |= (1 << RX);
	}else if((code[opcode].addressingMode == 0x6) || (code[opcode].addressingMode == 0x19) || (code[opcode].addressingMode == 0x11)){
		//X-Register needed
                usedRegisters |= (1 << RY);
	}
}

void
call_corresponding_addressingMode(uint8_t opcode){
	//jumps
	if((opcode == 0x20) || (opcode == 0x4C)){
		absolute();
	}else if(opcode == 0x6C){
		indirect();
	}

        uint8_t addressingMode = code[opcode].addressingMode;
        switch(addressingMode){
                case 0x0:
                        relative();
                        return;
                case 0x1:
                        indexed_x();
                        return;
                case 0x11:
                        indirect_y();
                        return;
                case 0x5:
                        zeropage();
                        return;
                case 0x15:
                        zeropage_x();
                        return;
                case 0x6:
                        zeropage_y();
                        return;
                case 0x8:
                case 0xa:
                        implied();
                        return;
                case 0x9:
                        immediate();
                        return;
                case 0xd:
                        absolute();
                        return;
                case 0x1d:
                        absolute_x();
                        return;
                case 0x19:
                        absolute_y();
                        return;
                case 0x2d:
                        indirect();
                        return;
        };
}

int
is_dynamic(uint16_t address){
	//address is in RAM
	uint8_t page = address >> 8;
	if(page == 0x00 || page == 0x01){
		//address is in zeropage or stack
		return TRUE;
	}
	if((address >= RRIOT_RAM_START) && (address < RRIOT_ROM_START)){
		//address is in RRIOT RAM
		return TRUE;
	}
	return FALSE;
}

int 
is_IO_operation(uint16_t address){
	return ((address >= RRIOT_IO_START) && (address < RRIOT_RAM_START));
}

/* print surounding code */
uint16_t
get_min_rom(void){
	uint16_t min = 0xffff;
	for(int i = 0; i < rom_addr; i++){
		if(rom_addresses[i] < min){
			min = rom_addresses[i];
		}
	}
	return min;
}

uint16_t
get_max_rom(void){
        uint16_t max = 0x0;
        for(int i = 0; i < rom_addr; i++){
                if(rom_addresses[i] > max){
                        max = rom_addresses[i];
                }
        }
        return max;
}

uint16_t
get_min_rriot(void){
        uint16_t min = 0xffff;
        for(int i = 0; i < rriot_addr; i++){
                if(rriot_rom[i] < min){
                        min = rriot_rom[i];
                }
        }
        return min;
}

uint16_t
get_max_rriot(void){
        uint16_t max = 0x0;
        for(int i = 0; i < rriot_addr; i++){
                if(rriot_rom[i] > max){
                        max = rriot_rom[i];
                }
        }
        return max;
}


void
print_set_up_timer(void){
	printf("void\nset_up_timer(uint16_t start){\n");
        printf("\t TCCR1A = 0;\n");
        printf("\t TCCR1B |= (1 << CS10);\n");
	printf("\t TIMSK1 |= (1 << TOIE1);\n");
        printf("\t TCNT1 = start;\n");
	printf("\t overflow = 0;\n");
	printf("\t io = 0;\n");
	printf("}\n\n");
}

void
print_calculate_time_cycles(void){
	printf("uint16_t\ncalculate_time(uint16_t counter){\n");
	printf("\t return (uint16_t) counter/16;\n");
	printf("}\n\n");
}


void
print_prolog(void){
        printf("#include <stdint.h>\n");
        printf("#include <avr/io.h>\n");
        printf("#include <avr/interrupt.h>\n");
        printf("#include \"serialCom.h\"\n\n");

        printf("#define OUTPUT_MIN 0x8b00\n");
        printf("#define OUTPUT_MAX 0x8b7f\n\n");

	printf("#define RAM_MIN 0x0000\n");
        printf("#define RAM_MAX 0x01ff\n\n");

	if(rom_addr > 0){
		printf("#define ROM_MIN %d\n", get_min_rom());
	}
	if(rriot_addr > 0){
		printf("#define RRIOT_MIN %d\n", get_min_rriot());
	}
	printf("\n");
}

void
print_global_vars_and_functions(void){
	if(optimization){
		if(usedRegisters & (1 << RA)){
			printf("volatile register uint8_t ra asm (\"r10\");\n");
		}
		if(usedRegisters & (1 << RX)){
        		printf("volatile register uint8_t rx asm (\"r11\");\n");
		}
		if(usedRegisters & (1 << RY)){
        		printf("volatile register uint8_t ry asm (\"r12\");\n");
		}
		if(usedRegisters & (1 << RS)){
        		printf("volatile register uint8_t rs asm (\"r13\");\n\n");
		}
	}else{
		printf("volatile register uint8_t ra asm (\"r10\");\n");
		printf("volatile register uint8_t rx asm (\"r11\");\n");
		printf("volatile register uint8_t ry asm (\"r12\");\n");
		printf("volatile register uint8_t rs asm (\"r13\");\n\n");
	}
	printf("volatile register uint8_t temp asm (\"r14\");\n\n");

        printf("volatile uint8_t wait;\n");
        printf("volatile uint8_t ioValue;\n\n");
	printf("volatile uint8_t io;\n");
        printf("volatile uint16_t overflow;\n");

        printf("uint32_t cycles;\n");
        printf("uint8_t m[512];\n");
	printf("uint8_t rriot_ram[64];\n\n");

	if(rom_addr > 0){
		printf("uint16_t rom[%d] = {", rom_addr);
		uint16_t end = get_max_rom();
		for(int i = get_min_rom(); i < end; i++){
			printf("%d, ", m[i]);
		}
		printf("%d};\n", m[end]);
	}
	if(rriot_addr > 0){
                printf("uint16_t rriot_rom[%d] = {", rriot_addr);
                uint16_t end = get_max_rriot();
                for(int i = get_min_rriot(); i < end; i++){
                        printf("%d, ", m[i]);
                }
                printf("%d};\n", m[end]);
        }
	printf("\n");

        print_set_up_timer();
        print_calculate_time_cycles();
}

void
print_main(void){
	printf("int main(void){\n");
        printf("\t set_up_timer(0);\n");
	printf("\t sei();\n");
	printf("\t uint8_t temp = 0;\n");
//	printf("\t sei();\n");
        printf("\t cycles = 0;\n");
	if(optimization){
        	if(usedRegisters & (1 << RA)){
			printf("\t ra = 0;\n");
		}
		if(usedRegisters & (1 << RX)){
        		printf("\t rx = 0;\n");
		}
		if(usedRegisters & (1 << RY)){
        		printf("\t ry = 0;\n");
		}
		if(usedRegisters & (1 << RS)){
        		printf("\t rs = 0xfd;\n");
		}
	}else{
		printf("\t ra = 0;\n");
		printf("\t rx = 0;\n");
		printf("\t ry = 0;\n");
		printf("\t rs = 0xfd;\n");
	}
}

void
print_epilog(void){
	printf("}\n\n");

        printf("ISR(TIMER1_OVF_vect){\n");
        printf("\t if(io && (overflow == 0)){\n");
        printf("\t\t putChar(ioValue);\n");
        printf("\t\t putChar('\\n');\n");
        printf("\t\t wait = 0;\n");
        printf("\t }else if((io && (overflow != 0))){\n");
        printf("\t\t overflow--;\n");
        printf("\t }else{\n");
        printf("\t\t overflow++;\n");
        printf("\t }\n");
	printf("}\n");
}

/* Code for printing flags */

void
invert_CF_flag(void){
	if(optimization && ((defs & (1 << CF)) == 0)){
		return;
	}
	printf("\t if((SREG & (1 << 0))){\n");
        printf("\t\t __asm__ volatile(\"clc\");\n");
        printf("\t }else{\n");
        printf("\t\t __asm__ volatile(\"sec\");\n");
        printf("\t }\n");
}

void
set_missing_flags(uint8_t reg){
	if(optimization && (!((defs & (1 << NF)) || (defs & (1 << ZF))))){
		return;
	}

	if(optimization && (defs & (1 << NF))){
		if(reg == RA){
			printf("\t if(ra & 0x80){\n\t __asm__ volatile(\"sen\");\n\t }else{\n\t __asm__ volatile(\"cln\");\n\t}\n");
		}else if(reg == RX){
			printf("\t if(rx & 0x80){\n\t __asm__ volatile(\"sen\");\n\t }else{\n\t __asm__ volatile(\"cln\");\n\t}\n");
		}else if(reg == RY){
			printf("\t if(ry & 0x80){\n\t __asm__ volatile(\"sen\");\n\t }else{\n\t __asm__ volatile(\"cln\");\n\t}\n");
		}
	}
	if(optimization && (defs & (1 << ZF))){
		if(reg == RA){
                        printf("\t if(ra == 0x0){\n\t __asm__ volatile(\"sez\");\n\t }else{\n\t __asm__ volatile(\"clz\");\n\t}\n");
                }else if(reg == RX){
                        printf("\t if(rx == 0x0){\n\t __asm__ volatile(\"sez\");\n\t }else{\n\t __asm__ volatile(\"clz\");\n\t}\n");
                }else if(reg == RY){
                        printf("\t if(ry == 0x0){\n\t __asm__ volatile(\"sez\");\n\t }else{\n\t __asm__ volatile(\"clz\");\n\t}\n");
                }
	}
	if(!optimization){
		if(reg == RA){
                        printf("\t if(ra & 0x80){\n\t __asm__ volatile(\"sen\");\n\t }else{\n\t __asm__ volatile(\"cln\");\n\t}\n");
			printf("\t if(ra == 0x0){\n\t __asm__ volatile(\"sez\");\n\t }else{\n\t __asm__ volatile(\"clz\");\n\t}\n");
                }else if(reg == RX){
                        printf("\t if(rx & 0x80){\n\t __asm__ volatile(\"sen\");\n\t }else{\n\t __asm__ volatile(\"cln\");\n\t}\n");
			printf("\t if(rx == 0x0){\n\t __asm__ volatile(\"sez\");\n\t }else{\n\t __asm__ volatile(\"clz\");\n\t}\n");
                }else if(reg == RY){
                        printf("\t if(ry & 0x80){\n\t __asm__ volatile(\"sen\");\n\t }else{\n\t __asm__ volatile(\"cln\");\n\t}\n");
			printf("\t if(ry == 0x0){\n\t __asm__ volatile(\"sez\");\n\t }else{\n\t __asm__ volatile(\"clz\");\n\t}\n");
                }

	}


/*	if(!optimization || (!((uses & (1 << VF)) || (uses & (1 << CF))))){
		if((uses & (1 << VF)) && (!((uses & (1 << CF))))){
			//only VF used
			printf("\t __asm__ volatile(\"bst %%0, 3\" : : \"r\"(SREG));\n");	
		}else if((uses & (1 << CF)) && (!((uses & (1 << VF))))){
			//only CF used
			printf("\t __asm__ volatile(\"bst %%0, 0\" : : \"r\"(SREG));\n");
		}else{
			//both used
			printf("\t temp = SREG;\n");
		}
	}
	if(reg == RA){
		printf("\t __asm__ volatile(\"cp %%0, %%1\" : \"=r\"(ra) : \"r\"((uint8_t)0));\n");
	}else if(reg == RX) {
		printf("\t __asm__ volatile(\"cp %%0, %%1\" : \"=r\"(rx) : \"r\"((uint8_t)0));\n");
	}else{
		printf("\t __asm__ volatile(\"cp %%0, %%1\" : \"=r\"(ry) : \"r\"((uint8_t)0));\n");
	}
	if(!optimization || (!((uses & (1 << VF)) || (uses & (1 << CF))))){
		if((uses & (1 << VF)) && (!((uses & (1 << CF))))){
                        //only VF used
			printf("\t __asm__ volatile(\"bld %%0, 0\" : \"=r\"(temp) : );\n");
                        printf("\t if(temp){\n \t\t __asm__ volatile(\"sev\");\n\t }else{\n \t\t __asm__ volatile(\"clv\");\n\t }\n");
                }else if((uses & (1 << CF)) && (!((uses & (1 << VF))))){
                        //only CF used
			printf("\t __asm__ volatile(\"bld %%0, 0\" : \"=r\"(temp) : );\n");
                        printf("\t if(temp){\n \t\t __asm__ volatile(\"sec\");\n\t }else{\n \t\t __asm__ volatile(\"clc\");\n\t }\n");
                }else{
			printf("\t if(temp & (1 << 0)){\n\t __asm__ volatile(\"sec\");\n\t }else{\n\t __asm__ volatile(\"clc\");\n\t }\n");
			printf("\t if(temp & (1 << 3)){\n\t __asm__ volatile(\"sev\");\n\t }else{\n\t __asm__ volatile(\"clv\");\n\t }\n");
		}
	}*/
}

void
set_NF_ZF_for_memory(void){
	if(optimization){
		if(defs & (1 << NF)){
			call_corresponding_addressingMode(m[pc]);
                	printf("& 0x80){\n\t __asm__ volatile(\"sen\");\n\t }else{\n\t __asm__ volatile(\"cln\");\n\t}\n");
		}
		if(defs & (1 << ZF)){
			call_corresponding_addressingMode(m[pc]);
                	printf(" == 0x0){\n\t __asm__ volatile(\"sez\");\n\t }else{\n\t __asm__ volatile(\"clz\");\n\t}\n");
		}

	}else{
		printf("\t if("); 
		call_corresponding_addressingMode(m[pc]);
		printf("& 0x80){\n\t __asm__ volatile(\"sen\");\n\t }else{\n\t __asm__ volatile(\"cln\");\n\t}\n");
                printf("\t if("); 
		call_corresponding_addressingMode(m[pc]);
		printf(" == 0x0){\n\t __asm__ volatile(\"sez\");\n\t }else{\n\t __asm__ volatile(\"clz\");\n\t}\n");
	}
}

void
save_VF(void){
	printf("\t temp = SREG;\n");
        printf("\t __asm__ volatile(\"bst %%0, %%1\" : \"=r\"(temp) : \"I\"(3));\n");
}

void
restore_VF(void){
	printf("\t temp = 0;\n");
        printf("\t __asm__ volatile(\"bld %%0, %%1\" : \"=r\"(temp) : \"I\"(0));\n");
        printf("\t if(temp){\n \t\t __asm__ volatile(\"sev\");\n\t }else{\n \t\t __asm__ volatile(\"clv\");\n\t }\n");
}

void
print_ram_or_rriot(void){
	if((parameter >= RRIOT_RAM_START) && (parameter < RRIOT_ROM_START)){
		printf("\t rriot_ram[");
		if(toSet == DYNAMIC){
			call_corresponding_addressingMode(m[pc]);
		}else{
			printf("%d", parameter);
		}
		printf("] = rriot_ram[");
		if(toSet == DYNAMIC){
                        call_corresponding_addressingMode(m[pc]);
                }else{
                        printf("%d", parameter);
                }
	}else{
		printf("\t m[");
                if(toSet == DYNAMIC){
                        call_corresponding_addressingMode(m[pc]);
                }else{
                        printf("%d", parameter);
                }
                printf("] = m[");
                if(toSet == DYNAMIC){
                        call_corresponding_addressingMode(m[pc]);
                }else{
                        printf("%d", parameter);
                }
	}
	printf("]");
}
/* possible Helper Functions for Programm Execution */
void
convert_number_to_bcd(void){
	printf("int\nconvert_number_to_bcd(int number){\n");
	printf("}");
}

void
convert_bcd_to_number(void){
	printf("int\nconvert_bcd_to_number(int bcd_number){\n");
	printf("}");
}

void
add(void){
	printf("int\nadd(uint8_t number, uint8_t add_term){\n");
	printf("}");
}

void
sub(void){
        printf("int\nsub(uint8_t number, uint8_t add_term){\n");
        printf("}");
}

void
setflag(void){
	printf("void\nsetflag(int flag, uint8_t value){\n");
        /*if(value == 0){
                //clear flag
                flags &= ~(1 << flag);
        }else if(value == 1){
                //set flag
                flags |= (1 << flag);
        }else{
                return;
        }*/
	printf("}");
}

void
write8(void){
	printf("void\nwrite8(uint16_t address, uint8_t value){\n");
	printf("\t if((address >= OUTPUT_MIN) && (address <= OUTPUT_MAX)){\n");
        printf("\t\t uint32_t avrCycles = ((uint32_t) overflow << 16) | TCNT1;\n");
        printf("\t\t TCNT1 = 0;\n");
        printf("\t\t uint32_t time = (cycles * 16) - avrCycles;\n");
        printf("\t\t overflow = time >> 16;\n");
        printf("\t\t time = 0xffff - (time & 0xffff);\n");
        printf("\t\t ioValue = value;\n");
        printf("\t\t io = 1;\n");
        printf("\t\t wait = 1;\n");
        printf("\t\t TCNT1 += time;\n");
        printf("\t\t while(wait);\n");
        printf("\t\t TCNT1 = 0;\n");
        printf("\t\t cycles = 0;\n");
        printf("\t\t overflow = 0;\n");
        printf("\t\t io = 0;\n");
        printf("\t }else if((address >= RAM_MIN) && (address <= RAM_MAX)){\n");
        printf("\t\t m[address] = value;\n");
        printf("\t }\n");
	printf("}\n");

}

void
read8(void){
	printf("uint8_t\nread8(uint16_t addr){\n");
	//TODO OS Functions
	//m represents RAM
	printf("\t if((addr >= RAM_MIN) && (addr <= RAM_MAX)){\n");
        printf("\t\t return m[addr];\n");
        printf("\t }else if((addr >= %d) && (addr <= %d)){\n", RRIOT_RAM_START, RRIOT_ROM_START);
        printf("\t\t return rriot_ram[addr ^ %d];\n", RRIOT_RAM_START);
	if(rom_addr > 0){
		printf("\t }else if((addr >= ROM_MIN) && (addr <= %d)){\n", get_max_rom());
		printf("\t\t return rom[addr ^ ROM_MIN];\n");
	}
	if(rriot_addr > 0){
		printf("\t }else if((addr >= RRIOT_MIN) && (addr <= %d)){\n", get_max_rriot());
                printf("\t\t return rom[addr ^ RRIOT_MIN];\n");
	}
	printf("\t }\n");
        printf("\t return 0;\n");
        printf("}\n");
}

void
pull8(void){
	printf("uint8_t\npull8(void){\n");
	printf("\t rs++;\n");
	printf("\t return m[0x0100 + rs];\n");
	printf("}\n");;
}

void
push8(void){
	printf("void\npush8(uint8_t value){\n");
	printf("\t write8(0x100+rs, value);\n");
	printf("\t rs--;\n");
	printf("}\n");
}

void
reset(void){
	printf("void\nreset(void){\n");
	printf("}\n");
}

/* Instructions */

void ADC(void){ //ADC... add memory to accumulator with carry
        if(toSet == DEFS){
		defs = (1 << NF) | (1 << VF) | (1 << ZF) | (1 << CF);
		uses = (1 << CF);
		
		if(!(code[m[pc]].addressingMode == 0x9)){
			add_used_helper_function(read8);
		}
		usedRegisters = (1 << RA);
		check_for_indexed_addressing(m[pc]);
	}else if(toSet == IR){
		printf("\t //ADC\n");
		call_corresponding_addressingMode(m[pc]);
		if(is_absolute_address(m[pc])){
			printf("\t __asm__ volatile(\"adc %%0, %%1\" : \"=r\"(ra) : \"r\"((uint8_t) ");
			if(toSet == DYNAMIC){
				printf("read8(");
				call_corresponding_addressingMode(m[pc]);
                                printf(")));\n");
                                toSet = IR;
			}else{
				printf("%d));\n", (uint8_t) m[parameter]);
			}	
		}else{
			printf("\t __asm__ volatile(\"adc %%0, %%1\" : \"=r\"(ra) : \"r\"((uint8_t) %d));\n", (uint8_t) parameter);
		}
	}
}

void AND(void){ //AND Memory with accumulator
        if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF); 
		uses = 0;
		if(!(code[m[pc]].addressingMode == 0x9)){
			add_used_helper_function(read8);
		}
		usedRegisters = (1 << RA);
		check_for_indexed_addressing(m[pc]);
	}else if(toSet == IR){
		printf("\t //AND\n");
		call_corresponding_addressingMode(m[pc]);
		if(!optimization || (uses & (1 <<  VF))){
                        save_VF();
                }
		if(is_absolute_address(m[pc])){
			printf("\t __asm__ volatile(\"and %%0, %%1\" : \"=r\"(ra) : \"r\"(");
			if(toSet == DYNAMIC){
				printf("read8(");
                                call_corresponding_addressingMode(m[pc]);
                                printf(")));\n");
                                toSet = IR;
                        }else{
				printf("%d));\n", m[parameter]);
			}
		}else{
			printf("\t __asm__ volatile(\"and %%0, %%1\" : \"=r\"(ra) : \"r\"(%d));\n", parameter);
		}
		if(!optimization || (uses & (1 <<  VF))){
                        restore_VF();
                }

	}
}

void ASL(void){ //Shift Left One Bit (Memory or Accumulator)
	if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF) | (1 << CF);
                uses = 0; 
		if(code[m[pc]].addressingMode == 0xa){
			usedRegisters = (1 << RA);
		}else{
			add_used_helper_function(read8);
		}
		check_for_indexed_addressing(m[pc]);

        }else if(toSet == IR){
		printf("\t //ASL\n");
		call_corresponding_addressingMode(m[pc]);
             
		if(is_absolute_address(m[pc]) == 0){
			if(!optimization || (uses & (1 <<  VF))){
				save_VF();
			}
                        printf("\t __asm__ volatile(\"lsl %%0\" : \"=r\"(ra));\n");
			if(!optimization || (uses & (1 <<  VF))){
                	        restore_VF();
	                }
                }else{
			if(is_in_ROM(parameter)){
                                //ROM cannot be modified
                                return;
                        }
			//set CF Flag
			if(!optimization || (defs & (1 << CF))){
				printf("\t temp = read8(");
				call_corresponding_addressingMode(m[pc]);
				printf(") & 0x80);\n");
				printf("\t if(temp){\n");
				printf("\t\t __asm__ volatile(\"sec\");\n");
				printf("\t }else{\n");
				printf("\t\t __asm__ volatile(\"clc\");\n");
				printf("\t }\n");
			}

			print_ram_or_rriot();
			printf(" << 1;\n");
			set_NF_ZF_for_memory();
			toSet = IR;
                }
	}
}

void BCC(void){ //BCC... branch on carry clean (CF == 0)
        if(toSet == DEFS){
		defs = 0;
		uses = (1 << CF);
	}else if(toSet == IR){
		call_corresponding_addressingMode(m[pc]);
		printf("\t //BCC\n");
		printf("\t temp = SREG;\n");
                printf("\t cycles += %d;\n", cycles);
                printf("\t SREG = temp;\n");
		printf("\t if(!(SREG & (1 << %d))){\n", CF);     
                printf("\t\t goto L%x;\n", parameter);
		printf("\t }\n");
      		
	}
}

void BCS(void){ //BCS... branch on carry set (CF == 1)
	if(toSet == DEFS){
		defs = 0;
		uses = (1 << CF);
        }else if(toSet == IR){
		call_corresponding_addressingMode(m[pc]);
		printf("\t //BCS\n");
		printf("\t temp = SREG;\n");
                printf("\t cycles += %d;\n", cycles);
                printf("\t SREG = temp;\n");
                printf("\t if(SREG & (1 << %d)){\n", CF);
                printf("\t goto L%x;\n", parameter);
                printf("\t }\n");
	}
}

void BEQ(void){ //Branch on result zero (ZF == 1)
	if(toSet == DEFS){
		defs = 0;
		uses = (1 << ZF);
        }else if(toSet == IR){
		call_corresponding_addressingMode(m[pc]);
		printf("\t //BEQ\n");
		printf("\t temp = SREG;\n");
                printf("\t cycles += %d;\n", cycles);
                printf("\t SREG = temp;\n");
		printf("\t if(SREG & (1 << %d)){\n", ZF);
                printf("\t\t goto L%x;\n", parameter);
        	printf("\t }\n");
	}
}

void BIT(void){ //BIT... Test Bits in Memory with accumulator
	if(toSet == DEFS){
		defs = (1 << NF) | (1 << VF) | (1 << ZF);
		uses = 0;
        }else if(toSet == IR){
		call_corresponding_addressingMode(m[pc]);
		printf("\t //BIT\n");
		if(optimization){
			if(uses & (1 << CF)){
				printf("\t temp = (1 << 0);\n");
        			printf("\t __asm__ volatile(\"and %%0, %%1\" : \"=r\"(temp) : \"r\"(SREG));\n");
        			printf("\t __asm__ volatile(\"bst %%0, 0\" : : \"r\"(temp));\n");
			}
                        if(defs & (1 << ZF)){
                                printf("\t if(ra & ");
				if(toSet == DYNAMIC){
					printf("read8(");
                                	call_corresponding_addressingMode(m[pc]);
                               	 	printf(")){\n");
                                	toSet = IR;
				}else{
					printf("%d){\n", m[parameter]);
				}
                		printf("\t\t __asm__ volatile(\"clz\");\n");
        			printf("\t }else{\n");
                		printf("\t\t __asm__ volatile(\"sez\");\n");
        			printf("\t }\n");

                        }
                        if(defs & (1 << NF)){
                                printf("\t if(");
				if(toSet == DYNAMIC){
                                        printf("read8(");
                                        call_corresponding_addressingMode(m[pc]);
                                        printf(")");
                                        toSet = IR;
                                }else{
                                        printf("%d", m[parameter]);
                                }
                                printf(" & (1 << 7)){\n");
                		printf("\t\t __asm__ volatile(\"sen\");\n");
        			printf("\t }else{\n");
                		printf("\t\t __asm__ volatile(\"cln\");\n");
        			printf("\t }\n");
                        }
                        if(defs & (1 << VF)){
                                printf("\t if(");
				if(toSet == DYNAMIC){
                                	printf("read8(");
                               		call_corresponding_addressingMode(m[pc]);
                                	printf(")");
                                	toSet = IR;
                        	}else{
                                	printf("%d", m[parameter]);
                        	}
				printf(" & (1 << 6)){\n");
                                printf("\t\t __asm__ volatile(\"sev\");\n");
                                printf("\t }else{\n");
                                printf("\t\t __asm__ volatile(\"clv\");\n");
                                printf("\t }\n");
                        }
			if(uses & (1 << CF)){
				printf("\t __asm__ volatile(\"bld %%0, 0\" : \"=r\"(temp) : );\n");
        			printf("\t if((temp & (1 << 0)) == 1){\n");
                		printf("\t\t __asm__ volatile(\"sec\");\n");
        			printf("\t }else{\n");
                		printf("\t\t __asm__ volatile(\"clc\");\n");
        			printf("\t }\n");
			}
                }else{
			//save CF Flag
                        printf("\t temp = (1 << 0);\n");
                        printf("\t __asm__ volatile(\"and %%0, %%1\" : \"=r\"(temp) : \"r\"(SREG));\n");
	                printf("\t __asm__ volatile(\"bst %%0, 0\" : : \"r\"(temp));\n");
			//ZF = RA & m[parameter]
			printf("\t if(ra & ");
			if(toSet == DYNAMIC){
				printf("read8(");
                                call_corresponding_addressingMode(m[pc]);
                                printf(")){\n");
                                toSet = IR;
			}else{	
				printf("%d){\n", m[parameter]);
			}
                        printf("\t\t __asm__ volatile(\"clz\");\n");
                        printf("\t }else{\n");
                        printf("\t\t __asm__ volatile(\"sez\");\n");
                        printf("\t }\n");
			//NF = m[parameter] & (1 << 7)
			printf("\t if(");
			if(toSet == DYNAMIC){
				printf("read8(");
                                call_corresponding_addressingMode(m[pc]);
                                printf(")");
                                toSet = IR;
			}else{
				printf("%d", m[parameter]); 
			}
			printf(" & (1 << 7)){\n");
                        printf("\t\t __asm__ volatile(\"sen\");\n");
                        printf("\t }else{\n");
                        printf("\t\t __asm__ volatile(\"cln\");\n");
                        printf("\t }\n");
			//VF 
			printf("\t if(");
		       	if(toSet == DYNAMIC){
				printf("read8(");
                                call_corresponding_addressingMode(m[pc]);
                                printf(")");
                                toSet = IR;
			}else{	
				printf("%d", m[parameter]);
			}
			printf(" & (1 << 6)){\n");
                        printf("\t\t __asm__ volatile(\"sev\");\n");
                        printf("\t }else{\n");
                        printf("\t\t __asm__ volatile(\"clv\");\n");
                        printf("\t }\n");
			printf("\t __asm__ volatile(\"bld %%0, 0\" : \"=r\"(temp) : );\n");
                        printf("\t if((temp & (1 << 0)) == 1){\n");
                        printf("\t\t __asm__ volatile(\"sec\");\n");
                        printf("\t }else{\n");
                        printf("\t\t __asm__ volatile(\"clc\");\n");
                        printf("\t }\n");
		}               

	}
}

void BMI(void){ //BMI... Branch on result Minus (NF == 1)
	if(toSet == DEFS){
		defs = 0;
                uses = (1 << NF);
        }else if(toSet == IR){
		call_corresponding_addressingMode(m[pc]);
		printf("\t //BMI\n");
		printf("\t temp = SREG;\n");
                printf("\t cycles += %d;\n", cycles);
                printf("\t SREG = temp;\n");
                printf("\t if(SREG & (1 << 2)){\n");
                printf("\t\t goto L%x;\n", parameter);
                printf("\t }\n");
	}
}

void BNE(void){ //Branch on result not zero (ZF == 0)
      	if(toSet == DEFS){
		defs = 0;
		uses = (1 << ZF);
	}else if(toSet == IR){
		call_corresponding_addressingMode(m[pc]);
		printf("\t //BNE\n");
		printf("\t temp = SREG;\n");
                printf("\t cycles += %d;\n", cycles);
                printf("\t SREG = temp;\n");
                printf("\t if(!(SREG & (1 << %d))){\n", ZF);
                printf("\t\t goto L%x;\n", parameter);
                printf("\t }\n");
	}
}

void BPL(void){ //BPL... Branch on Result Plus (NF == 0) 
	if(toSet == DEFS){
		defs = 0;
		uses = (1 << NF);
        }else if(toSet == IR){
		call_corresponding_addressingMode(m[pc]);
		printf("\t //BPL\n");
		printf("\t temp = SREG;\n");
                printf("\t cycles += %d;\n", cycles);
                printf("\t SREG = temp;\n");
		printf("\t if(!(SREG & (1 << 2))){\n"); 
                printf("\t\t goto L%x;\n", parameter);
        	printf("\t }\n");
	}
}

void BRK(void){ //BRK is a software interrupt
			      // TODO or should it be implemented as return 0?
	if(toSet == DEFS){
		uses = 0;
		defs = (1 << BF)|(1 << XX)|(1 << IF);
        }else if(toSet == IR){
		printf("\t __asm__ volatile(\"ret\");\n");
	}
}

void BVC(void){ //BVC... Branch on Overflow Clear (VF == 0)
	if(toSet == DEFS){
		uses = (1 << VF);
		defs = 0;
        }else if(toSet == IR){
		call_corresponding_addressingMode(m[pc]);
		printf("\t //BVC\n");
		printf("\t temp = SREG;\n");
                printf("\t cycles += %d;\n", cycles);
                printf("\t SREG = temp;\n");
                printf("\t if(!(SREG & (1 << 3))){\n");
                printf("\t\t goto L%x;\n", parameter);
                printf("\t }\n");
	}
}

void BVS(void){ //BVS... Branch on Overflow Set (VF == 1)
	if(toSet == DEFS){
		uses = (1 << VF);
		defs = 0;
        }else if(toSet == IR){
		call_corresponding_addressingMode(m[pc]);
		printf("\t //BVS\n");
                printf("\t if(SREG & (1 << 3)){\n");
                printf("\t\t goto L%x;\n", parameter);
                printf("\t }\n");
	}
}

void CLC(void){ //CLC... clear carry flag
	if(toSet == DEFS){
		defs = (1 << CF);
		uses = 0;
	}else if(toSet == IR){
        	printf("\t __asm__ volatile(\"clc\");\n");
	}
}

void CLD(void){ //CLD... clear decimal mode
	if(toSet == DEFS){
		defs = (1 << DF);
		uses = 0;
		add_used_helper_function(setflag);
        }else if(toSet == IR){
		printf("\t setflag(DF, 0);\n");
		printf("\t pc++;\n");
	}
}

void CLI(void){ //Clear Interrupt Disable Bit
	if(toSet == DEFS){
		defs = (1 << IF);
		uses = 0;
        }else if(toSet == IR){
		printf("\t cli();\n\t pc++;");
	}
}

void CLV(void){ //CLV... clear overflow flag
	if(toSet == DEFS){
		uses = 0;
		defs = (1 << VF);
        }else if(toSet == IR){
		printf("\t __asm__ volatile(\"clv\");\n\t pc++;");
	}
}

void CMP(void){ //CMP... compare memory with accumulator
        if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF) | (1 << CF);
	       	uses = 0;
		if(is_absolute_address(m[pc])){
			add_used_helper_function(read8);
		}	
		usedRegisters = (1 << RA);
		check_for_indexed_addressing(m[pc]);
	}else if(toSet == IR){
		printf("\t //CMP\n");
		call_corresponding_addressingMode(m[pc]);
		if(!optimization || (uses & (1 <<  VF))){
			save_VF();
		}
		if(is_absolute_address(m[pc])){
			printf("\t __asm__ volatile(\"cp %%0, %%1\" : \"=r\"(ra) : \"r\"((uint8_t)");
			if(toSet == DYNAMIC){
                                printf("read8(");
                                call_corresponding_addressingMode(m[pc]);
                                printf(")));\n");
                                toSet = IR;
			}else{
				printf(" %d));\n", (uint8_t) m[parameter]);
			}
		}else{
			printf("\t __asm__ volatile(\"cp %%0, %%1\" : \"=r\"(ra) : \"r\"((uint8_t) %d));\n", (uint8_t) parameter);
		}
		//invert CF flag 
		invert_CF_flag();
		if(!optimization || (uses & (1 <<  VF))){
			restore_VF();
                }
        }
}

void CPX(void){ //CPX... compare Memory with Index X
	if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF) | (1 << CF);
                uses = 0;
		if(is_absolute_address(m[pc])){
                        add_used_helper_function(read8);
                }
		usedRegisters = (1 << RX);
		check_for_indexed_addressing(m[pc]);
        }else if(toSet == IR){
		printf("\t //CPX\n");
		call_corresponding_addressingMode(m[pc]);
		if(!optimization || (uses & (1 <<  VF))){
                        save_VF();
                }
                if(is_absolute_address(m[pc])){
                        printf("\t __asm__ volatile(\"cp %%0, %%1\" : \"=r\"(rx) : \"r\"((uint8_t)");
			if(toSet == DYNAMIC){
                                printf("read8(");
                                call_corresponding_addressingMode(m[pc]);
                                printf(")));\n");
                                toSet = IR;
                        }else{
				printf(" %d));\n", (uint8_t) m[parameter]);
			}
                }else{
                        printf("\t __asm__ volatile(\"cp %%0, %%1\" : \"=r\"(rx) : \"r\"((uint8_t) %d));\n", (uint8_t) parameter);
                }
                //invert CF flag 
                invert_CF_flag();
		if(!optimization || (uses & (1 <<  VF))){
                        restore_VF();
                }

	}
}

void CPY(void){ //CPY... compare Memory with Index Y
        //printf("CPY:\n");
	if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF) | (1 << CF);
                uses = 0;
		if(is_absolute_address(m[pc])){
                        add_used_helper_function(read8);
                }
		usedRegisters = (1 << RY);
		check_for_indexed_addressing(m[pc]);
        }else if(toSet == IR){
		printf("\t //CPY\n");
		call_corresponding_addressingMode(m[pc]);
		if(!optimization || (uses & (1 <<  VF))){
                        save_VF();
                }
                if(is_absolute_address(m[pc])){
                        printf("\t __asm__ volatile(\"cp %%0, %%1\" : \"=r\"(ry) : \"r\"((uint8_t)");
			if(toSet == DYNAMIC){
                                printf("read8(");
                                call_corresponding_addressingMode(m[pc]);
                                printf(")));\n");
                                toSet = IR;
                        }else{
				printf(" %d));\n", (uint8_t) m[parameter]);
			}
                }else{
                        printf("\t __asm__ volatile(\"cp %%0, %%1\" : \"=r\"(ry) : \"r\"((uint8_t)"); 
			if(toSet == DYNAMIC){
                                printf("read8(");
                                call_corresponding_addressingMode(m[pc]);
                                printf(")));");
                                toSet = IR;
                        }else{
				printf(" %d));\n", (uint8_t) m[parameter]);
			}
                }
                //invert CF flag 
                invert_CF_flag();
		if(!optimization || (uses & (1 <<  VF))){
                        restore_VF();
                }

	}
}

void DEC(void){ //Decrement Memory by one
	if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF);
                uses = 0;
		add_used_helper_function(write8);
		check_for_indexed_addressing(m[pc]);
        }else if(toSet == IR){
		printf("\t //DEC\n");
		call_corresponding_addressingMode(m[pc]);
		if(is_in_ROM(parameter)){
        	        //ROM cannot be modified
                        return;
                }

		if((parameter >= RRIOT_RAM_START) && (parameter < RRIOT_ROM_START)){
			printf("\t rriot_ram[");
		}else{
			printf("\t m[");
		}
		call_corresponding_addressingMode(m[pc]);
		printf("]--;\n");
		set_NF_ZF_for_memory();
		toSet = IR;
	}
}

void DEX(void){ //Decrement Index X by one
	if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF);
                uses = 0;
		usedRegisters = (1 << RX);
        }else if(toSet == IR){
		printf("\t //DEX\n");
		if(!optimization || (uses & (1 <<  VF))){
                        save_VF();
                }
		printf("\t __asm__ volatile(\"dec %%0\" : \"=r\"(rx));\n");
		if(!optimization || (uses & (1 <<  VF))){
                        restore_VF();
                }

	}
}

void DEY(void){ //Decrement Index Y by one
	if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF);
                uses = 0;
		usedRegisters = (1 << RY);
        }else if(toSet == IR){
		printf("\t //DEY\n");
		if(!optimization || (uses & (1 <<  VF))){
                        save_VF();
                }
		printf("\t __asm__ volatile(\"dec %%0\" : \"=r\"(ry) : );\n");
                if(!optimization || (uses & (1 <<  VF))){
                        restore_VF();
                }
	}
}

void EOR(void){ //Exclusive-OR Memory with Accumulator
	if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF);
                uses = 0;
		
		if(is_absolute_address(m[pc])){
			add_used_helper_function(read8);
		}
		usedRegisters = (1 << RA);
		check_for_indexed_addressing(m[pc]);
        }else if(toSet == IR){
		printf("\t //EOR\n");
		call_corresponding_addressingMode(m[pc]);
                if(!optimization || (uses & (1 <<  VF))){
                        save_VF();
                }
		if(is_absolute_address(m[pc]) == 1){
                        printf("\t __asm__ volatile(\"eor %%0, %%1\" : \"=r\"(ra) : \"r\"("); 
			if(toSet == DYNAMIC){
                                printf("read8(");
                                call_corresponding_addressingMode(m[pc]);
                                printf(")));\n");
                                toSet = IR;
                        }else{
				printf("%d));\n", m[parameter]);
			}
                }else{
                        printf("\t __asm__ volatile(\"eor %%0, %%1\" : \"=r\"(ra) : \"r\"(%d));\n", parameter);
                }
                if(!optimization || (uses & (1 <<  VF))){
                        restore_VF();
                }
	}
}

void INC(void){ //Increment Memory by one
	if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF);
                uses = 0;
		add_used_helper_function(write8);
		check_for_indexed_addressing(m[pc]);
        }else if(toSet == IR){
		printf("\t //INC\n");
		call_corresponding_addressingMode(m[pc]);
		if(is_in_ROM(parameter)){
                        //ROM cannot be modified
	                return;
                }
		
		if((parameter >= RRIOT_RAM_START) && (parameter < RRIOT_ROM_START)){
			printf("\t rriot_ram[");
		}else{
			printf("\t m[");
		}
		call_corresponding_addressingMode(m[pc]);
		printf("]++;\n");
		set_NF_ZF_for_memory();
		toSet = IR;
	}
}

void INX(void){ //Increment Index X by one
        if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF);
		uses = 0;
		usedRegisters = (1 << RX);

	}else if(toSet == IR){
		printf("\t //INX\n");
		call_corresponding_addressingMode(m[pc]);
                if(!optimization || (uses & (1 <<  VF))){
                        save_VF();
                }
		printf("\t __asm__ volatile(\"inc %%0\" : \"=r\"(rx));\n");
                if(!optimization || (uses & (1 <<  VF))){
                        restore_VF();
                }
	}
}

void INY(void){ //Increment Index Y by one
        //printf("INY:\n");
	if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF);
                uses = 0;
		usedRegisters = (1 << RY);
        }else if(toSet == IR){
		printf("\t //INY\n");
		call_corresponding_addressingMode(m[pc]);
		if(!optimization || (uses & (1 <<  VF))){
                        save_VF();
                }
		printf("\t __asm__ volatile(\"inc %%0\" : \"=r\"(ry));\n");
		if(!optimization || (uses & (1 <<  VF))){
                        restore_VF();
                }
	}
}

void JMP(void){ //Jump to address
        if(toSet == DEFS){
		defs = 0;
		uses = 0;
	}else if(toSet == IR){
		printf("\t //JMP\n");
		call_corresponding_addressingMode(m[pc]);
                if(dynamic == TRUE){
                        exit(1);
                }else{
                        printf("\t cycles += %d;\n", cycles);
                        printf("\t goto L%x;\n", parameter);

                }
	}
}

void JSR(void){ //Jump to subroutine
		//TODO Shadow Stack
	if(toSet == DEFS){
		defs = 0;
                uses = 0;
		call_corresponding_addressingMode(m[pc]);
		for(int i = 0; i < jsr_counter; i++){
			if(parameter == jsr[i]){
				return;
			}
		}
		jsr[jsr_counter] = parameter;
		jsr_counter++;
		//add_used_helper_function(push8);
        }else if(toSet == IR){
		printf("\t //JSR\n");
		//TODO pc -> wird nie verwendet
		//printf("\t pc = pc - 1;\n");
		//printf("\t push8((pc >> 8));\n");
		//printf("\t push8((pc & 0x00FF));\n");
		//printf("\t pc = %d;\n", parameter);
		call_corresponding_addressingMode(m[pc]);
		printf("\t cycles += %d;\n", cycles); 
		printf("\t __asm__ volatile(\"rcall L%x\");\n", parameter);
	}
}

void LDA(void){ //LDA... Load Accumulator with memory
	if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF);
		uses = 0;
		if(is_absolute_address(m[pc])){
			add_used_helper_function(read8);
		}
		usedRegisters = (1 << RA);
		check_for_indexed_addressing(m[pc]);
	}else if(toSet == IR){
		printf("\t //LDA\n");
		call_corresponding_addressingMode(m[pc]);

		if(is_absolute_address(m[pc])){
			printf("\t __asm__ volatile(\"mov %%0, %%1\" : \"=r\"(ra) : \"r\"((uint8_t)");
			if(toSet == DYNAMIC){
				printf("read8(");
				call_corresponding_addressingMode(m[pc]);
                        	printf(")));\n");
				toSet = IR;
			}else{
		       		printf("%d));\n", m[parameter]);
			}

		}else{
			printf("\t __asm__ volatile(\"mov %%0, %%1\" : \"=r\"(ra) : \"r\"((uint8_t)%d));\n", parameter);
		}
		//to get zero and negative flag
                set_missing_flags(RA);
		//printf("\t pc += %d;\n", bytes);
	}
}

void LDX(void){ //LDX... Load Index X with memory
        if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF);
		uses = 0;
		if(is_absolute_address(m[pc])){
                        add_used_helper_function(read8);
                }
		usedRegisters = (1 << RX);
		check_for_indexed_addressing(m[pc]);
	}else if(toSet == IR){
		printf("\t //LDX\n");
		call_corresponding_addressingMode(m[pc]);
		if(is_absolute_address(m[pc])){
                        printf("\t __asm__ volatile(\"mov %%0, %%1\" : \"=r\"(rx) : \"r\"((uint8_t)"); 
			if(toSet == DYNAMIC){
				printf("read8(");
				call_corresponding_addressingMode(m[pc]);
                                printf(")));\n");
                                toSet = IR;
			}else{
				printf("%d));\n", m[parameter]);
			}
                }else{
                        printf("\t __asm__ volatile(\"mov %%0, %%1\" : \"=r\"(rx) : \"r\"((uint8_t)%d));\n", parameter);

                }
		//to get zero and negative flag
		set_missing_flags(RX);
		//printf("\t pc += %d\n", bytes);
	}
}

void LDY(void){ //LDY... Load Index Y with memory
	if(toSet == DEFS){
                defs = (1 << NF) | (1 << ZF);
                uses = 0;
		if(is_absolute_address(m[pc])){
                        add_used_helper_function(read8);
                }
		usedRegisters = (1 << RY);
		check_for_indexed_addressing(m[pc]);
        }else if(toSet == IR){
		printf("\t //LDY\n");
		call_corresponding_addressingMode(m[pc]);
		if(is_absolute_address(m[pc])){
                        printf("\t __asm__ volatile(\"mov %%0, %%1\" : \"=r\"(ry) : \"r\"((uint8_t)"); 
			if(toSet == DYNAMIC){
				printf("read8(");
				call_corresponding_addressingMode(m[pc]);
                                printf(")));\n");
                                toSet = IR;
			}else{
				printf("%d));\n", m[parameter]);
			}
                }else{
                        printf("\t __asm__ volatile(\"mov %%0, %%1\" : \"=r\"(ry) : \"r\"((uint8_t)%d));\n", parameter);

                }
		//to get zero and negative flag
		set_missing_flags(RY);
		//printf("\t pc += %d\n", bytes);

	}
}

void LSR(void){ //Shift One Bit Right (Memory or accumulator)
	if(toSet == DEFS){
		uses = 0;
		defs = (1 << NF) | (1 << ZF);
		if(is_absolute_address(m[pc])){
                        add_used_helper_function(read8);
                }else{
			usedRegisters = (1 << RA);
		}
		check_for_indexed_addressing(m[pc]);
        }else if(toSet == IR){
		printf("\t //LSR\n");
		call_corresponding_addressingMode(m[pc]);
		if(!is_absolute_address(m[pc])){
			if(!optimization || (uses & (1 <<  VF))){
        	                save_VF();
	                }
			printf("\t __asm__ volatile(\"lsr %%0\" : \"=r\"(ra));\n");
			if(!optimization || (uses & (1 <<  VF))){
                        	restore_VF();
			}
		}
		if(is_in_ROM(parameter)){
                	//ROM cannot be modified
                        return;
                }

		if(!optimization || (defs & (1 << CF))){        
			printf("\t temp = ");
                        printf("read8(");
                        call_corresponding_addressingMode(m[pc]);
                        printf(") & 0x1;\n");
			printf("\t if(temp){\n");
			printf("\t\t __asm__ volatile(\"sec\");\n");
			printf("\t }else{\n");
			printf("\t\t __asm__ volatile(\"clc\");\n");
			printf("\t }\n");
		}

		print_ram_or_rriot();
		printf(" >> 1;\n");
		set_NF_ZF_for_memory();
		toSet = IR;
	}
}

void NOP(void){
	if(toSet == DEFS){
		uses = 0;
		defs = 0;
        }else if(toSet == IR){
		printf("\t //NOP\n");
		if(!optimization){
			printf("\t __asm__ volatile(\"nop\");\n");
		}
		//printf("\t pc += %d\n", bytes);
	}
}

void ORA(void){ //OR Memory with Accumulator 
	if(toSet == DEFS){
		uses = 0;
		defs = (1 << NF) | (1 << ZF);
		if(is_absolute_address(m[pc])){
                        add_used_helper_function(read8);
                }
		usedRegisters = (1 << RA);
		check_for_indexed_addressing(m[pc]);
        }else if(toSet == IR){
		printf("\t //ORA\n");
		call_corresponding_addressingMode(m[pc]);

		if(!optimization || (uses & (1 <<  VF))){
                        save_VF();
                }
    
		if(is_absolute_address(m[pc]) == 1){
			printf("\t __asm__ volatile(\"or %%0, %%1\" : \"=r\"(ra) : \"r\"("); 
			if(toSet == DYNAMIC){
                                printf("read8(");
                                call_corresponding_addressingMode(m[pc]);
                                printf(")));\n");
                                toSet = IR;
                        }else{
				printf("%d));\n", m[parameter]);
			}
		}else{
			printf("\t __asm__ volatile(\"or %%0, %%1\" : \"=r\"(ra) : \"r\"(%d));\n", parameter);
		}
		
                if(!optimization || (uses & (1 <<  VF))){
                        restore_VF();
                }

	}
}

void PHA(void){ //PusH Accumulator
	if(toSet == DEFS){
		defs = 0;
		uses = 0;
		add_used_helper_function(push8);
		usedRegisters = (1 << RA) | (1 << RS);
        }else if(toSet == IR){
		printf("\t //PHA\n");
		call_corresponding_addressingMode(m[pc]);
		printf("\t push8(ra);\n");
		//printf("\t pc += %d;\n", bytes);
	}
}

void PHP(void){ //PusH Processor status
        //printf("PHP:\n");
	if(toSet == DEFS){
		defs = 0;
                //uses = (1 << NF) | (1 << VF) | (1 << DF) | (1 << IF) | (1 << ZF) | (1 << CF);
		uses = 0;
		add_used_helper_function(push8);
		usedRegisters = (1 << RS);
        }else if(toSet == IR){
		printf("\t //PHP\n");
		call_corresponding_addressingMode(m[pc]);
                printf("\t push8(SREG);\n");
	}
}

void PLA(void){ //PuLl Accumulator
        //printf("PLA:\n");
	if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF);
                uses = 0;
		add_used_helper_function(pull8);
		usedRegisters = (1 << RA) | (1 << RS);
        }else if(toSet == IR){
		printf("\t //PLA\n");
		printf("\t ra = pull8();\n");
		set_missing_flags(RA);	
	}
}

void PLP(void){ //PuLl Processor status
	if(toSet == DEFS){
		defs = (1 << BF) | (1 << XX);
                uses = 0;
		add_used_helper_function(pull8);
		usedRegisters = (1 << RS);
        }else if(toSet == IR){
		printf("\t //PLP\n");
		call_corresponding_addressingMode(m[pc]);
		printf("\t SREG = pull8();\n");
	        printf("\t pc += %d;\n", bytes);
	}
}

void ROL(void){ //ROL... Rotate one Bit left (memory or accumulator)
	if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF) | (1 << CF);
                uses = (1 << CF);
		if(is_absolute_address(m[pc]) == 1){
			add_used_helper_function(read8);
			add_used_helper_function(write8);
		}else{
			usedRegisters = (1 << RA);
		}
		check_for_indexed_addressing(m[pc]);
        }else if(toSet == IR){
		printf("\t //ROL\n");
		call_corresponding_addressingMode(m[pc]);

		if(is_absolute_address(m[pc]) == 0){
			if(!optimization || (uses & (1 <<  VF))){
                        	save_VF();
                	}
                        printf("\t __asm__ volatile(\"rol %%0\" : \"=r\"(ra));\n");
			if(!optimization || (uses & (1 <<  VF))){
                        	restore_VF();
                	}
                }else{
			if(is_in_ROM(parameter)){
	                        //ROM cannot be modified
        	                return;
                	}

			if(!optimization || (defs & (1 << CF))){
                        	printf("\t temp = (read8(");
                        	call_corresponding_addressingMode(m[pc]);
                        	printf(") & 0x80) >> 7;\n");
			}
                        
			print_ram_or_rriot();
			printf(" << 1 | (SREG & (1 << %d));\n", CF);
			
			if(!optimization || (defs & (1 << CF))){
				printf("\t if(temp){\n");
				printf("\t\t __asm__ volatile(\"sec\");\n");
				printf("\t }else{\n");
				printf("\t\t __asm__ volatile(\"clc\");\n");
				printf("\t }\n");
			}
			set_NF_ZF_for_memory();		
			toSet = IR;
                }
	}
}

void ROR(void){ //ROR... Rotate one Bit right (memory or accumulator)
	if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF) | (1 << CF);
                uses = (1 << CF);
		if(is_absolute_address(m[pc]) == 1){
                        add_used_helper_function(read8);
                        add_used_helper_function(write8);
                }else{
			usedRegisters = (1 << RA);
		}
		check_for_indexed_addressing(m[pc]);
        }else if(toSet == IR){
		printf("\t //ROR\n");
		call_corresponding_addressingMode(m[pc]);

		if(is_absolute_address(m[pc]) == 0){
			if(!optimization || (uses & (1 <<  VF))){
	                        save_VF();
        	        }

			printf("\t __asm__ volatile(\"ror %%0\" : \"=r\"(ra));\n");

			if(!optimization || (uses & (1 <<  VF))){
                        	restore_VF();
                	}
		}else{
			if(is_in_ROM(parameter)){
                	        //ROM cannot be modified
        	                return;
	                }

			if(!optimization || (defs & (1 << CF))){
                                printf("\t temp = read8(");
                                call_corresponding_addressingMode(m[pc]);
                                printf(") & 0x1;\n");
                        }

                        print_ram_or_rriot();
                        printf(" >> 1 | ((SREG & (1 << %d)) << 7);\n", CF);

                        if(!optimization || (defs & (1 << CF))){
                                printf("\t if(temp){\n");
                                printf("\t\t __asm__ volatile(\"sec\");\n");
                                printf("\t }else{\n");
                                printf("\t\t __asm__ volatile(\"clc\");\n");
                                printf("\t }\n");
                        }
                        set_NF_ZF_for_memory();
                        toSet = IR;
			
                }

	}
}

void RTI(void){ //Return from Interrupt
		//TODO shadow stack
	if(toSet == DEFS){
		defs = (1 << BF);
                uses = 0;
		add_used_helper_function(pull8);
        }else if(toSet == IR){
		printf("__asm__ volatile(\"reti\");\n");
		/*printf("\t flags = pull8();\n");
		printf("\t pc = pull8();\n");
        	printf("\t pc |= (pull8() << 8);\n");*/
	}
}

void RTS(void){ // return from subroutine
		// TODO shadow stack - implement labels for start of subroutine
	if(toSet == DEFS){
		defs = 0;
		uses = 0;
		//add_used_helper_function(pull8);
        }else if(toSet == IR){
		printf("\t //RTS\n");
		/*printf("\t pc = pull8();\n");
        	printf("\t pc |= (pull8() << 8);\n");
		printf("\t pc++;\n");*/
		printf("\t __asm__ volatile(\"ret\");\n");
	}
}

void SBC(void){ //SBC... subtract with carry
	if(toSet == DEFS){
		defs = (1 << NF) | (1 << VF) | (1 << ZF) | (1 << CF);
                uses = (1 << CF);
		if(is_absolute_address(m[pc]) == 1){
			add_used_helper_function(read8);
		}
		usedRegisters = (1 << RA);
		check_for_indexed_addressing(m[pc]);
        }else if(toSet == IR){
		printf("\t //SBC\n");
		call_corresponding_addressingMode(m[pc]);
                if(is_absolute_address(m[pc])){
                        printf("\t __asm__ volatile(\"sbc %%0, %%1\" : \"=r\"(ra) : \"r\"((uint8_t)");
			if(toSet == DYNAMIC){
                                printf("read8(");
                                call_corresponding_addressingMode(m[pc]);
                                printf(")));\n");
                                toSet = IR;
                        }else{
				printf(" %d));\n", (uint8_t) m[parameter]);
			}
                }else{
                        printf("\t __asm__ volatile(\"sbc %%0, %%1\" : \"=r\"(ra) : \"r\"((uint8_t) %d));\n\t pc += %d;\n",(uint8_t) parameter, bytes);

                }		
	}
}

void SEC(void){ //SEC... Set Carry Flag
	if(toSet == DEFS){
		defs = (1 << CF);
                uses = 0;
		add_used_helper_function(setflag);
        }else if(toSet == IR){
		call_corresponding_addressingMode(m[pc]);
		printf("\t __asm__ volatile(\"sec\");\n");
		//printf("\t pc += %d;\n", bytes);
	}
}

void SED(void){ //SED... set decimal flag
	if(toSet == DEFS){
		defs = (1 << DF);
                uses = 0;
		add_used_helper_function(convert_number_to_bcd);
		add_used_helper_function(convert_bcd_to_number);
		add_used_helper_function(setflag);
		bcd = 1;
        }else if(toSet == IR){
		//doesn't exist in AVR
		call_corresponding_addressingMode(m[pc]);
		printf("\t setflag(DF,1);\n");
		//printf("\t pc += %d;\n", bytes);
	}
}

void SEI(void){ //Set Interrupt Disable Status
	if(toSet == DEFS){
		defs = (1 << BF);
                uses = 0;
	//	add_used_helper_function(setflag);
        }else if(toSet == IR){
		call_corresponding_addressingMode(m[pc]);
		printf("\t sei();\n");
		//printf("\t pc += %d;\n", bytes);
	}
}



void TAX(void){ // Transfer Accumulator to Index X
	if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF);
                uses = 0;
		usedRegisters = (1 << RA) | (1 << RX);
        }else if(toSet == IR){
		call_corresponding_addressingMode(m[pc]);
                printf("\t __asm__ volatile(\"mov %%0, %%1\" : \"=r\"(rx) : \"r\"(ra));\n");
                set_missing_flags(RA);
                //printf("\t pc += %d;\n", bytes);
	}
}

void TAY(void){ // Transfer Accumulator to Index Y
	if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF);
                uses = 0;
		usedRegisters = (1 << RA) | (1 << RY);
        }else if(toSet == IR){
		call_corresponding_addressingMode(m[pc]);
                printf("\t __asm__ volatile(\"mov %%0, %%1\" : \"=r\"(ry) : \"r\"(ra));\n");
                set_missing_flags(RA);
                //printf("\t pc += %d;\n", bytes);
	}
}

void TSX(void){ //Transfer Stackpointer to X
	if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF);
                uses = 0;
		usedRegisters = (1 << RX) | (1 << RS);
        }else if(toSet == IR){
		call_corresponding_addressingMode(m[pc]);
                printf("\t __asm__ volatile(\"mov %%0, %%1\" : \"=r\"(rx) : \"r\"(rs));\n");
                set_missing_flags(RA);
                //printf("\t pc += %d;\n", bytes);
	}
}

void TXA(void){ // Transfer Index X to  Accumulator
	if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF);
                uses = 0;
		usedRegisters = (1 << RA) | (1 << RX);
        }else if(toSet == IR){
		call_corresponding_addressingMode(m[pc]);
		printf("\t __asm__ volatile(\"mov %%0, %%1\" : \"=r\"(ra) : \"r\"(rx));\n");
		set_missing_flags(RA);
		//printf("\t pc += %d;\n", bytes);
	}
}

void TXS(void){ //Transfer X to Stackpointer
	if(toSet == DEFS){
		defs = 0;
		uses = 0;
		usedRegisters = (1 << RX) | (1 << RS);
        }else if(toSet == IR){
		call_corresponding_addressingMode(m[pc]);
                printf("\t __asm__ volatile(\"mov %%0, %%1\" : \"=r\"(rs) : \"r\"(rx));\n");
                set_missing_flags(RA);
                //printf("\t pc += %d;\n", bytes);
	}
}

void TYA(void){ // Transfer Index Y to  Accumulator
	if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF);
                uses = 0;
		usedRegisters = (1 << RA) | (1 << RY);
        }else if(toSet == IR){
		call_corresponding_addressingMode(m[pc]);
                printf("\t __asm__ volatile(\"mov %%0, %%1\" : \"=r\"(ra) : \"r\"(ry));\n");
                set_missing_flags(RA);
                //pr += %d;\n", bytes);
	}
}

/* "Illegal" Opcodes */

void ALR(void){ //A AND operand + LSR
        //AND operation
	printf("ALR:\n");
        printf("\t r[RA] = r[RA] & parameter;\n");
        printf("\t setflag(ZF, (r[RA] == 0x00));\n");
        printf("\t setflag(NF, (r[RA] & (1 << NF)));\n");

        //LSR
        printf("\t temp = r[RA] >> 1;\n");
        printf("\t setflag(CF, ((r[RA] & 0x0001) == 0x0000));\n");
        printf("\t r[RA] = (uint8_t) (temp && 0x00FF);\n");
	printf("\t goto next_instruction;\n");
}

void ANC(void){ //A AND operand + set C as ASL
        //A AND operand
        printf("ANC:\n");
	printf("\t r[RA] = r[RA] & parameter;\n");
        printf("\t setflag(ZF, (r[RA] == 0x00));\n");
        printf("\t setflag(NF, (r[RA] & (1 << NF)));\n");

        //bit(7) -> C
        printf("\t setflag(CF, ((r[RA] & 0x80) > 0));\n");
	printf("\t goto next_instruction;\n");
}

void ANC2(void){ // A AND operand + set C as ROL
        //A AND operand
        printf("ANC2:\n");
	printf("\t r[RA] = r[RA] & parameter;\n");
        printf("\t setflag(ZF, (r[RA] == 0x00));\n");
        printf("\t setflag(NF, (r[RA] & (1 << NF)));\n");

        //bit(7) -> C
        printf("\t setflag(CF, ((r[RA] & 0x80) > 0));\n");
        //set C as ROL
        printf("\t r[RA] |= (flags & (1 << CF));\n");
	printf("\t goto next_instruction;\n");
}

void ARR(void){ //A AND operand + ROR
        //A AND operand
        printf("ARR:\n");
	printf("\t r[RA] = r[RA] & parameter;\n");
        printf("\t setflag(ZF, (r[RA] == 0x00));\n");
        printf("\t setflag(NF, (r[RA] & (1 << NF)));\n");
        //set V flag
        printf("\t uint16_t temp = (r[RA] & parameter) + parameter;\n");
        printf("\t setflag(ZF, ((temp & 0x80) > 0));\n");
        //exchange bit 7 with carry
        printf("\t r[RA] |= ((flags & (1 << CF)) > 0) << 7;\n");
	printf("\t goto next_instruction;\n");
}

void DCP(void){ //DEC operand + CMP oper
        //DEC operand
        printf("DCR:\n");
	printf("\t write8(parameter, sub(m[parameter], 1));\n");
        printf("\t setflag(ZF, (m[parameter] == 0));\n");
        printf("\t setflag(NF, (r[RA] & (1 << NF)));\n");
        //CMP to Accumulator
        printf("\t setflag(CF, (m[parameter] > r[RA]));\n");
	printf("\t goto next_instruction;\n");
}

void ISC(void){ //INC operand + SBC operand
        //INC operand
        printf("ISC:\n");
	printf("\t write8(parameter, add(m[parameter], 1));\n");
        printf("\t setflag(ZF, (m[parameter] == 0));\n");
        printf("\t setflag(NF, (r[RA] & (1 << NF)));\n");
        // SBC operand
        printf("\t r[RA] = r[RA] - m[parameter] - (flags & (1 << CF));\n");
	printf("\t goto next_instruction;\n");
}

void LAS(void){ //LDA/TSX operand
        //M AND SP
	printf("LAS:\n");
        printf("\t r[RS] = r[RS] & m[parameter];\n");
        //TSX
        printf("\t r[RX] = r[RS];\n");
	printf("\t goto next_instruction;\n");
}

void LAX(void){ //LDA operand + LDX operand
        //LDA
        printf("LAX:\n");
	printf("\t r[RA] = m[parameter];\n");
        //LDX 
        printf("\t r[RX] = m[parameter];\n");
        printf("\t setflag(ZF, (r[RX] == 0x00));\n");
        printf("\t setflag(NF, (r[RX] & (1 << NF)));\n");
	printf("\t goto next_instruction;\n");
}

void RLA(void){ //ROL operand + AND operand
        //M <- ROL
        printf("RLA:\n");
	printf("\t setflag(CF, ((r[parameter] & 0x8) > 0));\n");
        printf("\t temp = (r[parameter] << 1) | (flags & (1 << CF));\n");
        printf("\t write8(parameter, (uint8_t) (temp & 0xFF));\n");
        //A AND M -> A
        printf("\t r[RA] = temp & r[RA];\n");
        printf("\t setflag(ZF, (r[RA] == 0x00));\n");
        printf("\t setflag(NF, (r[RA] & (1 << NF)));\n");
	printf("\t goto next_instruction;\n");
}

void RRA(void){ //ROR operand + ADC Operand
        //M <- ROR
        printf("RRA:\n");
	printf("\t setflag(CF, ((r[parameter] & 0x1) > 0));\n");
        printf("\t temp = (r[parameter] >> 1) | ((flags & (1 << CF)) << 7);\n");
        printf("\t write8(parameter, (uint8_t) (temp & 0xFF));\n");
        //A + M -> A
        printf("\t r[RA] = temp + r[RA];\n");
        printf("\t setflag(ZF, (r[RA] == 0x00));\n");
        printf("\t setflag(NF, (r[RA] & (1 << NF)));\n");
	printf("\t goto next_instruction;\n");
}

void SAX(void){ // A and X are put on bus at the same time -> like A AND X
        printf("SAX:\n");
	printf("\t write8(parameter, (r[RA] & r[RX]));\n");
	printf("\t goto next_instruction;\n");
}

void SBX(void){ //CMP and DEC at onces
        //(A AND X) - operand -> X
        printf("SBX:\n");
	printf("\t r[RX] = (r[RA] & r[RX]) - parameter;\n");
        printf("\t setflag(ZF, (r[RX] == 0x00));\n");
        printf("\t setflag(NF, (r[RX] & (1 << NF)));\n");
        printf("\t setflag(CF, (r[RX] > parameter));\n");
	printf("\t goto next_instruction;\n");
}

void SLO(void){ //ASL operand + ORA operand
        printf("SLO:\n");
	printf("\t temp = (m[parameter] << 1);\n");
        printf("\t write8(parameter, (uint8_t) (temp & 0xFF));\n");
        printf("\t setflag(CF, ((temp & 0x80) > 0));\n");
        printf("\t r[RA] = r[RA] | (uint8_t) (temp & 0xFF);\n");
        printf("\t setflag(ZF, (r[RA] == 0x00));\n");
        printf("\t setflag(NF, (r[RA] & (1 << NF)));\n");
	printf("\t goto next_instruction;\n");
}

void SRE(void){ //LSR operand + EOR operand
        printf("SRE:\n");
	printf("\t temp = (m[parameter] >> 1);\n");
        printf("\t write8(parameter, (uint8_t) (temp & 0xFF));\n");
        printf("\t setflag(CF, ((temp & 0x01) > 0));\n");

        printf("\t r[RA] = r[RA] ^ (uint8_t) (temp & 0xFF);\n");
        printf("\t setflag(ZF, (r[RA] == 0x00));\n");
        printf("\t setflag(NF, (r[RA] & (1 << NF)));\n");
	printf("\t goto next_instruction;\n");
}

void USBC(void){ //SBC + NOP
        printf("USBC:\n");
	printf("\t r[RA] = r[RA] - parameter - (flags & (1 << CF));\n");
        printf("\t setflag(ZF, (r[RA] == 0x00));\n");
        printf("\t setflag(NF, (r[RA] & (1 << NF)));\n");

        //NOP
        printf("\t pc += 2;\n");
	printf("\t goto next_instruction;\n");
}

void JAM(void){ //freeze the CPU with $FF on data bus
     	printf("JAM:\n");
	printf("\t return;\n");
}

/* Instructions */
void STA(void){ //STA... store accumulator in memory
	if(toSet == DEFS){
		defs = 0;
		uses = 0;
		add_used_helper_function(write8);
		if(!((code[m[pc]].addressingMode == 0x5) || (code[m[pc]].addressingMode == 0x0d))){
			add_used_helper_function(read8);
		}
		usedRegisters = (1 << RA);
		check_for_indexed_addressing(m[pc]);
	}else if(toSet == IR){
		printf("\t //STA\n");
		printf("\t temp = SREG;\n");
		call_corresponding_addressingMode(m[pc]);
		if(!((code[m[pc]].addressingMode == 0x5) || (code[m[pc]].addressingMode == 0x0d))){
                        printf("\t read8(");
			if(toSet == DYNAMIC){
				call_corresponding_addressingMode(m[pc]);
				printf(");\n");
			}else{
				printf("%d);\n", parameter & 0xff);
			}
		}
		if(is_in_ROM(parameter)){
                        //ROM cannot be modified
			printf("\t SREG = temp;\n");
			toSet = IR;
                        return;
               	}

		printf("\t write8(");
                if(toSet == DYNAMIC){
                	call_corresponding_addressingMode(m[pc]);
                        printf(", ra);\n");
                        toSet = IR;
		}else{
                	printf("%d, ra);\n", parameter);
                }
		printf("\t SREG = temp;\n");

                /*if(!((code[m[pc]].addressingMode == 0x5) || (code[m[pc]].addressingMode == 0x0d))){
                        printf("\t read8(%d);\n", parameter & 0xF);
                }
                printf("\t write8(");
                if((code[m[pc]].addressingMode == 0x5) || (code[m[pc]].addressingMode == 0x0d)){
                        printf("%d, ra);\n", parameter);
                }else{
			call_corresponding_addressingMode(m[pc]);
                        printf(", ra));\n");
                        toSet = IR;

                        if((m[pc] == 0x99) || (m[pc] == 0x91)){
                                //ry added
                                printf("%d+ry, ra);\n", parameter);
                        }else if(m[pc] == 0x81){
                                //indexed_x needs extra treatment - dynamic TODO
                                printf("%d, ra);\n", parameter);
                        }else{
                                //rx added
                                printf("%d+rx, ra);\n", parameter);
                        }
                }*/

		//printf("\t pc += %d;\n", bytes);
	}
}

void STX(void){ //STA... store Index X in memory
        if(toSet == DEFS){
                defs = 0;
                uses = 0;
		add_used_helper_function(write8);
                if(!((code[m[pc]].addressingMode == 0x5) || (code[m[pc]].addressingMode == 0x0d))){
                        add_used_helper_function(read8);
                }
		usedRegisters = (1 << RX);
		check_for_indexed_addressing(m[pc]);
	}else if(toSet == IR){
		printf("\t //STX\n");
		printf("\t temp = SREG;\n");
		call_corresponding_addressingMode(m[pc]);
		if(!((code[m[pc]].addressingMode == 0x5) || (code[m[pc]].addressingMode == 0x0d))){
                        printf("\t read8("); 
			if(toSet == DYNAMIC){
				call_corresponding_addressingMode(m[pc]);
			}else{
				printf("%d);\n", parameter & 0xff);
			}
		}
		if(is_in_ROM(parameter)){
                        //ROM cannot be modified
                        toSet = IR;
			printf("\t SREG = temp;\n");
                        return;
                }
                
		printf("\t write8(");
                if(toSet == DYNAMIC){
                	call_corresponding_addressingMode(m[pc]);
                        printf(", rx);\n");
                        toSet = IR;
                }else{
                        printf("%d, rx);\n", parameter);
                }
		printf("\t SREG = temp;\n");
	}
}

void STY(void){ //STA... store Index Y in memory
        if(toSet == DEFS){
                defs = 0;
                uses = 0;
		add_used_helper_function(write8);
                if(!((code[m[pc]].addressingMode == 0x5) || (code[m[pc]].addressingMode == 0x0d))){
                        add_used_helper_function(read8);
                }
		usedRegisters = (1 << RY);
		check_for_indexed_addressing(m[pc]);
        }else if(toSet == IR){
		printf("\t //STY\n");
		printf("\t temp = SREG;\n");
		call_corresponding_addressingMode(m[pc]);
		if(!((code[m[pc]].addressingMode == 0x5) || (code[m[pc]].addressingMode == 0x0d))){
                        printf("\t read8("); 
			if(toSet == DYNAMIC){
				call_corresponding_addressingMode(m[pc]);
				printf(");\n");
			}else{
				printf("%d);\n", parameter & 0xff);
			}
		}
		if(is_in_ROM(parameter)){
                        //ROM cannot be modified
                       	printf("\t SREG = temp;\n");
		       	toSet = IR;
                        return;
                }

              	printf("\t write8(");
                if(toSet == DYNAMIC){
                	call_corresponding_addressingMode(m[pc]);
                        printf(", ry);\n");
                        toSet = IR;
                }else{
                        printf("%d, ry);\n", parameter);
                
                }
		printf("\t SREG = temp;\n");
	}
}

/* Addressing Modes */
void
add_rom_address(uint16_t address){
	for(int i = 0; i < rom_addr; i++){
		if(rom_addresses[i] == address){
			return;
		}
	}
	rom_addresses[rom_addr] = address;
	rom_addr++;
	return;
}

void
add_rriot_address(uint16_t address){
	for(int i = 0; i < rriot_addr; i++){
        	if(rriot_rom[i] == address){
              		return;
                }
        }
        
        rriot_rom[rriot_addr] = address;
        rriot_addr++;
        return;
}

void
add_rom_range_to_rom_addresses(uint16_t address){
	//add range accessible with 8-Bit Register (possible values: -128 to 127)
	uint16_t start;
       	uint16_t lowest = address + 0xff80;
	uint16_t end;
       	uint32_t highest = address + 0x007f;
	
        if(highest > 0xffff){
                end = 0xffff;
        }else{
                end = highest & 0xffff;
        }
	start = 0xf000;
        if((lowest & 0xF000) == 0xF000){
		//no overflow
                start = lowest;
	}
	
	for(uint16_t i = start; i <= end; i++){
		if((i >= start) && (i <= end)){
			add_rom_address(i);
		}
	}
}

void
add_rriot_range_to_rom_addresses(uint16_t address){
	uint16_t start;
        uint16_t lowest = address + 0xff80;
        uint16_t end;
        uint32_t highest = address + 0x007f;
	//address in RRIOT ROM
	if(highest > 0x8fff){
        	end = 0x8fff;
        }else{
                end = highest & 0xffff;
        }
        start = 0x8c00;
       	if((lowest & 0x8000) == 0x8000){
                start = RRIOT_ROM_START;
        }else{
                start = lowest;
        }

	for(uint16_t i = start; i <= end; i++){
                if((i >= start) && (i <= end)){
                        add_rriot_address(i);
                }
        }

}

void absolute(void){
	if(toSet == BYTES){
		bytes = 3;
		parameter = m[pc + 1] | ((uint16_t) m[pc + 2] << 8);
	}else if(toSet == DYNAMIC){
		printf("%d", parameter);
	}else{
		parameter = m[pc + 1] | ((uint16_t) m[pc + 2] << 8);
                bytes = 3;
                if(((parameter >= RRIOT_RAM_START) && (parameter < RRIOT_ROM_START)) || (parameter <= 0xff)){
                        toSet = DYNAMIC;
                }
	}
}

void absolute_x (void){ //address is address incremented with X (with carry)
	static int visits = 0;
	if(toSet == BYTES){
		bytes = 3;
#if WCET
		cycles++;
#endif
		if(visits == 0){
			parameter = m[pc + 1] | ((uint16_t) m[pc + 2] << 8);
			//save rom range for dynamic access
			if(parameter >= 0xf000){
                        	add_rom_range_to_rom_addresses(parameter);
                	}else{
                        	add_rriot_range_to_rom_addresses(parameter);
                	}
		}

	}else if(toSet == DYNAMIC){
		printf("%d + rx", parameter);
	}else{
		toSet = DYNAMIC;
		parameter = m[pc + 1] | ((uint16_t) m[pc + 2] << 8);
		bytes = 3;
	}
	visits++;
}

void absolute_y(void){ //address is address incremented with Y (with carry)
	static int visits = 0;
        if(toSet == BYTES){
		bytes = 3;
#if WCET
		cycles++;
#endif
		if(visits == 0){
			parameter = m[pc+1] | ((uint16_t) m[pc + 2] << 8);
			//save rom range for dynamic access
                	if(parameter >= 0xf000){
                        	add_rom_range_to_rom_addresses(parameter);
                	}else{
                        	add_rriot_range_to_rom_addresses(parameter);
                	}
		}

	}else if(toSet == DYNAMIC){
		printf("%d + ry", parameter);
	}else{
		toSet = DYNAMIC;
		parameter = m[pc+1] | ((uint16_t) m[pc + 2] << 8);
		bytes = 3;
	}
	visits++;
}

void immediate(void){
	if(toSet == BYTES){
		bytes = 2;
	}else{
		bytes = 2;
		parameter = m[pc+1];
	}
}

void implied(void){
	bytes = 1;
}

void indirect(void){
	if(toSet == BYTES){
		bytes = 3;
	}else{
		bytes = 3;
        	uint16_t vector = m[pc+1] | ((uint16_t) m[pc+2] << 8);
		if(is_in_ROM(vector)){
			parameter = m[vector] | ((uint16_t) m[vector + 1] << 8);
			if(!is_in_ROM(parameter)){
				printf("jump address is in RAM!!!\n");
			       	dynamic = TRUE;
			}else{
				//jump target is static
				dynamic = FALSE;
			}
		}else{
			printf("jump address is in RAM!!!\n");
			dynamic = TRUE;
		}
	}
	
}

void indirect_y(void){
	if(toSet == BYTES){
#if WCET
		cycles++;
#endif
		bytes = 2;
	}else if(toSet == DYNAMIC){
		//address = m[pc+1];
        	//parameter = m[address] | (m[address+1] << 8) + ry;
		printf("(read8(%d) | ((uint16_t) (read8(%d) + 1) << 8)) + ry)", parameter, parameter);
	}else{
		parameter = m[pc+1];
		toSet = DYNAMIC;
		bytes = 2;
	}
}

void indexed_x(void){ //pointer is modified with x
	if(toSet == BYTES){
        	bytes = 2;
	}else if(toSet == DYNAMIC){
		// address = (m[pc+1] + r[RX]) & 0x00FF;
        	// parameter = m[address] | (m[address+1] << 8);
		printf("(read8(%d) + rx) | (((uint16_t) (read8(%d) + rx) + 1) << 8)", parameter, parameter);
	}else{
		parameter = m[pc+1];
		toSet = DYNAMIC;
		bytes = 2;
	}
}

void relative(void){
	parameter = m[pc + 1];
	bytes = 2;
	if((parameter & 0x80) == 0x80){
		parameter = ((uint16_t) 0xff << 8) | parameter;
	}
	parameter = (pc + 2) + parameter;
}

void zeropage(void){ //hi-byte is 0x00
        if(toSet == BYTES){
                bytes = 2;
        }else if(toSet == DYNAMIC){
                printf("%d", parameter);
        }else{
		parameter = m[pc + 1];
                bytes = 2;
                toSet = DYNAMIC;
	}
}

void zeropage_x(void){ //address is address incremented with x (without carry)
	if(toSet == BYTES){
		bytes = 2;
	}else if(toSet == DYNAMIC){
		printf("%d + rx", parameter);
	}else{
		//is always in RAM because Zeropage is in RAM
		toSet = DYNAMIC;
		parameter = m[pc + 1];
	}
}

void zeropage_y(void){ //address is address incremented with y (without carry)
        if(toSet == BYTES){
                bytes = 2;
        }else if(toSet == DYNAMIC){
                printf("%d + ry", parameter);
        }else{
                //is always in RAM because Zeropage is in RAM
                toSet = DYNAMIC;
                parameter = m[pc + 1];
        }
}


