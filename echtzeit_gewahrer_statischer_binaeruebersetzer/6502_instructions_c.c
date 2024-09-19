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
enum { RA=0, RY, RX, RS, TEMP};

/* Communication with Translator*/
enum{DEFS, IR, BYTES, DYNAMIC};
uint8_t bytes;

extern int toSet;
extern uint8_t defs;
extern uint8_t uses;
extern uint8_t usedRegisters;
extern uint8_t optimization;
extern uint16_t cycles;

extern uint8_t bcd;
/* communication for printing */
uint16_t rriot_rom[1024];
uint16_t rriot_addr;
uint16_t rom_addresses[4096];
uint16_t rom_addr;

uint16_t jsr[2048];
uint16_t jsr_counter;

extern void (*used_helper_functions[9])(void);
int helperFunctions = 0;

/* Helper Functions */
uint16_t parameter; //set by addressing mode

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
        printf("void\nset_up_timer(uint32_t start){\n");
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
        printf("uint16_t\ncalculate_time(uint32_t counter){\n");
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
print_jump_table_jsr(void){
	if(jsr_counter > 0){
		printf("\t uint16_t address = 0;\n");
		printf("jump_table:\n");
		printf("\t switch(address){\n");
		for(int i = 0; i < jsr_counter; i++){
			//JSR instruction has 3 Bytes
			printf("\t\t case %d:\n", jsr[i]);
			printf("\t\t\t goto L%x;\n", jsr[i] + 3);
		}
		printf("\t };\n");
	}
}

void
print_global_vars_and_functions(void){
        if(optimization){
		if(usedRegisters & (1 << RA)){
			printf("uint8_t ra;\n");
		}
		if(usedRegisters & (1 << RX)){
        		printf("uint8_t rx;\n");
		}
		if(usedRegisters & (1 << RY)){
        		printf("uint8_t ry;\n");
		}
		if(usedRegisters & (1 << RS)){
        		printf("uint8_t rs;\n\n");
		}
	}else{
		printf("uint8_t ra;\n");
		printf("uint8_t rx;\n");
		printf("uint8_t ry;\n");
		printf("uint8_t rs;\n\n");
	}

        printf("volatile uint8_t wait;\n");
        printf("volatile uint8_t ioValue;\n\n");
        printf("volatile uint16_t overflow;\n");
	printf("volatile uint8_t io;\n");

	printf("uint8_t flags;\n");
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
	print_jump_table_jsr();
        printf("\t set_up_timer(0);\n");
	printf("\t sei();\n");
	if(usedRegisters & (1 << 4)){
		printf("\t uint16_t temp = 0;\n");
	}
        printf("\t cycles = 0;\n");
	printf("\t flags = 0;\n");
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
	printf("}");
}


/* Code for printing flags */

void
code_for_NF_flag(int reg){
	if(reg == RA){
		fprintf(stdout, "\t setflag(%d, ((ra & 0x80) > 0));\n", NF);
	}else if(reg == RX){
		fprintf(stdout, "\t setflag(%d, ((rx & 0x80) > 0));\n", NF);
	}else if(reg == RY){
		fprintf(stdout, "\t setflag(%d, ((ry & 0x80) > 0));\n", NF);
	}
}

void
code_for_set_VF_ADC_SBC(uint16_t parameter){
	printf("\t setflag(%d, ((((temp & 0xFF) ^ ra) & ((temp & 0xFF) ^ %d) & 0x80) > 0));\n", VF, parameter);
}

void
code_for_set_VF_ADC_SBC_dynamic(char *value, char *parameter){
        printf("setflag(VF, ((((%s & 0xFF) ^ ra) & ((%s & 0xFF) ^ %s) & 0x80) > 0)));\n", value, value, parameter);
}

void
code_for_ZF_flag(int reg){
	if(reg == RA){
		fprintf(stdout, "\t setflag(%d, (ra == 0x00));\n", ZF);
	}else if(reg == RX){
		fprintf(stdout, "\t setflag(%d, (rx == 0x00));\n", ZF);
	}else if(reg == RY){
		fprintf(stdout, "\t setflag(%d, (ry == 0x00));\n", ZF);
	}
}

void
code_for_CF_flag(void){
	fprintf(stdout, "\t setflag(%d, (temp > 0xFF));\n", CF); 
}

void
code_for_CF_flag_dynamic(char *value){
	fprintf(stdout, "\t setflag(%d, ((%s > 0xFF));\n", CF, value);
}

void
code_for_ZF_dynamic(uint8_t toSet){
	printf("\t setflag(%d, ", ZF);
        if(toSet == DYNAMIC){
        	printf("read8(");
                call_corresponding_addressingMode(m[pc]);
                printf(")");
        }else{
                printf("%d", m[parameter]);
        }
        printf(" > 0);\n");
}

void
code_for_NF_dynamic(uint8_t toSet){
	printf("\t setflag(%d, ", NF);
        if(toSet == DYNAMIC){
        	printf("read8(");
                call_corresponding_addressingMode(m[pc]);
                printf(")");
        }else{
                printf("%d", m[parameter]);
	}
        printf(" & 0x80);\n");
}

void
set_CF_for_Compare(void){
	printf("\t setflag(0, ra >= ");
        if(toSet == DYNAMIC){
	        printf("read8(");
                call_corresponding_addressingMode(m[pc]);
                printf("));\n");
	}else if(code[m[pc]].addressingMode == 0x9){
		//immediate addressing
		printf("%d);\n", parameter);
	}else{
                printf("%d);\n", m[parameter]);
        }
}

/* possible Helper Functions for Programm Execution */
void
convert_number_to_bcd(void){
	printf("int\nconvert_number_to_bcd(int number){\n");
	printf("\t int bcd_number = 0;\n");
        printf("\t uint8_t shift = 0;\n");
        printf("\t while(number > 0){\n");
        printf("\t\t uint8_t digit = number %% 10;\n");
        printf("\t\t bcd_number |= (digit << shift);\n");
        printf("\t\t number = (number-digit)/10;\n");
        printf("\t\t shift += 4;\n");
        printf("\t }\n");
        printf("\t return bcd_number;\n");
	printf("}");
}

void
convert_bcd_to_number(void){
	printf("int\nconvert_bcd_to_number(int bcd_number){\n");
	printf("\t int number = 0;\n");
        printf("\t uint8_t shift = 0;\n");
        printf("\t while (bcd_number > 0){\n");
        printf("\t\t int digit = bcd_number & 0xF;\n");
        printf("\t\t for(int i = 0; i < shift; i++){\n");
        printf("\t\t\t digit = digit * 10;\n");
        printf("\t\t }\n");
        printf("\t\t number += digit;\n");
        printf("\t\t bcd_number = (bcd_number >> 4);\n");
        printf("\t\t shift++;\n");
        printf("\t }\n");
        printf("\t return number;\n");
	printf("}");
}

void
add(void){
	printf("int\nadd(uint8_t number, uint8_t add_term){\n");
	printf("\t if((flags & (1 << %d)) > 0){\n", DF);
        printf("\t\t number = convert_bcd_to_number(number);\n");
        printf("\t\t number += convert_bcd_to_number(add_term);\n");
        printf("\t\t return convert_number_to_bcd(number);\n");
        printf("\t }else{\n");
        printf("\t\t return number + add_term;\n");
        printf("\t }\n");
	printf("}");
}

void
sub(void){
        printf("int\nsub(uint8_t number, uint8_t add_term){\n");
	printf("\t if((flags & (1 << %d)) > 0){\n", DF);
        printf("\t\t number = convert_bcd_to_number(number);\n");
        printf("\t\t number -= convert_bcd_to_number(add_term);\n");
        printf("\t\t return convert_number_to_bcd(number);\n");
        printf("\t }else{\n");
        printf("\t\t return number - add_term;\n");
        printf("\t }\n");
        printf("}");
}

void
setflag(void){
	printf("void\nsetflag(int flag, uint8_t value){\n");
        printf("\t if(value == 0){\n");
        printf("\t\t flags &= ~(1 << flag);\n");
        printf("\t }else{\n");
        printf("\t\t flags |= (1 << flag);\n");
        printf("\t }\n");
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
	printf("}\n");
}

void
push8(void){
	printf("void\npush8(uint8_t value){\n");
        printf("\t write8((0x0100 + rs), value);\n");
        printf("\t rs--;\n");
	printf("}\n");
}

/* Instructions */

void ADC(void){ //ADC... add memory to accumulator with carry
        if(toSet == DEFS){
		defs = (1 << NF) | (1 << VF) | (1 << ZF) | (1 << CF);
		uses = (1 << CF);
		add_used_helper_function(setflag);
		usedRegisters = (1 << RA)|(1 << TEMP);
		check_for_indexed_addressing(m[pc]);
	}else if(toSet == IR){
		printf("\t //ADC\n");
		call_corresponding_addressingMode(m[pc]);
		if(bcd){
			if(is_absolute_address(m[pc]) == 0){
                                printf("\t temp = add(ra, %d);\n", parameter);
                        }else{
				printf("\t uint16_t temp = add(r[RA],"); 
				if(toSet == DYNAMIC){
                                	printf("read8(");
                                	call_corresponding_addressingMode(m[pc]);
                                	printf(")");
                        	}else{
                                	printf("%d", m[parameter]);
                        	}
				printf(") + (flags & (1<<%d));\n", CF);
			}
		}else{
			if(is_absolute_address(m[pc])){
				printf("\t temp = ra + ");
				if(toSet == DYNAMIC){
					printf("read8(");
                                	call_corresponding_addressingMode(m[pc]);
                                	printf(")");
                                	toSet = IR;
                        	}else{
					printf("%d", m[parameter]);
				}
				printf(" + (flags & (1<<%d));\n", CF);
				
			}else{
				printf("\t temp = ra + %d + (flags & (1<<%d));\n", parameter, CF);
			}
		}
		if(optimization){
			if(defs & (1 << VF)){
				code_for_set_VF_ADC_SBC(parameter);
			}
			if(defs & (1 << CF)){
				code_for_CF_flag();
			}
			if(defs & (1 << ZF)){ 
				code_for_ZF_flag(RA);
			}
			if(defs & (1 << NF)){
				code_for_NF_flag(RA);
			}
		}else{
			code_for_set_VF_ADC_SBC(parameter);
                	code_for_CF_flag();
                	code_for_ZF_flag(RA);
                	code_for_NF_flag(RA);
		}
		printf("\t ra = (uint8_t) temp & 0xFF;\n");
	}
}

void AND(void){ //AND Memory with accumulator
        if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF); 
		uses = 0;
		add_used_helper_function(read8);
		add_used_helper_function(setflag);
		usedRegisters = (1 << RA);
		check_for_indexed_addressing(m[pc]);
	}else if(toSet == IR){
		printf("\t //AND\n");
		call_corresponding_addressingMode(m[pc]);
		if(is_absolute_address(m[pc])){
			printf("\t ra = ra & "); 
			if(toSet == DYNAMIC){
				printf("read8(");
                                call_corresponding_addressingMode(m[pc]);
                                printf(");\n");
                                toSet = IR;
                        }else{
				printf("%d;\n", m[parameter]);
			}
		}else{
			printf("\t ra = ra & %d;\n", parameter);
		}
		
		if(optimization){
			if(defs & (1 << NF)){
				code_for_NF_flag(RA);
			}
			if(defs & (1 << ZF)){
				code_for_ZF_flag(RA);
			}
		}else{
			code_for_ZF_flag(RA);
                	code_for_NF_flag(RA);
		}
	}
}

void ASL(void){ //Shift Left One Bit (Memory or Accumulator)
	if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF) | (1 << CF);
                uses = 0; 
		if(m[pc] == 0x0a){
			usedRegisters = (1 << RA) | (1 << TEMP);
		}else{
			add_used_helper_function(read8);
			usedRegisters = (1 << TEMP);
		}
		add_used_helper_function(setflag);
		check_for_indexed_addressing(m[pc]);
        }else if(toSet == IR){
		printf("\t //ASL\n");
		call_corresponding_addressingMode(m[pc]);
		if(is_absolute_address(m[pc])){
			if(is_in_ROM(parameter)){
                                //ROM cannot be modified
                                return;
                        }
			printf("\t temp = (uint16_t) ("); 
			if(toSet == DYNAMIC){
				printf("read8(");
                                call_corresponding_addressingMode(m[pc]);
                                printf("))");
                        }else{
				printf("%d)", m[parameter]);
			}
			printf(" << 1;\n");
			printf("\t write8("); 
			if(toSet == DYNAMIC){
                                call_corresponding_addressingMode(m[pc]);
                                printf(", ");
                        }else{
				printf("%d,", parameter); 
			}
			printf(" (uint8_t) (temp & 0x00FF));\n");

			if(optimization){
				if(defs & (1 << ZF)){
                               		code_for_ZF_dynamic(toSet);
                        	}
                        	if(defs & (1 << NF)){
                                	code_for_NF_dynamic(toSet);
                        	}
			}else{
				code_for_ZF_dynamic(toSet);
				code_for_NF_dynamic(toSet);
			}

		}else{
			printf("\t temp = (uint16_t) %d << 1;\n", parameter);
			printf("\t ra = (uint8_t) (temp & 0x00FF);\n");
			if(optimization){
				if(defs & (1 << ZF)){
	                        	code_for_ZF_flag(RA);
        	        	}
               			if(defs & (1 << NF)){
                        		code_for_NF_flag(RA);
                		}
			}else{
				code_for_ZF_flag(RA);
                		code_for_NF_flag(RA);
			}
		}
	}
}

void BCC(void){ //BCC... branch on carry clean (CF == 0)
        if(toSet == DEFS){
		defs = 0;
		uses = (1 << CF);
	}else if(toSet == IR){
		printf("\t //BCC\n");
		call_corresponding_addressingMode(m[pc]);
		printf("\t if((flags & (1 << %d)) == 0){ ", CF);
		printf("cycles += %d;", cycles); 
		printf("goto L%x;}\n", parameter);
		printf("\t cycles += %d;\n", cycles);
	}
}

void BCS(void){ //BCS... branch on carry set (CF == 1)
	if(toSet == DEFS){
		defs = 0;
		uses = (1 << CF);
        }else if(toSet == IR){
		printf("\t //BCS\n");
		call_corresponding_addressingMode(m[pc]);
		printf("\t if((flags & (1 << %d)) == (1 << %d)){ ", CF, CF);
		printf("cycles += %d; ", cycles);
		printf("goto L%x;}\n", parameter);
		printf("\t cycles += %d;\n", cycles);
	}
}

void BEQ(void){ //Branch on result zero (ZF == 1)
	if(toSet == DEFS){
		defs = 0;
		uses = (1 << ZF);
        }else if(toSet == IR){
		printf("\t //BEQ\n");
		call_corresponding_addressingMode(m[pc]);
		printf("\t if((flags & (1 << %d)) == (1 << %d)){ ", ZF, ZF);
		printf("cycles += %d; ", cycles);
		printf("goto L%x;}\n", parameter);
		printf("\t cycles += %d;\n", cycles);
	}
}

void BIT(void){ //BIT... Test Bits in Memory with accumulator
	if(toSet == DEFS){
		defs = (1 << NF) | (1 << VF) | (1 << ZF);
		uses = 0;
		usedRegisters = (1 << RA);
		add_used_helper_function(setflag);
        }else if(toSet == IR){
		printf("\t //BIT\n");
		call_corresponding_addressingMode(m[pc]);
		if(optimization){
			if(defs & (1 << ZF)){
				printf("\t setflag(%d, ((", ZF);
				if(toSet == DYNAMIC){
					printf("read8(");
					call_corresponding_addressingMode(m[pc]);
					printf(")");
					toSet = IR;
				}else{
					printf(" %d", m[parameter]); 
				}
				printf(" & ra) == 0x00));\n");
			}
			if(defs & (1 << NF)){
        			printf("\t setflag(%d, ((", NF);
			       	if(toSet == DYNAMIC){
                                        printf("read8(");
                                        call_corresponding_addressingMode(m[pc]);
                                        printf(")");
                                        toSet = IR;
                                }else{
					printf("%d", m[parameter]);
				}
				printf(" & (1 << 7)) > 0));\n");
			}
			if(defs & (1 << VF)){
        			printf("\t setflag(%d, ((", VF);
				if(toSet == DYNAMIC){
					printf("read8(");
					call_corresponding_addressingMode(m[pc]);
                                        printf(")");
                                        toSet = IR;
				}else{
					printf("%d",m[parameter]);
				}
				printf(" & (1 << VF)) > 0));\n");
			}
		}else{
			printf("\t setflag(%d, ((", ZF); 
			if(toSet == DYNAMIC){
				printf("read8(");
                                call_corresponding_addressingMode(m[pc]);
                                printf(")");
                                toSet = IR;
			}else{
				printf("%d", m[parameter]);
			}
			printf(" & ra) == 0x00));\n");
			printf("\t setflag(%d, ((", NF); 
			if(toSet == DYNAMIC){
				printf("read8(");
                                call_corresponding_addressingMode(m[pc]);
                                printf(")");
                                toSet = IR;
			}else{
				printf("%d", m[parameter]);
			}
			printf(" & (1 << 7)) > 0));\n");
			printf("\t setflag(%d, ((", VF); 
			if(toSet == DYNAMIC){
				printf("read8(");
                                call_corresponding_addressingMode(m[pc]);
                                printf(")");
                                toSet = IR;
			}else{
				printf("%d", m[parameter]);
			}
			printf(" & (1 << 6)) > 0));\n");
		}
	}
}

void BMI(void){ //BMI... Branch on result Minus (NF == 1)
	if(toSet == DEFS){
		defs = 0;
                uses = (1 << NF);
        }else if(toSet == IR){
		printf("\t //BMI\n");
		call_corresponding_addressingMode(m[pc]);
		printf("\t if((flags & (1 << %d)) == (1 << %d)){ ", NF, NF);
		printf("cycles += %d; ", cycles);
		printf("goto L%x;}\n", parameter);
		printf("\t cycles += %d;\n", cycles);
	}
}

void BNE(void){ //Branch on result not zero (ZF == 0)
      	if(toSet == DEFS){
		defs = 0;
		uses = (1 << ZF);
	}else if(toSet == IR){
		printf("\t //BNE\n");
		call_corresponding_addressingMode(m[pc]);
		printf("\t if((flags & (1 << %d)) == 0){ ", ZF);
		printf("cycles += %d; ", cycles);
		printf("goto L%x;}\n", parameter);
		printf("\t cycles += %d;\n", cycles);
	}
}

void BPL(void){ //BPL... Branch on Result Plus (NF == 0) 
	if(toSet == DEFS){
		defs = 0;
		uses = (1 << NF);
        }else if(toSet == IR){
		printf("\t //BPL\n");
		call_corresponding_addressingMode(m[pc]);
		printf("\t if((flags & (1 << %d)) == 0){ ", NF);
		printf("cycles += %d; ", cycles);
		printf("goto L%x;}\n", parameter);
		printf("\t cycles += %d;\n", cycles);
	}
}

void BRK(void){ //BRK is a software interrupt
	if(toSet == DEFS){
		uses = 0;
		defs = (1 << BF)|(1 << XX)|(1 << IF);
		add_used_helper_function(setflag);
		//add_used_helper_function(push8);
        }else if(toSet == IR){
		printf("\t return 0;\n");
	}
}

void BVC(void){ //BVC... Branch on Overflow Clear (VF == 0)
	if(toSet == DEFS){
		uses = (1 << VF);
		defs = 0;
        }else if(toSet == IR){
		printf("\t //BVC\n");
		call_corresponding_addressingMode(m[pc]);
		printf("\t if((flags & (1 << %d)) == 0){ ", VF);
		printf("cycles += %d; ", cycles);
		printf("goto L%x;}\n", parameter);
		printf("\t cycles += %d;\n", cycles);
	}
}

void BVS(void){ //BVS... Branch on Overflow Set (VF == 1)
	if(toSet == DEFS){
		uses = (1 << VF);
		defs = 0;
        }else if(toSet == IR){
		printf("\t //BVS\n");
		call_corresponding_addressingMode(m[pc]);
		printf("\t if((flags & (1 << %d)) == (1 << %d)){ ", VF, VF);
		printf("cycles += %d; ", cycles);
		printf("goto L%x;}\n", parameter);
		printf("\t cycles += %d;\n", cycles);
	}
}

void CLC(void){ //CLC... clear carry flag
	if(toSet == DEFS){
		defs = (1 << CF);
		uses = 0;
		add_used_helper_function(setflag);
	}else if(toSet == IR){
        	printf("\t setflag(%d, 0);\n", CF);
	}
}

void CLD(void){ //CLD... clear decimal mode
	if(toSet == DEFS){
		defs = (1 << DF);
		uses = 0;
		add_used_helper_function(setflag);
        }else if(toSet == IR){
		printf("\t setflag(%d, 0);\n", DF);
	}
}

void CLI(void){ //Clear Interrupt Disable Bit
	if(toSet == DEFS){
		defs = (1 << IF);
		uses = 0;
		add_used_helper_function(setflag);
        }else if(toSet == IR){
		printf("\t setflag(%d, 0);\n", IF);
	}
}

void CLV(void){ //CLV... clear overflow flag
	if(toSet == DEFS){
		uses = 0;
		defs = (1 << VF);
		add_used_helper_function(setflag);
        }else if(toSet == IR){
		printf("\t setflag(%d, 0);\n", VF);
	}
}

void CMP(void){ //CMP... compare memory with accumulator
        if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF) | (1 << CF);
	       	uses = 0;
		if(is_absolute_address(m[pc])){
			add_used_helper_function(read8);
		}
		add_used_helper_function(setflag);
		usedRegisters = (1 << RA) | (1 << TEMP);	
		check_for_indexed_addressing(m[pc]);
	}else if(toSet == IR){
		printf("\t //CMP\n");
		call_corresponding_addressingMode(m[pc]);
		if(is_absolute_address(m[pc])){
			printf("\t temp = ra - ");
			if(toSet == DYNAMIC){
				printf("read8(");
                                call_corresponding_addressingMode(m[pc]);
                                printf(");\n");
                        }else{
				printf("%d;\n", m[parameter]);
			}
		}else{
			printf("\t temp = ra - %d;\n", parameter);
		}
		if(optimization){
			if(defs & (1 << CF)){
				set_CF_for_Compare();
				toSet = IR;
			}
                	if(defs & (1 << ZF)){
				code_for_ZF_flag(RA);
			}
			if(defs & (1 << NF)){
                		code_for_NF_flag(RA);
			}
		}else{
			set_CF_for_Compare();
                        toSet = IR;
			code_for_ZF_flag(RA);
			code_for_NF_flag(RA);
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
		add_used_helper_function(setflag);
		usedRegisters = (1 << RX) | (1 << TEMP);
		check_for_indexed_addressing(m[pc]);
        }else if(toSet == IR){
		printf("\t //CPX\n");
		call_corresponding_addressingMode(m[pc]);
		if(is_absolute_address(m[pc])){
                        printf("\t temp = rx - ");
			if(toSet == DYNAMIC){
				printf("read8(");
                                call_corresponding_addressingMode(m[pc]);
                                printf(");\n");
                        }else{
				printf("%d;\n", m[parameter]);
			}

                }else{
                        printf("\t temp = rx - %d;\n", parameter);
                }
		if(optimization){
			if(defs & (1 << CF)){
                        	set_CF_for_Compare();
                                toSet = IR;
                	}
                	if(defs & (1 << ZF)){
                        	code_for_ZF_flag(RX);
                	}
                	if(defs & (1 << NF)){
                        	code_for_NF_flag(RX);
                	}
		}else{
			set_CF_for_Compare();
                        toSet = IR;
                        code_for_ZF_flag(RX);
                        code_for_NF_flag(RX);
		}


	}
}

void CPY(void){ //CPY... compare Memory with Index Y
	if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF) | (1 << CF);
                uses = 0;
		if(is_absolute_address(m[pc])){
                        add_used_helper_function(read8);
                }
		add_used_helper_function(setflag);
		usedRegisters = (1 << RY) | (1 << TEMP);
		check_for_indexed_addressing(m[pc]);
        }else if(toSet == IR){
		printf("\t //CPY\n");
		call_corresponding_addressingMode(m[pc]);
		if(is_absolute_address(m[pc])){
                        printf("\t temp = ry - ");
			if(toSet == DYNAMIC){
				printf("read8(");
                                call_corresponding_addressingMode(m[pc]);
                                printf(");\n");
                        }else{
				printf("%d;\n", m[parameter]);
			}
                }else{
                        printf("\t temp = ry - %d;\n", parameter);
                }
		if(optimization){
			if(defs & (1 << CF)){
                        	set_CF_for_Compare();
                                toSet = IR;
                	}
                	if(defs & (1 << ZF)){
                        	code_for_ZF_flag(RY);
                	}
                	if(defs & (1 << NF)){
                        	code_for_NF_flag(RY);
                	}
		}else{
			set_CF_for_Compare();
                        toSet = IR;
                        code_for_ZF_flag(RY);
                        code_for_NF_flag(RY);
		}


	}
}

void DEC(void){ //Decrement Memory by one
	if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF);
                uses = 0;
		add_used_helper_function(write8);
		add_used_helper_function(setflag);
		check_for_indexed_addressing(m[pc]);
        }else if(toSet == IR){
		printf("\t //DEC\n");
		call_corresponding_addressingMode(m[pc]);
		if(is_in_ROM(parameter)){
                        //ROM cannot be modified
                        return;
                }
		if(bcd){
			printf("\t write8("); 
			if(toSet == DYNAMIC){
				call_corresponding_addressingMode(m[pc]);
				printf(", sub(m["); 
				call_corresponding_addressingMode(m[pc]);
				printf("], 1));\n");
				toSet = IR;
			}else{
				printf(", sub(m[%d], 1));", parameter);
			}
		}else{
			printf("\t write8(");
			if(toSet == DYNAMIC){
				call_corresponding_addressingMode(m[pc]);
                                printf(", read8(");
				call_corresponding_addressingMode(m[pc]);
				printf(") - 1);\n");
                        }else{
				printf("%d, %d - 1);\n", parameter, m[parameter]);
			}
		}
		if(optimization){	
                	if(defs & (1 << ZF)){
                        	code_for_ZF_dynamic(toSet);
                	}
                	if(defs & (1 << NF)){
                                code_for_NF_dynamic(toSet);
                	}
			toSet = IR;
		}else{
			code_for_ZF_dynamic(toSet);
                        code_for_NF_dynamic(toSet);
			toSet = IR;
		}

	}
}

void DEX(void){ //Decrement Index X by one
	if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF);
                uses = 0;
		usedRegisters = (1 << RX);
		add_used_helper_function(setflag);
        }else if(toSet == IR){
		printf("\t //DEX\n");
		if(bcd){
			printf("\t rx = sub(rx, 1);\n");
		}else{
			printf("\t rx = rx - 1;\n");
		}
	
		if(optimization){
			if(defs & (1 << ZF)){
                		code_for_ZF_flag(RX);
        		}
        		if(defs & (1 << NF)){
                		code_for_NF_flag(RX);
			}
		}else{
			code_for_ZF_flag(RX);
			code_for_NF_flag(RX);
		}
        }
}

void DEY(void){ //Decrement Index Y by one
	if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF);
                uses = 0;
		add_used_helper_function(setflag);
		usedRegisters = (1 << RY);
        }else if(toSet == IR){
		printf("\t //DEY\n");
		if(bcd){
			printf("\t ry = sub(ry, 1);\n");
		}else{
			printf("\t ry = ry - 1;\n");
		}
		if(optimization){
			if(defs & (1 << ZF)){
                        	code_for_ZF_flag(RY);
                	}
                	if(defs & (1 << NF)){
                        	code_for_NF_flag(RY);
                	}
		}else{
			code_for_ZF_flag(RY);
			code_for_NF_flag(RY);
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
		add_used_helper_function(setflag);
		usedRegisters = (1 << RA);
		check_for_indexed_addressing(m[pc]);
        }else if(toSet == IR){
		printf("\t //EOR\n");
		call_corresponding_addressingMode(m[pc]);
		if(is_absolute_address(m[pc])){
			printf("\t ra = ra ^ read8(");
			if(toSet == DYNAMIC){
                                call_corresponding_addressingMode(m[pc]);
                                printf(");\n");
                                toSet = IR;
                        }else{
				printf("%d);\n", m[parameter]);
			}
		}else{
			printf("\t ra = ra ^ %d;\n", parameter);
		}
		if(optimization){
			if(defs & (1 << ZF)){
                        	code_for_ZF_flag(RA);
                	}
                	if(defs & (1 << NF)){
                        	code_for_NF_flag(RA);
                	}
		}else{
			code_for_ZF_flag(RA);
			code_for_NF_flag(RA);
		}

	}
}

void INC(void){ //Increment Memory by one
	if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF);
                uses = 0;
		add_used_helper_function(write8);
		add_used_helper_function(setflag);
		check_for_indexed_addressing(m[pc]);
        }else if(toSet == IR){
		printf("\t //INC\n");
		call_corresponding_addressingMode(m[pc]);
		if(is_in_ROM(parameter)){
                        //ROM cannot be modified
                        return;
                }
		if(bcd){
			printf("\t write8(parameter, ");
			if(toSet == DYNAMIC){
				call_corresponding_addressingMode(m[pc]);
                                printf(", add(read8(");
				call_corresponding_addressingMode(m[pc]);
				printf(")");
			}else{
				printf("%d, add(m[%d]", parameter, parameter);
			}
			printf(", 1));\n");
		}else{
			printf("\t write8(");
		       	if(toSet == DYNAMIC){
                                call_corresponding_addressingMode(m[pc]);
                          	printf(", read8(");
				call_corresponding_addressingMode(m[pc]);
				printf(") + 1);\n");
                        }
		}
		if(optimization){
			if(defs & (1 << ZF)){
                        	code_for_ZF_dynamic(toSet);
                	}
                	if(defs & (1 << NF)){
	                        code_for_NF_dynamic(toSet);
                	}
			toSet = IR;
		}else{
			code_for_ZF_dynamic(toSet);
                        code_for_NF_dynamic(toSet);
			toSet = IR;
		}

	}
}

void INX(void){ //Increment Index X by one
        if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF);
		uses = 0;
		usedRegisters = (1 << RX);
		add_used_helper_function(setflag);
	}else if(toSet == IR){
		printf("\t //INX\n");
		if(bcd){
			printf("\t rx = add(rx, 1);\n");
		}else{
			printf("\t rx = rx + 1;\n");
		}
		if(optimization){
			if(defs & (1 << ZF)){
                        	code_for_ZF_flag(RX);
                	}
                	if(defs & (1 << NF)){
                        	code_for_NF_flag(RX);
                	}
		}else{
			code_for_ZF_flag(RX);
			code_for_NF_flag(RX);
		}

	}
}

void INY(void){ //Increment Index Y by one
	if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF);
                uses = 0;
		usedRegisters = (1 << RY);
		add_used_helper_function(setflag);
        }else if(toSet == IR){
		printf("\t //INY\n");
		if(bcd){
			printf("\t ry = add(ry, 1);\n");
		}else{
			printf("\t ry = ry + 1;\n");
		}
		if(optimization){
			if(defs & (1 << ZF)){
                        	code_for_ZF_flag(RA);
                	}
                	if(defs & (1 << NF)){
                        	code_for_NF_flag(RA);
                	}
		}else{
			code_for_ZF_flag(RA);
			code_for_NF_flag(RA);
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
		if(toSet == DYNAMIC){
			exit(1);
		}else{
			printf("\t cycles += %d;\n", cycles);
	                printf("\t goto L%x;\n", parameter);
		}
	}
}

void JSR(void){ //Jump to subroutine
	if(toSet == DEFS){
		static int counter = 0;
		defs = 0;
                uses = 0;
		//add_used_helper_function(push8);
		if(counter == 0){
			for(int i = 0; i < jsr_counter; i++){
				if(jsr[jsr_counter] == pc){
					return;
				}	
			}
			jsr[jsr_counter] = pc;
			jsr_counter++;
		}
		counter++;
        }else if(toSet == IR){
		//replaced by label stored
		printf("\t //JSR\n");
		call_corresponding_addressingMode(m[pc]);
		printf("\t cycles += %d;\n", cycles);
		printf("\t address = %d;\n", pc);
		printf("\t goto L%x;\n", parameter);
	}
}

void LDA(void){ //LDA... Load Accumulator with memory
	if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF);
		uses = 0;
		if(!(code[m[pc]].addressingMode == 0x9)){
			add_used_helper_function(read8);
		}
		add_used_helper_function(setflag);
		usedRegisters = (1 << RA);
		check_for_indexed_addressing(m[pc]);
	}else if(toSet == IR){
		printf("\t //LDA\n");
		call_corresponding_addressingMode(m[pc]);
		if(is_absolute_address(m[pc])){
			printf("\t ra = ");
			if(toSet == DYNAMIC){
				printf("read8(");
				call_corresponding_addressingMode(m[pc]);
                                printf(") & 0xff;\n");
                                toSet = IR;
			}else{
		       		printf("%d & 0xff;\n", m[parameter]);
			}
		}else{
			printf("\t ra = %d & 0xFF;\n", parameter);
		}
		if(optimization){
			if(defs & (1 << NF)){
				code_for_NF_flag(RA);
			}
			if(defs & (1 << ZF)){
				code_for_ZF_flag(RA);
			}
		}else{
			code_for_NF_flag(RA);
                	code_for_ZF_flag(RA);
		}
		
	}
}

void LDX(void){ //LDX... Load Index X with memory
        if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF);
		uses = 0;
		add_used_helper_function(setflag);
		usedRegisters = (1 << RX);
		check_for_indexed_addressing(m[pc]);
	}else if(toSet == IR){
		printf("\t //LDX\n");
		call_corresponding_addressingMode(m[pc]);
		if(is_absolute_address(m[pc])){
                        printf("\t rx = ");
		       	if(toSet == DYNAMIC){
				printf("read8(");
				call_corresponding_addressingMode(m[pc]);
                                printf(") & 0xff;\n");
                                toSet = IR;
			}else{	
				printf("%d & 0xff;\n", m[parameter]);
			}
                }else{
                        printf("\t rx = %d & 0xff;\n", parameter);
                }
		if(optimization){
			if(defs & (1 << NF)){
                        	code_for_NF_flag(RX);
                	}
                	if(defs & (1 << ZF)){
                        	code_for_ZF_flag(RX);
                	}
		}else{
			code_for_NF_flag(RX);
                        code_for_ZF_flag(RX);
		}

	}
}

void LDY(void){ //LDY... Load Index Y with memory
	if(toSet == DEFS){
                defs = (1 << NF) | (1 << ZF);
                uses = 0;
		if(is_absolute_address(m[pc])){
                        add_used_helper_function(read8);
                }
		add_used_helper_function(setflag);
		usedRegisters = (1 << RY);
		check_for_indexed_addressing(m[pc]);
        }else if(toSet == IR){
		printf("\t //LDY\n");
		call_corresponding_addressingMode(m[pc]);
		if(is_absolute_address(m[pc])){
                        printf("\t ry = ");
			if(toSet == DYNAMIC){
				printf("read8(");
				call_corresponding_addressingMode(m[pc]);
                                printf(") & 0xff;\n");
                                toSet = IR;
			}else{
				printf("%d & 0xff;\n", m[parameter]);
			}
                }else{
                        printf("\t ry = %d & 0xff;\n", parameter);
                }
		if(optimization){
			if(defs & (1 << NF)){
                        	code_for_NF_flag(RY);
                	}
                	if(defs & (1 << ZF)){
                        	code_for_ZF_flag(RY);
                	}
		}else{
			code_for_NF_flag(RY);
                        code_for_ZF_flag(RY);
		}

	}
}

void LSR(void){ //Shift One Bit Right (Memory or accumulator)
	if(toSet == DEFS){
		uses = 0;
		defs = (1 << NF) | (1 << ZF);
		if(m[pc] != 0xa){
                        add_used_helper_function(read8);
			usedRegisters = (1 << 4);
                }else{
			usedRegisters = (1 << RA) | (1 << TEMP);
		}
		add_used_helper_function(setflag);
		check_for_indexed_addressing(m[pc]);
        }else if(toSet == IR){
		printf("\t //LSR\n");
		call_corresponding_addressingMode(m[pc]);
	
		if(is_absolute_address(m[pc]) == 0){
			printf("\t setflag(%d,((ra & 0x1) > 0));\n", CF);
                	printf("\t temp = ra >> 1;\n");
                	printf("\t ra = (uint8_t) (temp & 0x00FF);\n");

			if(optimization){
				if(defs & (1 << NF)){
                                	code_for_NF_flag(RA);
                        	}
                        	if(defs & (1 << ZF)){
                                	code_for_ZF_flag(RA);
                        	}
			}else{
				code_for_NF_flag(RA);
				code_for_ZF_flag(RA);
			}
		}else{
			if(is_in_ROM(parameter)){
                        	//ROM cannot be modified
                        	return;
                	}
			printf("\t temp = "); 
			if(toSet == DYNAMIC){
				printf("read8(");
				call_corresponding_addressingMode(m[pc]);
				printf(")");
			}else{
				printf("%d", m[parameter]);
			}
			printf(" >> 1;\n");
                	printf("\t setflag(%d, ((", CF); 
			if(toSet == DYNAMIC){
				printf("read8(");
				call_corresponding_addressingMode(m[pc]);
				printf(")");
			}else{
				printf("%d", m[parameter]);
			}
			printf(" & 0x1) > 0));\n");
                	printf("\t write8(");
			if(toSet == DYNAMIC){
                                call_corresponding_addressingMode(m[pc]);
                                printf(", ");
                        }else{
				printf("%d, ", parameter);
			}	
			printf("(uint8_t) (temp & 0xFF));\n");

			if(optimization){
				if(defs & (1 << ZF)){
                               		code_for_ZF_dynamic(toSet);
                        	}
                        	if(defs & (1 << NF)){
                                	code_for_NF_dynamic(toSet);
                        	}
			}else{
				code_for_ZF_dynamic(toSet);
                        	code_for_NF_dynamic(toSet);
			}
		}
	}
}

void NOP(void){
	if(toSet == DEFS){
		uses = 0;
		defs = 0;
        }
}

void ORA(void){ //OR Memory with Accumulator 
	if(toSet == DEFS){
		uses = 0;
		defs = (1 << NF) | (1 << ZF);
		if(is_absolute_address(m[pc])){
                        add_used_helper_function(read8);
                }
		add_used_helper_function(setflag);
		usedRegisters = (1 << RA);
		check_for_indexed_addressing(m[pc]);
        }else if(toSet == IR){
		printf("\t //ORA\n");
		call_corresponding_addressingMode(m[pc]);
		if(is_absolute_address(m[pc]) == 1){
			printf("\t ra = ra | ");
			if(toSet == DYNAMIC){
				printf("read8(");
                                call_corresponding_addressingMode(m[pc]);
                                printf(");\n");
                                toSet = IR;
                        }else{
				printf("%d;\n", m[parameter]);
			}
		}
		if(optimization){
			if(defs & (1 << NF)){
                        	code_for_NF_flag(RA);
                	}
                	if(defs & (1 << ZF)){
                        	code_for_ZF_flag(RA);
                	}
		}else{
			code_for_NF_flag(RA);
			code_for_ZF_flag(RA);
		}

	}
}

void PHA(void){ //PusH Accumulator
	if(toSet == DEFS){
		defs = 0;
		uses = 0;
		add_used_helper_function(push8);
		usedRegisters = (1 << RA);
        }else if(toSet == IR){
		printf("\t //PHA\n");
		printf("\t push8(ra);\n");
	}
}

void PHP(void){ //PusH Processor status
        //printf("PHP:\n");
	if(toSet == DEFS){
		defs = 0;
                //uses = (1 << NF) | (1 << VF) | (1 << DF) | (1 << IF) | (1 << ZF) | (1 << CF);
		uses = 0;
		add_used_helper_function(push8);
        }else if(toSet == IR){
		printf("\t //PHP\n");
		printf("\t push8(flags);\n");
	}
}

void PLA(void){ //PuLl Accumulator
        //printf("PLA:\n");
	if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF);
                uses = 0;
		add_used_helper_function(pull8);
		add_used_helper_function(setflag);
		usedRegisters = (1 << RA);
        }else if(toSet == IR){
		printf("\t //PLA\n");
		printf("\t ra = pull8();\n");
		if(optimization){
			if(defs & (1 << NF)){
                        	code_for_NF_flag(RA);
                	}
                	if(defs & (1 << ZF)){
                        	code_for_ZF_flag(RA);
                	}
		}else{
			code_for_NF_flag(RA);
			code_for_ZF_flag(RA);
		}

	}
}

void PLP(void){ //PuLl Processor status
	if(toSet == DEFS){
		defs = (1 << BF) | (1 << XX);
                uses = 0;
		add_used_helper_function(pull8);
        }else if(toSet == IR){
		printf("\t //PLP \n");
		printf("\t flags = pull8();\n");
	}
}

void ROL(void){ //ROL... Rotate one Bit left (memory or accumulator)
	if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF) | (1 << CF);
                uses = (1 << CF);
		if((m[pc] & 0xf) != 0xa){
			add_used_helper_function(read8);
			add_used_helper_function(write8);
			usedRegisters = (1 << TEMP);
		}else{
			usedRegisters = (1 << RA) | (1 << TEMP);
		}
		add_used_helper_function(setflag);
        }else if(toSet == IR){
		printf("\t //ROL\n");
		if(is_absolute_address(m[pc]) == 0){
			printf("\t temp = (ra << 1) + (flags & (1 << %d));\n", CF);
                	printf("\t ra = (uint8_t) (temp & 0xFF);\n");
                	printf("\t setflag(%d, ((temp & 0xFF00) > 0));\n", CF);
			if(optimization){
				if(defs & (1 << NF)){
                                	code_for_NF_flag(RA);
                        	}
                        	if(defs & (1 << ZF)){
                                	code_for_ZF_flag(RA);
                        	}
			}
		}else{
			call_corresponding_addressingMode(m[pc]);
			if(is_in_ROM(parameter)){
                	        //ROM cannot be modified
        	                return;
	                }

			printf("\t temp = "); 
			if(toSet == DYNAMIC){
				printf("(read8(");
                                call_corresponding_addressingMode(m[pc]);
                                printf(")");
                                toSet = IR;
                        }else{
				printf("(%d", m[parameter]); 
			}
			printf(" << 1) + (flags & (1 << %d));\n", CF);
                	printf("\t write8(");
		        if(toSet == DYNAMIC){
                                call_corresponding_addressingMode(m[pc]);
                                printf(",");
                        }else{	
				printf("%d,", parameter);
			}
			printf(" (uint8_t) (temp & 0xFF));\n");
                	printf("\t setflag(%d, ((temp & 0xFF00) > 0));\n", CF);
			if(optimization){
				if(defs & (1 << ZF)){
                 			code_for_ZF_dynamic(toSet);
                		}
                		if(defs & (1 << NF)){
                                	code_for_NF_dynamic(toSet);
                		}
			}else{
				code_for_ZF_dynamic(toSet);
				code_for_NF_dynamic(toSet);
			}
		}
	}
}

void ROR(void){ //ROR... Rotate one Bit right (memory or accumulator)
	if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF) | (1 << CF);
                uses = (1 << CF);
		if((m[pc] & 0xf) != 0xa){
                        add_used_helper_function(read8);
                        add_used_helper_function(write8);
			usedRegisters = (1 << 4);
                }else{
			usedRegisters = (1 << RA) | (1 <<4);
		}
		add_used_helper_function(setflag);
        }else if(toSet == IR){
		printf("\t //ROR\n");
		if(is_absolute_address(m[pc]) == 0){
			printf("\t temp = ((flags & (1 << %d)) << 7) | (ra >> 1);\n", CF);
                	printf("\t setflag(%d, (ra & 0x01));\n", CF);
                	printf("\t ra = (uint8_t) (temp & 0xFF);\n");
			
			if(optimization){
				if(defs & (1 << NF)){
                        		code_for_NF_flag(RA);
	                	}
        	        	if(defs & (1 << ZF)){
                	        	code_for_ZF_flag(RA);
                		}
			}else{
				code_for_NF_flag(RA);
				code_for_ZF_flag(RA);
			}

		}else{
			call_corresponding_addressingMode(m[pc]);
			if(is_in_ROM(parameter)){
                	        //ROM cannot be modified
        	                return;
	                }

			printf("\t temp = ((flags & (1 << %d)) << 7) | ", CF);
			if(toSet == DYNAMIC){
				printf("(read8(");
                                call_corresponding_addressingMode(m[pc]);
                                printf(")");
                                toSet = IR;
                        }else{
				printf("(%d", m[parameter]);
			}
			printf(" >> 1);\n");
                	printf("\t setflag(%d, (", CF);
			if(toSet == DYNAMIC){
				printf("read8(");
                                call_corresponding_addressingMode(m[pc]);
                                printf(")");
                                toSet = IR;
                        }else{
				printf("%d", m[parameter]);
			}
			printf("& 0x01));\n");
                	printf("\t write8("); 
			if(toSet == DYNAMIC){
                                call_corresponding_addressingMode(m[pc]);
                                printf(",\n");
                                toSet = IR;
                        }else{
				printf("%d,", parameter); 
			}
			printf(" (uint8_t) (temp & 0xFF));\n");
			if(optimization){
				if(defs & (1 << ZF)){
                        		code_for_ZF_dynamic(toSet);
                		}
                		if(defs & (1 << NF)){
                        		code_for_NF_dynamic(toSet);
                		}
			}else{
				code_for_ZF_dynamic(toSet);
				code_for_NF_dynamic(toSet);
			}
		}
	}
}

void RTI(void){ //Return from Interrupt
	if(toSet == DEFS){
		defs = (1 << BF);
                uses = 0;
		add_used_helper_function(pull8);
        }else if(toSet == IR){
		printf("\t flags = pull8();\n");
		printf("\t pc = pull8();\n");
        	printf("\t pc |= (pull8() << 8);\n");
	}
}

void RTS(void){ // return from subroutine
	if(toSet == DEFS){
		defs = 0;
		uses = 0;
		//add_used_helper_function(pull8);
        }else if(toSet == IR){
		printf("\t //RTS\n");
		printf("\t goto jump_table;\n");
	}
}

void SBC(void){ //SBC... subtract with carry
	if(toSet == DEFS){
		defs = (1 << NF) | (1 << VF) | (1 << ZF) | (1 << CF);
                uses = (1 << CF);
		if(is_absolute_address(m[pc]) == 1){
			add_used_helper_function(read8);
		}
		add_used_helper_function(setflag);
		usedRegisters = (1 << RA) | (1 << TEMP);
        }else if(toSet == IR){
		printf("\t //SBC\n");
		call_corresponding_addressingMode(m[pc]);
		if(bcd){
			if(is_absolute_address(m[pc]) == 0){
				printf("\t temp = %d ^ 0xFF) + 1;\n", parameter);
			}else{
				printf("\t temp = (");
                        	if(toSet == DYNAMIC){
                                	printf("read8(");
                                	call_corresponding_addressingMode(m[pc]);
                                	printf(")");
                        	}else{
                                	printf("%d", m[parameter]);
                        	}
				printf("^ 0xFF) + 1;\n");
			}
			printf("\t temp = add(ra, temp) - (~(flags & (1<<%d)) & (1 << %d));\n", CF, CF);
		}else{
			call_corresponding_addressingMode(m[pc]);
			if(is_absolute_address(m[pc]) == 1){
				printf("\t temp = (ra + ");
				if(toSet == DYNAMIC){
					printf("read8(");
                                	call_corresponding_addressingMode(m[pc]);
                                	printf(")");
                                	toSet = IR;
                        	}else{
					printf("%d)", parameter);
				}
				printf("- (~(flags & (1<<%d)));\n", CF);
				printf("\t ra = temp & 0xff;\n");
			}else{
				printf("\t temp = (ra + %d) - (~(flags & (1<<%d));\n", parameter, CF);
				printf("\t ra = temp & 0xff;\n");
			}
		}
	
		if(optimization){	
			if(defs & (1 << VF)){
                        	code_for_set_VF_ADC_SBC(parameter);
                	}
               		if(defs & (1 << CF)){
                        	code_for_CF_flag();
                	}
                	if(defs & (1 << ZF)){
                        	code_for_ZF_flag(RA);
                	}
               		if(defs & (1 << NF)){
                        	code_for_NF_flag(RA);
                	}
		}else{
			code_for_set_VF_ADC_SBC(parameter);
			code_for_CF_flag();
			code_for_ZF_flag(RA);
			code_for_NF_flag(RA);
		}
	}
}

void SEC(void){ //SEC... Set Carry Flag
	if(toSet == DEFS){
		defs = (1 << CF);
                uses = 0;
		add_used_helper_function(setflag);
        }else if(toSet == IR){
		printf("\t setflag(CF, 1);\n");
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
		printf("\t setflag(DF,1);\n");
	}
}

void SEI(void){ //Set Interrupt Disable Status
	if(toSet == DEFS){
		defs = (1 << BF);
                uses = 0;
		add_used_helper_function(setflag);
        }else if(toSet == IR){
		printf("\t setflag(BF, 1);\n");
	}
}



void TAX(void){ // Transfer Accumulator to Index X
	if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF);
                uses = 0;
		usedRegisters = (1 << RA) | (1 << RX);
        }else if(toSet == IR){
		printf("\t rx = ra;\n");
		if(optimization){
			if(defs & (1 << ZF)){
                        	code_for_ZF_flag(RX);
                	}
                	if(defs & (1 << NF)){
                        	code_for_NF_flag(RX);
                	}
		}else{
			code_for_ZF_flag(RX);
			code_for_NF_flag(RX);
		}

	}
}

void TAY(void){ // Transfer Accumulator to Index Y
	if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF);
                uses = 0;
		usedRegisters = (1 << RA) | (1 << RY);
		add_used_helper_function(setflag);
        }else if(toSet == IR){
		printf("\t ry = ra;\n");
		if(optimization){
			if(defs & (1 << ZF)){
                        	code_for_ZF_flag(RY);
                	}
                	if(defs & (1 << NF)){
                        	code_for_NF_flag(RY);
                	}
		}else{
			code_for_ZF_flag(RY);
			code_for_NF_flag(RY);
		}

	}
}

void TSX(void){ //Transfer Stackpointer to X
	if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF);
                uses = 0;
		usedRegisters = (1 << RX) | (1 << RS);
		add_used_helper_function(setflag);
        }else if(toSet == IR){
		printf("\t rx = rs;\n");
		if(optimization){
			if(defs & (1 << ZF)){
                        	code_for_ZF_flag(RX);
                	}
                	if(defs & (1 << NF)){
                        	code_for_NF_flag(RX);
                	}
		}else{
			code_for_ZF_flag(RX);
			code_for_NF_flag(RX);
		}

	}
}

void TXA(void){ // Transfer Index X to  Accumulator
	if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF);
                uses = 0;
		usedRegisters = (1 << RA) | (1 << RX);
		add_used_helper_function(setflag);
        }else if(toSet == IR){
		printf("\t ra = rx;\n");
		if(optimization){
			if(defs & (1 << ZF)){
                        	code_for_ZF_flag(RA);
                	}
                	if(defs & (1 << NF)){
                        	code_for_NF_flag(RA);
                	}
		}else{
			code_for_ZF_flag(RA);
			code_for_NF_flag(RA);
		}

	}
}

void TXS(void){ //Transfer X to Stackpointer
	if(toSet == DEFS){
		defs = 0;
		uses = 0;
		usedRegisters = (1 << RX) | (1 << RS);
        }else if(toSet == IR){
		printf("\t rs = rx;\n");
	}
}

void TYA(void){ // Transfer Index Y to  Accumulator
	if(toSet == DEFS){
		defs = (1 << NF) | (1 << ZF);
                uses = 0;
		usedRegisters = (1 << RA) | (1 << RY);
		add_used_helper_function(setflag);
        }else if(toSet == IR){
		printf("\t ra = ry;\n");
		if(optimization){
                        if(defs & (1 << ZF)){
                                code_for_ZF_flag(RA);
                        }
                        if(defs & (1 << NF)){
                                code_for_NF_flag(RA);
                        }
                }else{
                        code_for_ZF_flag(RA);
                        code_for_NF_flag(RA);
                }

	}
}

/* "Illegal" Opcodes - not supported by translator*/

void ALR(void){ //A AND operand + LSR
}

void ANC(void){ //A AND operand + set C as ASL
}

void ANC2(void){ // A AND operand + set C as ROL
}

void ARR(void){ //A AND operand + ROR
}

void DCP(void){ //DEC operand + CMP oper
}

void ISC(void){ //INC operand + SBC operand
}

void LAS(void){ //LDA/TSX operand
}

void LAX(void){ //LDA operand + LDX operand
}

void RLA(void){ //ROL operand + AND operand
}

void RRA(void){ //ROR operand + ADC Operand
}

void SAX(void){ // A and X are put on bus at the same time -> like A AND X
}

void SBX(void){ //CMP and DEC at onces
}

void SLO(void){ //ASL operand + ORA operand
}

void SRE(void){ //LSR operand + EOR operand
}

void USBC(void){ //SBC + NOP
}

void JAM(void){ //freeze the CPU with $FF on data bus
     	printf("//JAM\n");
	printf("\t return 0;\n");
}

/* Instructions */
void STA(void){ //STA... store accumulator in memory
        //printf("STA:\n");
	if(toSet == DEFS){
		defs = 0;
		uses = 0;
		add_used_helper_function(write8);
		if(!((code[m[pc]].addressingMode == 0x5) || (code[m[pc]].addressingMode == 0x0d))){
			add_used_helper_function(read8);
		}
		check_for_indexed_addressing(m[pc]);
	}else if(toSet == IR){
		printf("\t //STA\n");
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
		check_for_indexed_addressing(m[pc]);
	}else if(toSet == IR){
		printf("\t //STX\n");
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
                        toSet = IR;
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
		check_for_indexed_addressing(m[pc]);
        }else if(toSet == IR){
		printf("\t //STY\n");
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
                                toSet = DYNAMIC;
                        }
                }else{
                        printf("jump address is in RAM!!!\n");
                        toSet = DYNAMIC;
                }
        }
}

void indirect_y(void){
	if(toSet == BYTES){
                bytes = 2;
#if WCET
		cycles++;
#endif
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
	bytes = 2;
	parameter = m[pc + 1];
	if((parameter & 0x80) > 0){
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


