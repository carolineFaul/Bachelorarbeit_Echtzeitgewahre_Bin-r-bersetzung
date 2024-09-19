#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <string.h>

#define OUTPUT_ADDRESS 0xf001
#define INPUT_ADDRESS 0xf004
#define MEMORY 64 * 1024
#define HIGHEST_RAM_ADDRESS 0xccff

/*A Accumulator, Y Index Register, X Index Register, S Stackpointer as 8-bit registers*/
uint8_t r[4];
/* symbolic names for regs indexes*/
enum { RA=0, RY, RX, RS};
/* program counter as 16-bit register */
uint16_t pc;
uint16_t oldpc = 0xffff;
/* flag bits: N Negative, O Overflow, -, B BRK Command, D Decimal Mode, I IRQ disable, Z Zero, C Carry */
uint8_t flags;
/* flag bit numbers */
enum { CF=0, ZF, IF, DF, BF, XX, VF, NF };
/* memory (RAM) 64 KB = 64 * 1024 Byte = 65536 Byte*/
uint8_t m[MEMORY];

//counter for cycles needed for executing program
int cycles;
uint16_t parameter;
void (*lastInstruction)(void);

/* emulation of interrupt pins */
uint8_t nmi_pin;
uint8_t irq_pin;
uint8_t reset_pin;
enum {LOW, HIGH};

struct Instructions{
	//opcode
	void (*opcode)(void);
	//addressing mode
	void (*addressingMode)(void);
	//clock cycles needed
	int cycles;
	//opcode mnemonic
	char *mnemonic;
};

int 
convert_number_to_bcd(int number){
	int bcd_number = 0;
	//devide number by 10
	uint8_t shift = 0;
	while(number > 0){
		//get last digit
		uint8_t digit = number % 10;
		//add representation of digit to bcd_number at correct position
		bcd_number |= (digit << shift);
		//shift decimal number to the right
		number = (number-digit)/10; 
		//increase shift by 4 for next iteration
		shift += 4;
	}
	return bcd_number;
}

int 
convert_bcd_to_number(int bcd_number){
	int number = 0;
	uint8_t shift = 0;
	while (bcd_number > 0){
		//get last 4 bits
		int digit = bcd_number & 0xF; 
		//add digit to number
		for(int i = 0; i < shift; i++){
			//multiply shift times with 10
			digit = digit * 10;
		}
		number += digit;
		//shift bdc 4 bits to the right
		bcd_number = (bcd_number >> 4);
		shift++;
	}
	return number;
}

int 
add(uint8_t number, uint8_t add_term){
	if((flags & (1 << DF)) > 0){
                //decimal mode on
                number = convert_bcd_to_number(number);
		number += convert_bcd_to_number(add_term);
		return convert_number_to_bcd(number);
	}else{
		return number + add_term;
	}
}

int 
sub(uint8_t number, uint8_t sub_term){
        if((flags & (1 << DF)) > 0){
                //decimal mode on
                number = convert_bcd_to_number(r[RX]);
                number -= sub_term;
                return convert_number_to_bcd(number);
        }else{
                return number - sub_term;
        }
}

void
setflag(int flag, uint8_t value){
	if(value == 0){
		//clear flag
		flags &= ~(1 << flag);
	}else if(value == 1){
		//set flag
		flags |= (1 << flag);
	}else{
		return;
	}
}

void 
write8(uint16_t addr, uint8_t val){
	#if DECIMAL_MODE
	if((flags & (1 << DF)) == (1 << DF)){
		val = convert_bcd_to_number(val);
	}
	#endif

        if(addr == OUTPUT_ADDRESS){
                // memory mapped I/O output address
               	// putchar(val);
		if(val >= 'A' && val <= 'z'){
			printf("%c\n", val);
		}else{
               		printf("%d\n", val);
		}
        }else{
                //ordinary write to memory (only possible to write in RAM part)
                if((addr >= 0x0) && (addr <= HIGHEST_RAM_ADDRESS)){
                        //address is in memory
                        m[addr] = val;
                }else{
                        //address out of memory -> do nothing to prevent out of memory exception
                        return;
                }
        }
}

uint16_t
read8(uint16_t addr){
        if(addr == INPUT_ADDRESS){
                //printf("*** waiting for input\n");
                return getchar();
        }else{
                return m[addr];
        }
}

uint8_t
pull8(void){
	//increment pc
	r[RS]++;
	//pull from stack
	return m[0x0100 + r[RS]];
}

void
push8(uint8_t value){
	//store content
	write8((0x0100 + r[RS]), value);
       	//decrement pc
       	r[RS]--;
}

void 
reset(void){
	//cycle 0: initialize sp with 0
	//cycle 3: push highest byte of pc to 0x0100 + SP (ignore result) & decrement SP
	r[RS]--;
	//cycle 4: push lowest byte of pc to 0x0100 + SP (ignore result) & decrement SP
	r[RS]--;
	//cycle 5: push processor status (flags) to 0x0100 + SP (ignore result) & decrement SP
	r[RS]--;
	//cycle 6 + 7: read lowest and highest byte from vector FFFC FFFC into pc
	/* fetch program counter at 0xFFFC and 0xFFFD (little endian)*/
        pc = m[0xFFFC] + (m[0xFFFD] << 8);
	//reset needed 7 cycles
	cycles = 7;

}

void
print_hex_as_bin(uint8_t hex){
	uint8_t binary[8];
	for(uint8_t pos = 0; pos < 8; pos++){
		uint8_t bit = hex & 0x1;
		binary[7 - pos] = bit;
		hex = hex >> 1;
	}
	for(uint8_t pos = 0; pos < 8; pos++){
		printf("%x", binary[pos]);
	}
}

void
trace(void){
	printf("\t pc: \t %x\n", pc);
	printf("\t opcode: \t %x\n\t next bytes in memory: \t ", m[pc]);
	if(pc + 2 < MEMORY){
		printf("%x %x %x\n", m[pc], m[pc + 1], m[pc + 2]);
	}else if(pc + 1 < MEMORY){
		printf("%x %x\n", m[pc], m[pc + 1]);
	}else{
		printf("%x\n", m[pc]);
	}
	printf("\t RA: \t %x\n \t RX: \t %x\n \t RY: \t %x\n \t RS: \t %x\n", r[RA], r[RX], r[RY], r[RS]);
	printf("\t Processor Status (NF, VF, -, BF, DF, IF, ZF, CF): \t ");
	print_hex_as_bin(flags);
	//printf("\n\tSP points at: %x", m[0x100 + r[RS]]);
	printf("\n\n");
}


int 
emulate_6502(void){
	uint8_t is_address = 0; //is false
	/* initilaize pc to zero for reset sequence*/
	pc = 0;
	/* initialize internal CPU state */
	r[RA] = 0;
	r[RY] = 0;
	r[RX] = 0;
	r[RS] = 0;
	flags = 0;

	/* initialize irq_pin and nmi_pin for interrupts -> need to be high if no interrupt occured */
	nmi_pin = HIGH;
	irq_pin = HIGH;

	struct Instructions code[256] = {
		{&&BRK, &&implied, 7, "BRK"},{&&ORA, &&indexed_x, 6, "ORA"},{&&JAM, &&implied, 1, "JAM"},{&&SLO, &&indexed_x, 8, "SLO"},{&&NOP, &&zeropage, 3, "NOP"},{&&ORA, &&zeropage, 3, "NOP"},{&&ASL, &&zeropage, 5, "ASL"},{&&SLO, &&zeropage, 5, "SLO"},
		{&&PHP, &&implied, 3, "PHP"},{&&ORA, &&immediate, 2, "ORA"},{&&ASL, &&implied, 2, "ASL"},{&&ANC, &&immediate, 2, "ANC"},{&&NOP, &&absolute, 4, "NOP"},{&&ORA, &&absolute, 4, "ORA"},{&&ASL, &&absolute, 6, "ASL"},{&&SLO, &&absolute, 6, "SLO"},
		
		{&&BPL, &&relative, 2, "BPL"},{&&ORA, &&indirect_y, 5, "ORA"},{&&JAM, &&implied, 1, "JAM"},{&&SLO, &&indirect_y, 8, "SLO"},{&&NOP, &&zeropage_x, 4, "NOP"},{&&ORA, &&zeropage_x, 4, "ORA"},{&&ASL, &&zeropage_x, 6, "ASL"},{&&SLO,&&zeropage_x, 6, "SLO"},
		{&&CLC, &&implied, 2, "CLC"},{&&ORA, &&absolute_y, 4, "ORA"},{&&NOP, &&implied, 2, "NOP"},{&&SLO, &&absolute_y, 7, "SLO"},{&&NOP, &&absolute_x, 4, "NOP"},{&&ORA, &&absolute_x, 4, "ORA"},{&&ASL, &&absolute_x, 7, "ASL"},{&&SLO, &&absolute_x, 7, "SLO"},

		{&&JSR, &&absolute, 6, "JSR"},{&&AND, &&indexed_x, 6, "AND"},{&&JAM, &&implied, 1, "JAM"},{&&RLA, &&indexed_x, 8, "RLA"},{&&BIT, &&zeropage, 3, "BIT"},{&&AND, &&zeropage, 3, "AND"},{&&ROL, &&zeropage, 5, "ROL"},{&&RLA, &&zeropage, 5, "RLA"},
		{&&PLP, &&implied, 4, "PLP"},{&&AND, &&immediate, 2, "AND"},{&&ROL, &&implied, 2, "ROL"},{&&ANC2, &&immediate, 2, "ANC2"},{&&BIT, &&absolute, 4, "BIT"},{&&AND, &&absolute, 4, "AND"},{&&ROL, &&absolute, 6, "ROL"},{&&RLA, &&absolute, 6, "RLA"},

		{&&BMI, &&relative, 2, "BMI"},{&&AND, &&indirect_y, 5, "AND"},{&&JAM, &&implied, 1, "JAM"},{&&RLA, &&indirect_y, 8, "RLA"},{&&NOP, &&zeropage_x, 4, "NOP"},{&&AND, &&zeropage_x, 4, "AND"},{&&ROL, &&zeropage_x, 6, "ROL"},{&&RLA, &&zeropage_x, 5, "RLA"},
		{&&SEC, &&implied, 2, "SEC"},{&&AND, &&absolute_y, 4, "AND"},{&&NOP, &&implied, 2, "NOP"},{&&RLA, &&absolute_y, 7, "RLA"},{&&NOP, &&absolute_x, 4, "NOP"},{&&AND, &&absolute_x, 4, "AND"},{&&ROL, &&absolute_x, 7, "ROL"},{&&RLA, &&absolute_x, 7, "RLA"},

		{&&RTI, &&implied, 6, "RTI"},{&&EOR, &&indexed_x, 6, "EOR"},{&&JAM, &&implied, 1, "JAM"},{&&SRE, &&indexed_x, 8, "SRE"},{&&NOP, &&zeropage, 3, "NOP"},{&&EOR, &&zeropage, 3, "EOR"},{&&LSR, &&zeropage, 5, "LSR"},{&&SRE, &&zeropage, 5, "SRE"},
		{&&PHA, &&implied, 3, "PHA"},{&&EOR, &&immediate, 2, "EOR"},{&&LSR, &&implied, 2, "LSR"},{&&ALR, &&immediate, 2, "ALR"},{&&JMP, &&absolute, 3, "JMP"},{&&EOR, &&absolute, 4, "EOR"},{&&LSR, &&absolute, 7, "LSR"},{&&SRE, &&absolute, 6, "SRE"},

		{&&BVC, &&relative, 2, "BVC"},{&&EOR, &&indirect_y, 5, "EOR"},{&&JAM, &&implied, 1, "JAM"},{&&SRE, &&indirect_y, 8, "SRE"},{&&NOP, &&zeropage_x, 4, "NOP"},{&&EOR, &&zeropage_x, 4, "EOR"},{&&LSR, &&zeropage_x, 6, "LSR"},{&&SRE, &&zeropage_x, 6, "SRE"},
		{&&CLI, &&implied, 2, "CLI"},{&&EOR, &&absolute_y, 4, "EOR"},{&&NOP, &&implied, 2, "NOP"},{&&SRE, &&absolute_y, 7, "SRE"},{&&NOP, &&absolute_x, 4, "NOP"},{&&EOR, &&absolute_x, 4, "EOR"},{&&LSR, &&absolute_x, 7, "LSR"},{&&SRE, &&absolute_x, 7, "SRE"},

		{&&RTS, &&implied, 6, "RTS"},{&&ADC, &&indexed_x, 6, "ADC"},{&&JAM, &&implied, 1, "JAM"},{&&RRA, &&indexed_x, 8, "RRA"},{&&NOP, &&zeropage, 3, "NOP"},{&&ADC, &&zeropage, 3, "ADC"},{&&ROR, &&zeropage, 5, "ROR"},{&&RRA, &&zeropage, 5, "RRA"},
		{&&PLA, &&implied, 4, "PLA"},{&&ADC, &&immediate, 2, "ADC"},{&&ROR, &&implied, 2, "ROR"},{&&ARR, &&immediate, 2, "ARR"},{&&JMP, &&indirect, 5, "JMP"},{&&ADC, &&absolute, 4, "ADC"},{&&ROR, &&absolute, 6, "ROR"},{&&RRA, &&absolute, 6, "RRA"},

		{&&BVS, &&relative, 2, "BVS"},{&&ADC, &&indirect_y, 5, "ADC"},{&&JAM, &&implied, 1, "JAM"},{&&RRA, &&indirect_y, 8, "RRA"},{&&NOP, &&zeropage_x, 4, "NOP"},{&&ADC, &&zeropage_x, 4, "ADC"},{&&ROR, &&zeropage_x, 6, "ROR"},{&&RRA, &&zeropage_x, 6, "RRA"},
		{&&SEI, &&implied, 2, "SEI"},{&&ADC, &&absolute_y, 4, "ADC"},{&&NOP, &&implied, 2, "NOP"},{&&RRA, &&absolute_y, 7, "RRA"},{&&NOP, &&absolute_x, 4, "NOP"}, {&&ADC, &&absolute_x, 4, "ADC"},{&&ROR, &&absolute_x, 7, "ROR"},{&&NOP, &&absolute_x, 7, "NOP"},

		{&&NOP, &&immediate, 2, "NOP"},{&&STA, &&indexed_x, 6, "STA"},{&&JAM, &&immediate, 2, "JAM"},{&&SAX, &&indirect_y, 6, "SAX"},{&&STY, &&zeropage, 3, "STY"},{&&STA, &&zeropage, 3, "STA"},{&&STX, &&zeropage, 3, "STX"},{&&SAX, &&zeropage, 3, "SAX"},
		{&&DEY, &&implied, 2, "DEY"},{&&NOP, &&immediate, 2, "NOP"},{&&TXA, &&implied, 2, "TXA"},{&&NOP, &&implied, 1, "NOP"},{&&STY, &&absolute, 4, "STY"},{&&STA, &&absolute, 4, "STA"},{&&STX, &&absolute, 4, "STX"},{&&SAX, &&absolute, 4, "SAX"},

		{&&BCC, &&relative, 2, "BCC"},{&&STA, &&indirect_y, 6, "STA"},{&&JAM, &&implied, 1, "JAM"},{&&NOP, &&implied, 1, "NOP"},{&&STY, &&zeropage_x, 4, "STY"},{&&STA, &&zeropage_x, 4, "STA"},{&&STX, &&zeropage_y, 4, "STX"},{&&SAX, &&zeropage_x, 4, "SAX"},
		{&&TYA, &&implied, 2, "TYA"},{&&STA, &&absolute_y, 5, "STA"},{&&TXS, &&implied, 2, "TXS"},{&&NOP, &&implied, 1, "NOP"},{&&NOP, &&implied, 1, "NOP"},{&&STA, &&absolute_x, 5, "STA"},{&&NOP, &&implied, 1, "NOP"},{&&NOP, &&implied, 1, "NOP"},

		{&&LDY, &&immediate, 2, "LDY"},{&&LDA, &&indexed_x, 6, "LDA"},{&&LDX, &&immediate, 2, "LDX"},{&&LAX, &&indexed_x, 6, "LAX"},{&&LDY, &&zeropage, 3, "LDY"},{&&LDA, &&zeropage, 3, "LDA"},{&&LDX, &&zeropage, 3, "LDX"},{&&LAX, &&zeropage, 3, "LAX"},
		{&&TAY, &&implied, 2, "TAY"},{&&LDA, &&immediate, 2, "LDA"},{&&TAX, &&implied, 2, "TAX"},{&&NOP, &&implied, 1, "NOP"},{&&LDY, &&absolute, 4, "LDY"},{&&LDA, &&absolute, 4, "LDA"},{&&LDX, &&absolute, 4, "LDX"},{&&LAX, &&absolute, 4, "LAX"},

		{&&BCS, &&relative, 2, "BCS"},{&&LDA, &&indirect_y, 5, "LDA"},{&&JAM, &&implied, 1, "JAM"},{&&LAX, &&indirect_y, 5, "LAX"},{&&LDY, &&zeropage_x, 4, "LDY"},{&&LDA, &&zeropage_x, 4, "LDA"},{&&LDX, &&zeropage_y, 4, "LDX"},{&&LAX, &&zeropage_y, 4, "LAX"},
		{&&CLV, &&implied, 2, "CLV"},{&&LDA, &&absolute_y, 4, "LDA"},{&&TSX, &&implied, 2, "TSX"},{&&LAS, &&absolute_y, 4, "LAS"},{&&LDY, &&absolute_x, 4, "LDY"},{&&LDA, &&absolute_x, 4, "LDA"},{&&LDX, &&absolute_y, 4, "LDX"},{&&LAX, &&absolute_y, 4, "LAX"},

		{&&CPY, &&immediate, 2, "CPY"},{&&CMP, &&indexed_x, 6, "CMP"},{&&JAM, &&immediate, 2, "JAM"},{&&DCP, &&indexed_x, 8, "DCP"},{&&CPY, &&zeropage, 3, "CPY"},{&&CMP, &&zeropage, 3, "CMP"},{&&DEC, &&zeropage, 5, "DEC"},{&&DCP, &&zeropage, 5, "DCP"},
		{&&INY, &&implied, 2, "INY"},{&&CMP, &&immediate, 2, "CMP"},{&&DEX, &&implied, 2, "DEX"},{&&SBX, &&immediate, 2, "SBX"},{&&CPY, &&absolute, 4, "CPY"},{&&CMP, &&absolute, 4, "CMP"},{&&DEC, &&absolute, 6, "DEC"},{&&DCP, &&absolute, 6, "DCP"},

		{&&BNE, &&relative, 2, "BNE"},{&&CMP, &&indirect_y, 5, "CMP"},{&&JAM, &&implied, 1, "JAM"},{&&DCP, &&indirect_y, 8, "DCP"},{&&NOP, &&zeropage_x, 4, "NOP"},{&&CMP, &&zeropage_x, 4, "CMP"},{&&DEC, &&zeropage_x, 6, "DEC"},{&&DCP, &&zeropage_x, 6, "DCP"},
		{&&CLD, &&implied, 2, "CLD"},{&&CMP, &&absolute_y, 4, "CMP"},{&&NOP, &&implied, 2, "NOP"},{&&DCP, &&absolute_y, 7, "DCP"},{&&NOP, &&absolute_x, 4, "NOP"},{&&CMP, &&absolute_x, 4, "CMP"},{&&DEC, &&absolute_x, 7, "DEC"},{&&DCP, &&absolute_x, 7, "DCP"},

		{&&CPX, &&immediate, 2, "CPX"},{&&SBC, &&indexed_x, 6, "SBC"},{&&NOP, &&immediate, 2, "NOP"},{&&ISC, &&indexed_x, 8, "ISC"},{&&CPX, &&zeropage, 3, "CPX"},{&&SBC, &&zeropage, 3, "SBC"},{&&INC, &&zeropage, 5, "INC"},{&&ISC, &&zeropage, 5, "ISC"},
		{&&INX, &&implied, 2, "INX"},{&&SBC, &&immediate, 2, "SBC"},{&&NOP, &&implied, 2, "NOP"},{&&USBC, &&immediate, 2, "USBC"},{&&CPX, &&absolute, 4, "CPX"},{&&SBC, &&absolute, 4, "SBC"},{&&INC, &&absolute, 6, "INC"},{&&ISC, &&absolute, 6, "ISC"},

		{&&BEQ, &&relative, 2, "BEQ"},{&&SBC, &&indirect_y, 5, "SBC"},{&&JAM, &&implied, 1, "JAM"},{&&ISC, &&indirect_y, 8, "ISC"},{&&NOP, &&zeropage_x, 4, "NOP"},{&&SBC, &&zeropage_x, 4, "SBC"},{&&INC, &&zeropage_x, 6, "INC"},{&&ISC, &&zeropage_x, 6, "ISC"},
		{&&SED, &&implied, 2, "SED"},{&&SBC, &&absolute_y, 4, "SBC"},{&&NOP, &&implied, 2, "NOP"},{&&ISC, &&absolute_y, 7, "ISC"},{&&NOP, &&absolute_x, 4, "NOP"},{&&SBC, &&absolute_x, 4, "SBC"},{&&INC, &&absolute_x, 7, "INC"},{&&ISC, &&absolute_x, 7, "ISC"}
	};

	//start execution
	reset();
	//set reset pin to high to avoid another reset
	reset_pin = HIGH;
	/* for test purpose only XX must be set to 1*/
	setflag(XX, 1);
	uint8_t opcode = 0;
	uint16_t address = 0;

next_instruction:
        if (pc == oldpc) { printf("!!! ENDLESS LOOP !!!\n"); exit(1); }
        oldpc = pc;
        if(nmi_pin == LOW){
                //nmi occured
                goto nmi;
        }else if(irq_pin == LOW){
        //else if((irq_pin == LOW) || ((flags & (1 << BF)) > 0)){
                //irq occured
                goto irq;
        }else if(reset_pin == LOW){
                reset();
                goto next_instruction;
        }else{
                opcode = m[pc];

                //trace
                printf("\t mnemonic: %s\n", code[opcode].mnemonic);
                trace();

                goto *code[opcode].addressingMode;
        }


	return 0;


//addressing modes	
absolute:
	//use absolute address to load data
	is_address = 1;
	parameter = m[pc+1] | (m[pc+2] << 8);
	cycles += code[opcode].cycles;
	pc = pc + 3;
	goto *code[opcode].opcode;

absolute_x: //address is address incremented with X (with carry)
	is_address = 1;
	//fetch LSB, fetch MSB and add index to LSB
	parameter = (m[pc+1] + r[RX]) | (m[pc+2] << 8); 
	//parameter = (addrWithCarry & 0x00FFFF) + ((addrWithCarry & 0x10000) >> 16);
	if((parameter & 0xFF00) != m[pc+2]){
		//page boundary crossed -> MSB needs to be updated as well
		cycles++;
	}

	cycles += code[opcode].cycles;
        pc = pc + 3;
        goto *code[opcode].opcode;

absolute_y: //address is address incremented with Y (with carry)
	is_address = 1;
	//fetch LSB, fetch MSB and add index to LSB
        parameter = (m[pc+1] + r[RY]) | (m[pc+2] << 8);

	if((parameter & 0xFF00) != m[pc+2]){
                //page boundary crossed -> MSB needs to be updated as well
                cycles++;
        }

        cycles += code[opcode].cycles;
        pc = pc + 3;
        goto *code[opcode].opcode;

immediate:
	is_address = 0;
	parameter = m[pc+1];
	cycles += code[opcode].cycles;
	pc = pc + 2;
	goto *code[opcode].opcode;
	
	
relative:
	is_address = 0;
	parameter = m[pc+1];
	if(parameter & 0x80){
		//parameter is negative -> set 2 MSB to FF
		parameter |= 0xFF00;
	}
	parameter = (pc + 2) + parameter;
	
	if((pc & 0xFF00) != ((pc + parameter) & 0xFF00)){
		//MSB was changes == page boundary was crossed
		cycles++;
	}

	cycles += code[opcode].cycles;
	pc = pc + 2;
	goto *code[opcode].opcode;

implied:
	//or accumulator (no additional data implied)
	is_address = 0;
	cycles += code[opcode].cycles;
	pc = pc + 1;
	goto *code[opcode].opcode;

zeropage: //hi-byte is 0x00
	is_address = 1; //set true to indicate that parameter is address
	parameter = (0x00 << 8) | m[pc+1];
	cycles += code[opcode].cycles;
	pc = pc + 2;
	goto *code[opcode].opcode;

zeropage_x: //address is address incremented with x (without carry)
	is_address = 1;
	parameter = (m[pc+1] + r[RX]) & 0xff;
	cycles += code[opcode].cycles;
        pc = pc + 2;
        goto *code[opcode].opcode;

zeropage_y: //address is address incremented with y (without carry)
	is_address = 1;
        parameter = (m[pc+1] + r[RY]) & 0xff;
        cycles += code[opcode].cycles;
        pc = pc + 2;
        goto *code[opcode].opcode;

indirect:
	//parameter is a vector that needs to be resolved
	is_address = 1;
	//load LSB from address and MSB from address+1
	address = m[pc+1] | (m[pc+2] << 8);
	parameter = m[address] | (m[address+1] << 8);
	cycles += code[opcode].cycles;
        pc = pc + 3;
        goto *code[opcode].opcode;

indirect_y:
	is_address = 1;
        //get address
        address = m[pc+1];
        //perform additional lookup
        parameter = m[address] | (m[address+1] << 8);
        //add y to address
        parameter = (parameter + r[RY]);

	if(((parameter & 0xFF00) != m[pc+2]) && (code[opcode].addressingMode != &&STA)){
                //page boundary crossed -> MSB needs to be updated as well
                cycles++;
        }

        cycles += code[opcode].cycles;
        pc = pc + 2;
        goto *code[opcode].opcode;

indexed_x: //pointer is modified with x
	is_address = 1;
        //get address and add x to it
        address = (m[pc+1] + r[RX]) & 0x00FF;
        //perform additional lookup (will never overflow)
        parameter = m[address] | (m[address+1] << 8);
        cycles += code[opcode].cycles;
        pc = pc + 2;
        goto *code[opcode].opcode;

/* instructions */

LDA: //LDA... Load Accumulator with memory
	if(is_address == 1){
                //parameter is address
                parameter = read8(parameter);
        }
        r[RA] = (parameter & (0x00FF));
        setflag(ZF, (r[RA] == 0x00));
        setflag(NF, ((r[RA] & 0x80) > 0));
        goto next_instruction;
	
LDX: //LDA... Load Index X with memory
	if(is_address == 1){
                //parameter is address
                parameter = read8(parameter);
        }
        r[RX] = parameter;
	setflag(ZF, (r[RX] == 0x00));
        setflag(NF, ((r[RX] & 0x80) > 0));
        goto next_instruction;

LDY: //LDY... Load Index Y with memory
	if(is_address == 1){
                //parameter is address
                parameter = read8(parameter);
        }
        r[RY] = parameter;
	setflag(ZF, (r[RY] == 0x00));
        setflag(NF, ((r[RY] & 0x80) > 0));
        goto next_instruction;

ADC: //ADC... add memory to accumulator with carry
	if(is_address == 1){
                //parameter is address
                parameter = read8(parameter);
        }
	uint16_t temp = add(r[RA], parameter) + (flags & (1<<CF));
	//set VF if positive + positive = negative or the other way around
	setflag(VF, ((((temp & 0xFF) ^ r[RA]) & ((temp & 0xFF) ^ parameter) & 0x80) > 0));
	r[RA] = (uint8_t) temp & 0xFF;
        //set ZF if result is 0
        setflag(ZF, (r[RA] == 0x00));
	//set CF if number > 0xFF
        setflag(CF, (temp > 0xFF));
        //set NF if temp is negative
        setflag(NF, ((r[RA] & 0x80) > 0));

	goto next_instruction;

ASL: //Shift Left One Bit (Memory or Accumulator)
	if(is_address == 0){
		//shift accumulator & write back
		temp = r[RA] << 1;
		r[RA] = (uint8_t) (temp & 0x00FF);
	}else{
		//shift memory content & write back
		temp = m[parameter] << 1;
		write8(parameter, (uint8_t) (temp & 0x00FF));
	}

	setflag(CF, ((temp & 0x100) == 0x100));
	setflag(ZF, (r[RA] == 0x00));
        setflag(NF, ((r[RA] & 0x80) > 0));

        goto next_instruction;

LSR: //Shift One Bit Right (Memory or accumulator)
        if(is_address == 0){
                //shift accumulator & write back
		setflag(CF,((r[RA] & 0x1) > 0));
                temp = r[RA] >> 1;
                r[RA] = (uint8_t) (temp & 0x00FF);
        }else{
                //shift memory content & write back
                temp = m[parameter] >> 1;
		setflag(CF, ((m[parameter] & 0x1) > 0));
                write8(parameter, (uint8_t) (temp & 0xFF));
        }

        setflag(ZF, (r[RA] == 0x00));
        setflag(NF, ((r[RA] & 0x80) > 0));

      	goto next_instruction;

ROL: //ROL... Rotate one Bit left (memory or accumulator)
	if(is_address == 0){
                //shift accumulator & write back 
                temp = (r[RA] << 1) + (flags & (1 << CF));
                r[RA] = (uint8_t) (temp & 0xFF);
		setflag(CF, ((temp & 0xFF00) > 0));
        }else{
                //shift memory content & write back
                temp = (m[parameter] << 1) + (flags & (1 << CF));
                write8(parameter, (uint8_t) (temp & 0xFF));
		setflag(CF, ((temp & 0xFF00) > 0));
        }

        setflag(ZF, (r[RA] == 0x00));
        setflag(NF, ((r[RA] & 0x80) > 0));

        goto next_instruction;

ROR: //ROR... Rotate one Bit right (memory or accumulator)
	if(is_address == 0){
                //shift accumulator & write back
                temp = ((flags & (1 << CF)) << 7) | (r[RA] >> 1);
		setflag(CF, (r[RA] & 0x01));
                r[RA] = (uint8_t) (temp & 0xFF);
		setflag(ZF, (r[RA] == 0x00));
        	setflag(NF, ((r[RA] & 0x80) > 0));
        }else{
                //shift memory content & write back
                temp = ((flags & (1 << CF)) << 7) | (m[parameter] >> 1);
		setflag(CF, (m[parameter] & 0x01));
                write8(parameter, (uint8_t) (temp & 0xFF));
		setflag(ZF, (temp == 0x00));
        	setflag(NF, ((temp & 0x80) > 0));
        }

        goto next_instruction;

AND: //AND Memory with accumulator
	if(is_address == 1){
                //parameter is address
                parameter = read8(parameter);
        }

	r[RA] = r[RA] & parameter;
	setflag(ZF, (r[RA] == 0x00));
        setflag(NF, ((r[RA] & 0x80) > 0));
        goto next_instruction;

ORA: //OR Memory with Accumulator
	if(is_address == 1){
		parameter = read8(parameter);
	}

	r[RA] = r[RA] | parameter;
	setflag(ZF, (r[RA] == 0x00));
        setflag(NF, ((r[RA] & 0x80) > 0));
        goto next_instruction;

EOR: //Exclusive-OR Memory with Accumulator
	if(is_address == 1){
		parameter = read8(parameter);
	}
	r[RA] = r[RA] ^ parameter;
	setflag(ZF, (r[RA] == 0x00));
        setflag(NF, ((r[RA] & 0x80) > 0));
        goto next_instruction;

SBC: //SBC... subtract with carry
     	if(is_address == 1){
                //parameter is address
                parameter = read8(parameter);
        }
	//convert parameter in 1's complement
        parameter = (parameter ^ 0xFF) + 1;
        //subtract with carry
        temp = add(r[RA], parameter) - (~(flags & (1<<CF)) & (1 << CF));
        printf("temp: %x\n", temp);
        setflag(CF, ((temp & 0x100) == 0x100));
        setflag(ZF, ((temp & 0x00FF) == 0));
        setflag(VF, ((((temp & 0xFF) ^ r[RA]) & ((temp & 0xFF) ^ parameter) & 0x0080) > 0));
        setflag(NF, (temp & 0x0080) > 0);
        r[RA] = (temp & 0xFF);
        goto next_instruction;

INC: //Increment Memory by one
	write8(parameter, (m[parameter] + 1));
	setflag(ZF, (m[parameter] == 0));
        setflag(NF, ((m[parameter] & 0x80) > 0));
        goto next_instruction;

INX: //Increment Index X by one
	r[RX] = add(r[RX], 1);
	setflag(ZF, (r[RX] == 0x00));
        setflag(NF, ((r[RX] & 0x80) > 0));
	goto next_instruction;

INY: //Increment Index Y by one
        r[RY] = add(r[RY], 1);
        setflag(ZF, (r[RY] == 0x00));
        setflag(NF, ((r[RY] & 0x80) > 0));
        goto next_instruction;

DEC: //Decrement Memory by one
        write8(parameter, sub(m[parameter], 1));
        setflag(ZF, (m[parameter] == 0));
        setflag(NF, ((m[parameter] & 0x80) > 0));
        goto next_instruction;

DEX: //Decrement Index X by one
        r[RX] = sub(r[RX], 1);
        setflag(ZF, (r[RX] == 0x00));
        setflag(NF, ((r[RX] & 0x80) > 0));
        goto next_instruction;

DEY: //Decrement Index Y by one
        r[RY] = sub(r[RY], 1);
        setflag(ZF, (r[RY] == 0x00));
        setflag(NF, ((r[RY] & 0x80) > 0));
        goto next_instruction;

CMP: //CMP... compare memory with accumulator
	if(is_address == 1){
		parameter = read8(parameter);
	} 
	temp = r[RA] - parameter;
	setflag(CF, (r[RA] >= parameter));
	setflag(ZF, (temp == 0x00));
        setflag(NF, ((temp & 0x80) > 0));
	goto next_instruction;

CPX: //CPX... compare Memory with Index X
	if(is_address == 1){
                parameter = read8(parameter);
        }
        temp = r[RX] - parameter;
        setflag(CF, (r[RX] >= parameter));
        setflag(ZF, (temp == 0x00));
        setflag(NF, ((temp & 0x80) > 0 ));
        goto next_instruction;

CPY: //CPY... compare Memory with Index Y
	if(is_address == 1){
                parameter = read8(parameter);
        }
        temp = r[RY] - parameter;
        setflag(CF, (r[RY] >= parameter));
        setflag(ZF, (temp == 0x00));
        setflag(NF, ((temp & 0x80) > 0));
        goto next_instruction;

BIT: //BIT... Test Bits in Memory with accumulator
     	//ZF is set if m[parameter] & r[RA] == 0
	setflag(ZF, ((m[parameter] & r[RA]) == 0x00));
	//7th bit transfered into NF
	setflag(NF, ((m[parameter] & (1 << 7)) > 0));
	//6th bit transfered into VF
	setflag(VF, ((m[parameter] & (1 << VF)) > 0));
	goto next_instruction;
	
BCC: //BCC... branch on carry clean (CF == 0)
	if((flags & (1 << CF)) == 0){
		pc = parameter;
		cycles++;
	}
        goto next_instruction;

BCS: //BCS... branch on carry set (CF == 1)
        if((flags & (1 << CF)) == (1 << CF)){
                pc = parameter;
		cycles++;
        }
        goto next_instruction;


BNE: //Branch on result not zero (ZF == 0)
	if((flags & (1 << ZF)) == 0){
		pc = parameter;
		cycles++;
	}
        goto next_instruction;

BEQ: //Branch on result zero (ZF == 1)
        if((flags & (1 << ZF)) == (1 << ZF)){
                pc = parameter;
		cycles++;
        }
        goto next_instruction;

BMI: //BMI... Branch on result Minus (NF == 1)
	if((flags & (1 << NF)) == (1 << NF)){
		pc = parameter;
		cycles++;
	}
	goto next_instruction;

BPL: //BPL... Branch on Result Plus (NF == 0) 
	if((flags & (1 << NF)) == 0){
                pc = parameter;
		cycles++;
        }
        goto next_instruction;

BVC: //BVC... Branch on Overflow Clear (VF == 0)
	if((flags & (1 << VF)) == 0){
                pc = parameter;
		cycles++;
        }
        goto next_instruction;

BVS: //BVS... Branch on Overflow Set (VF == 1)
	if((flags & (1 << VF)) == (1 << VF)){
                pc = parameter;
		cycles++;
        }
        goto next_instruction;
	
CLC: //CLC... clear carry flag
	setflag(CF, 0);
	goto next_instruction;

CLD: //CLD... clear decimal mode
	setflag(DF, 0);
	goto next_instruction;

SEC: //SEC... Set Carry Flag
	setflag(CF, 1);
	goto next_instruction;

SED: //SED... set decimal flag
	setflag(DF, 1);
	goto next_instruction;

CLV: //CLV... clear overflow flag
	setflag(VF, 0);
	goto next_instruction;

BRK: //BRK is a software interrupt
     	//read next instruction byte and throw away
	pc++;
	//set BF
	setflag(BF, 1);
	setflag(XX, 1);
	//push pc
	//push MSB of PC on Stack
	push8(((pc & 0xFF00) >> 8));
        //write8(0x0100 + r[RS], (uint8_t) ((pc & 0xFF00) >> 8));
        //push LSB of PC on Stack
        //write8(0x0100 + r[RS], (uint8_t) (pc & 0x00FF));
        push8((pc & 0x00FF)); 
	//push processor status
	//write8(0x0100 + r[RS], flags);
	push8(flags);
	//set interrupt
        setflag(IF, 1);
	//fetch content of FFFE and FFFF as new pc
	pc = m[0xFFFE] | (m[0xFFFF] << 8);
	goto next_instruction;
	//return 0;

JMP: //Jump to address
	pc = parameter;
	goto next_instruction;

JSR: //Jump to subroutine
	//set pc to last byte of instruction
	pc = pc - 1;
	//push high byte of PC on Stack
	push8((pc >> 8));
	//write8(0x0100 + r[RS], (uint8_t) (pc >> 8));
	//push low byte of PC on Stack
	//write8(0x0100 + r[RS], (uint8_t) (pc & 0x00FF));
	push8((pc & 0x00FF));
	//set pc to address of subroutine
	pc = parameter;
	goto next_instruction;

PHA: //PusH Accumulator
	//push register on stack
	push8(r[RA]);
	//continue with next instruction
	goto next_instruction;

PLA: //PuLl Accumulator
	//pop value from stack
	r[RA] = pull8();
	//set flags
	setflag(ZF, (r[RA] == 0));
        setflag(NF,((r[RA] & 0x80) > 0));
	//continue with next instruction
	goto next_instruction;

PHP: //PusH Processor status
	//set bit 5 to 1
        setflag(XX, 1);
	setflag(BF, 1);
	//push flags (variable for processor status) on stack
        push8(flags);
        //continue with next instruction
        goto next_instruction;

PLP: //PuLl Processor status
        //pop value from stack
        flags = pull8();
	//reset XX flag
	setflag(XX, 0);
	//ignore BF
	setflag(BF, 0);
        //continue with next instruction
        goto next_instruction;

STA: //STA... store accumulator in memory
     	if((code[opcode].addressingMode != &&zeropage) || (code[opcode].addressingMode != &&absolute)){
		//read from effective address
		read8((0x00 + parameter));
	}
	write8(parameter, r[RA]);
	goto next_instruction;

STX: //STX... store Index X in memory
        if(code[opcode].addressingMode == &&zeropage_y){
                //read from effective address
                read8((0x00 + parameter));
        }

	write8(parameter, r[RX]);
        goto next_instruction;

STY: //STY... store Index Y in memory
        if(code[opcode].addressingMode == &&zeropage_x){
                //read from effective address
                read8((0x00 + parameter));
        }

	write8(parameter, r[RY]);
        goto next_instruction;

TAX: // Transfer Accumulator to Index X
	r[RX] = r[RA];
	setflag(ZF, (r[RX] == 0x00));
        setflag(NF, ((r[RX] & 0x80) > 0));
        goto next_instruction;

TAY:// Transfer Accumulator to Index Y
        r[RY] = r[RA];
        setflag(NF, ((r[RY] & 0x80) > 0));
        goto next_instruction;

TXA: // Transfer Index X to  Accumulator
        r[RA] = r[RX];
        setflag(ZF, (r[RA] == 0x00));
        setflag(NF, ((r[RA] & 0x80) > 0));
        goto next_instruction;

TYA:// Transfer Index Y to  Accumulator
        r[RA] = r[RY];
        setflag(ZF, (r[RA] == 0x00));
        setflag(NF, ((r[RA] & 0x80) > 0));
        goto next_instruction;

TSX: //Transfer Stackpointer to X
	//load RS in RX
	r[RX] = r[RS];
	//set flags for RX
	setflag(ZF, (r[RX] == 0x00));
        setflag(NF, ((r[RX] & 0x80) > 0));
	//continue with next instruction
	goto next_instruction;

TXS: //Transfer X to Stackpointer
	//load RX in RS
        r[RS] = r[RX];
        //continue with next instruction
        goto next_instruction;

RTS: // return from subroutine
	//pop low byte of old pc
	pc = pull8();
	pc |= (pull8() << 8);
	//increment pc to get to next instruction 
	//(pc currently points at last byte of last instruction)
	pc++;
	//continue execution with old pc
	goto next_instruction;

CLI: //Clear Interrupt Disable Bit
	setflag(IF, 0);
      	goto next_instruction;

SEI: //Set Interrupt Disable Status
	setflag(IF, 1);
	goto next_instruction;

RTI: //Return from Interrupt
	//pull flags register from Stack
	flags = pull8();
	//ignore BF
	setflag(BF, 0);
	//pull LSB of PC from Stack
	pc = pull8();
	//pull MSB of PC from Stack
	pc |= (pull8() << 8);
	//continue execution with next operation
	goto next_instruction;

NOP:
	goto next_instruction;

/* "Illegal" Opcodes */

ALR: //A AND operand + LSR
	//AND operation
	r[RA] = r[RA] & parameter;
        setflag(ZF, (r[RA] == 0x00));
        setflag(NF, (r[RA] & (1 << NF)));

	//LSR
	temp = r[RA] >> 1;
        setflag(CF, ((r[RA] & 0x0001) == 0x0000));
	r[RA] = (uint8_t) (temp & 0x00FF);

	goto next_instruction;

ANC: //A AND operand + set C as ASL
	//A AND operand
	r[RA] = r[RA] & parameter;
	setflag(ZF, (r[RA] == 0x00));
        setflag(NF, (r[RA] & (1 << NF)));

	//bit(7) -> C
	setflag(CF, ((r[RA] & 0x80) > 0));

	goto next_instruction;

ANC2: // A AND operand + set C as ROL
	//A AND operand
        r[RA] = r[RA] & parameter;
        setflag(ZF, (r[RA] == 0x00));
        setflag(NF, (r[RA] & (1 << NF)));

        //bit(7) -> C
        setflag(CF, ((r[RA] & 0x80) > 0));
	//set C as ROL
	r[RA] |= (flags & (1 << CF));

        goto next_instruction;	

ARR: //A AND operand + ROR
	//A AND operand
	r[RA] = r[RA] & parameter;
        setflag(ZF, (r[RA] == 0x00));
        setflag(NF, (r[RA] & (1 << NF)));
	//set V flag
	temp = (r[RA] & parameter) + parameter;
	setflag(ZF, ((temp & 0x80) > 0));
	//exchange bit 7 with carry
	r[RA] |= ((flags & (1 << CF)) > 0) << 7;
	
	goto next_instruction;

DCP: //DEC operand + CMP oper
	//DEC operand
	write8(parameter, sub(m[parameter], 1));
        setflag(ZF, (m[parameter] == 0));
        setflag(NF, (r[RA] & (1 << NF)));
	//CMP to Accumulator
	setflag(CF, (m[parameter] > r[RA]));

	goto next_instruction;

ISC: //INC operand + SBC operand
	//INC operand
        write8(parameter, add(m[parameter], 1));
        setflag(ZF, (m[parameter] == 0));
        setflag(NF, (r[RA] & (1 << NF)));
        // SBC operand
        r[RA] = r[RA] - m[parameter] - (flags & (1 << CF));

        goto next_instruction;

LAS: //LDA/TSX operand
	//M AND SP
	r[RS] = r[RS] & m[parameter];
	//TSX
	r[RX] = r[RS];

	goto next_instruction;

LAX: //LDA operand + LDX operand
	//LDA
	r[RA] = m[parameter];
	//LDX 
	r[RX] = m[parameter];
	setflag(ZF, (r[RX] == 0x00));
        setflag(NF, (r[RX] & (1 << NF)));

	goto next_instruction;

RLA: //ROL operand + AND operand
	//M <- ROL
	setflag(CF, ((r[parameter] & 0x8) > 0));
	temp = (r[parameter] << 1) | (flags & (1 << CF));
	write8(parameter, (uint8_t) (temp & 0xFF));
	//A AND M -> A
	r[RA] = temp & r[RA];
	setflag(ZF, (r[RA] == 0x00));
        setflag(NF, (r[RA] & (1 << NF)));
	
	goto next_instruction;

RRA: //ROR operand + ADC Operand
	//M <- ROR
        setflag(CF, ((r[parameter] & 0x1) > 0));
        temp = (r[parameter] >> 1) | ((flags & (1 << CF)) << 7);
        write8(parameter, (uint8_t) (temp & 0xFF));
        //A + M -> A
        r[RA] = temp + r[RA];
        setflag(ZF, (r[RA] == 0x00));
        setflag(NF, (r[RA] & (1 << NF)));
	
	goto next_instruction;

SAX: // A and X are put on bus at the same time -> like A AND X
	write8(parameter, (r[RA] & r[RX]));
	goto next_instruction;

SBX: //CMP and DEC at onces
	//(A AND X) - operand -> X
	r[RX] = (r[RA] & r[RX]) - parameter;
	setflag(ZF, (r[RX] == 0x00));
        setflag(NF, (r[RX] & (1 << NF)));
	setflag(CF, (r[RX] > parameter));
	goto next_instruction;

SLO: //ASL operand + ORA operand
	temp = (m[parameter] << 1);
	write8(parameter, (uint8_t) (temp & 0xFF));
	setflag(CF,((temp & 0x80) > 0));
	r[RA] = r[RA] | (uint8_t) (temp & 0xFF);
	setflag(ZF, (r[RA] == 0x00));
        setflag(NF, (r[RA] & (1 << NF)));

	goto next_instruction;

SRE: //LSR operand + EOR operand
	temp = (m[parameter] >> 1);
        write8(parameter, (uint8_t) (temp & 0xFF));
        setflag(CF, ((temp & 0x01) > 0));

        r[RA] = r[RA] ^ (uint8_t) (temp & 0xFF);
	setflag(ZF, (r[RA] == 0x00));
        setflag(NF, (r[RA] & (1 << NF)));

        goto next_instruction;

USBC: //SBC + NOP
	r[RA] = r[RA] - parameter - (flags & (1 << CF));
	setflag(ZF, (r[RA] == 0x00));
        setflag(NF, (r[RA] & (1 << NF)));
	
	//NOP
	pc += 2;

	goto next_instruction;

JAM: //freeze the CPU with $FF on data bus
     return 0;


     /* interrupt handler */
irq: //Interrupt request
	//push MSB of pc on stack, decrement SP
	write8(0x0100 + r[RS], (uint8_t)(pc & 0xFF00));
	r[RS]--;
	//push LSB of pc on stack, decrement SP
	write8(0x0100 + r[RS], (uint8_t)(pc & 0x00FF));
        r[RS]--;
	//set pc to IRQ vector FFFE/FFFF
	pc = (m[0xfff] << 8) | m[0xfffe];
	//execute IRQ routine (at address $ff48)
	opcode = m[pc];
        goto *code[opcode].addressingMode;

nmi: //non-maskable interrupt
	//push MSB of pc on stack, decrement SP
        write8(0x0100 + r[RS], (uint8_t)(pc & 0xFF00));
        r[RS]--;
        //push LSB of pc on stack, decrement SP
        write8(0x0100 + r[RS], (uint8_t)(pc & 0x00FF));
        r[RS]--;
        //set pc to IRQ vector FFFA/FFFB
        pc = (m[0xffb] << 8) | m[0xfffa];
        //execute IRQ routine (at address $ff48)
        opcode = m[pc];
        goto *code[opcode].addressingMode;


}

int
load_into_memory(uint8_t program[], int size, int offset){
        for(int i = 0; i < size; i++){
                // 0xFFFC - 0x1 = 0xFFFB (CLC can be 1 Bytes before begin of program)
                m[i + offset] = program[i];
        }
        //set reset vector to start executing code at 0x0100 (LE)
        m[0xFFFC] = (offset & 0xFF);
        m[0xFFFD] = (offset >> 8);

        return 0;
}

void
load_program_from_file(char* filename, int size, int offset){
        int fd = open(filename, O_RDONLY);
        if (fd < 0) { perror("open"); exit(1); }
        int n = read(fd, m+offset, 65536);
        m[0xfffc] = offset & 0xff;
        m[0xfffd] = offset >> 8;
        printf("%d bytes gelesen\n", n);
}

int
main (void){
	// int8_t program[] = {0xA9, 0xAA, 0x2A, 0x6A, 0x00};
	// load_into_memory(program, 5);

	load_program_from_file("../test_better/TTL6502.BIN", 65536, 0xe000);
	
	return emulate_6502();

//	return 0;
}
