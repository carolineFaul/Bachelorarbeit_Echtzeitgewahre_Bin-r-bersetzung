#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "6502_instructions.h"

#define MEMORY 1024 * 64
#define ROM_SIZE 1024 * 5
#define MAX_ROM 0xffff
#define MIN_ROM 0xf000
#define MAX_RRIOT_ROM 0x8fff
#define MIN_RRIOT_ROM 0x8c00
#define RAM_SIZE 256
#define MIN_IO 0x8b00
#define MAX_IO 0x8b7f
//0xfe2f
#define END_PROGRAM 0xfe2f
#define IO_START 0xfeb6
#define SKIP_BYTE1 0xfeed
#define SKIP_BYTE2 0xff03
#define SKIP_BYTE3 0xff7c
#define START_TABLE 0xffc6

/* memory addressable by 6502 */
uint8_t m[MEMORY];
/* flag bit numbers */
enum { CF=0, ZF, IF, DF, BF, XX, VF, NF };

/* table for opcodes, addressing mode and cycles */
struct Instructions code[256] = {
		{&BRK, 0x8, 7},{&ORA, 0x1, 6},{&JAM, 0x8, 1},{&SLO, 0x1, 8},{&NOP, 0x5, 3},{&ORA, 0x5, 3},{&ASL, 0x5, 5},{&SLO, 0x5, 5},
                {&PHP, 0x8, 3},{&ORA, 0x9, 2},{&ASL, 0xa, 2},{&ANC, 0x9, 2},{&NOP, 0xd, 4},{&ORA, 0xd, 4},{&ASL, 0xd, 6},{&SLO, 0xd, 6},

                {&BPL, 0x0, 2},{&ORA, 0x11, 5},{&JAM, 0x8, 1},{&SLO, 0x11, 8},{&NOP, 0x15, 4},{&ORA, 0x15, 4},{&ASL, 0x15, 6},{&SLO, 0x15, 6},
                {&CLC, 0x8, 2},{&ORA, 0x19, 4},{&NOP, 0x8, 2},{&SLO, 0x19, 7},{&NOP, 0x1d, 4},{&ORA, 0x1d, 4},{&ASL, 0x1d, 7},{&SLO, 0x1d, 6},

                {&JSR, 0xd, 6},{&AND, 0x1, 6},{&JAM, 0x8, 1},{&RLA, 0x1, 8},{&BIT, 0x5, 3},{&AND, 0x5, 3},{&ROL, 0x5, 5},{&RLA, 0x5, 5},
                {&PLP, 0x8, 4},{&AND, 0x9, 4},{&ROL, 0xa, 2},{&ANC2, 0x9, 2},{&BIT, 0xd, 4},{&AND, 0xd, 4},{&ROL, 0xd, 6},{&RLA, 0xd, 6},

                {&BMI, 0x0, 2},{&AND, 0x11, 5},{&JAM, 0x8, 1},{&RLA, 0x11, 8},{&NOP, 0x15, 4},{&AND, 0x15, 4},{&ROL, 0x15, 6},{&RLA, 0x15, 5},
                {&SEC, 0x8, 2},{&AND, 0x19, 4},{&NOP, 0x8, 2},{&RLA, 0x19, 7},{&NOP, 0x1d, 4},{&AND, 0x1d, 4},{&ROL, 0x1d, 7},{&RLA, 0x1d, 7},

                {&RTI, 0x8, 6},{&EOR, 0x1, 6},{&JAM, 0x8, 1},{&SRE, 0x1, 8},{&NOP, 0x5, 3},{&EOR, 0x5, 3},{&LSR, 0x5, 5},{&SRE, 0x5, 5},
                {&PHA, 0x8, 3},{&EOR, 0x9, 2},{&LSR, 0xa, 2},{&ALR, 0x9, 2},{&JMP, 0xd, 3},{&EOR, 0xd, 4},{&LSR, 0xd, 7},{&SRE, 0xd, 6},

                {&BVC, 0x0, 2},{&EOR, 0x11, 5},{&JAM, 0x8, 1},{&SRE, 0x11, 8},{&NOP, 0x15, 4},{&EOR, 0x15, 4},{&LSR, 0x15, 6},{&SRE, 0x15, 6},
                {&CLI, 0x8, 2},{&EOR, 0x19, 4},{&NOP, 0x8, 2},{&SRE, 0x19, 7},{&NOP, 0x1d, 4},{&EOR, 0x1d, 4},{&LSR, 0x1d, 7},{&SRE, 0x1d, 7},

                {&RTS, 0x8, 6},{&ADC, 0x1, 6},{&JAM, 0x8, 1},{&RRA, 0x1, 8},{&NOP, 0x5, 3},{&ADC, 0x5, 3},{&ROR, 0x5, 5},{&RRA, 0x5, 5},
                {&PLA, 0x8, 4},{&ADC, 0x9, 2},{&ROR, 0xa, 2},{&ARR, 0x9, 2},{&JMP, 0x2d, 5},{&ADC, 0xd, 4},{&ROR, 0xd, 6},{&RRA, 0xd, 6},

                {&BVS, 0x0, 2},{&ADC, 0x11, 5},{&JAM, 0x8, 1},{&RRA, 0x11, 8},{&NOP, 0x15, 4},{&ADC, 0x15, 4},{&ROR, 0x15, 6},{&RRA, 0x15, 6},
                {&SEI, 0x8, 2},{&ADC, 0x19, 4},{&NOP, 0x8, 2},{&RRA, 0x19, 7},{&NOP, 0x1d, 4}, {&ADC, 0x1d, 4},{&ROR, 0x1d, 7},{&NOP, 0x1d, 7},

                {&NOP, 0x9, 2},{&STA, 0x1, 6},{&JAM, 0x9, 2},{&SAX, 0x11, 6},{&STY, 0x5, 3},{&STA, 0x5, 3},{&STX, 0x5, 3},{&SAX, 0x5, 3},
                {&DEY, 0x8, 2},{&NOP, 0x9, 2},{&TXA, 0x8, 2},{&NOP, 0x8, 1},{&STY, 0xd, 4},{&STA, 0xd, 4},{&STX, 0xd, 4},{&SAX, 0xd, 4},

                {&BCC, 0x0, 2},{&STA, 0x11, 6},{&JAM, 0x8, 1},{&NOP, 0x8, 1},{&STY, 0x15, 4},{&STA, 0x15, 4},{&STX, 0x6, 4},{&SAX, 0x15, 4},
                {&TYA, 0x8, 2},{&STA, 0x19, 5},{&TXS, 0x8, 2},{&NOP, 0x8, 1},{&NOP, 0x8, 1},{&STA, 0x1d, 5},{&NOP, 0x8, 1},{&NOP, 0x8, 1},

                {&LDY, 0x9, 2},{&LDA, 0x1, 6},{&LDX, 0x9, 2},{&LAX, 0x1, 6},{&LDY, 0x5, 3},{&LDA, 0x5, 3},{&LDX, 0x5, 3},{&LAX, 0x5, 3},
                {&TAY, 0x8, 2},{&LDA, 0x9, 2},{&TAX, 0x8, 2},{&NOP, 0x8, 1},{&LDY, 0xd, 4},{&LDA, 0xd, 4},{&LDX, 0xd, 4},{&LAX, 0xd, 4},

                {&BCS, 0x0, 2},{&LDA, 0x11, 5},{&JAM, 0x8, 1},{&LAX, 0x11, 5},{&LDY, 0x15, 4},{&LDA, 0x15, 4},{&LDX, 0x6, 4},{&LAX, 0x6, 4},
                {&CLV, 0x8, 2},{&LDA, 0x19, 4},{&TSX, 0x8, 2},{&LAS, 0x19, 4},{&LDY, 0x1d, 4},{&LDA, 0x1d, 4},{&LDX, 0x19, 4},{&LAX, 0x19, 4},

                {&CPY, 0x9, 2},{&CMP, 0x1, 6},{&JAM, 0x9, 2},{&DCP, 0x1, 8},{&CPY, 0x5, 3},{&CMP, 0x5, 3},{&DEC, 0x5, 5},{&DCP, 0x5, 5},
                {&INY, 0x8, 2},{&CMP, 0x9, 2},{&DEX, 0x8, 2},{&SBX, 0x9, 2},{&CPY, 0xd, 4},{&CMP, 0xd, 4},{&DEC, 0xd, 6},{&DCP, 0xd, 6},

                {&BNE, 0x0, 2},{&CMP, 0x11, 5},{&JAM, 0x8, 1},{&DCP, 0x11, 8},{&NOP, 0x15, 4},{&CMP, 0x15, 4},{&DEC, 0x15, 6},{&DCP, 0x15, 6},
                {&CLD, 0x8, 2},{&CMP, 0x19, 4},{&NOP, 0x8, 2},{&DCP, 0x19, 7},{&NOP, 0x1d, 4},{&CMP, 0x1d, 4},{&DEC, 0x1d, 7},{&DCP, 0x1d, 7},

                {&CPX, 0x9, 2},{&SBC, 0x1, 6},{&NOP, 0x9, 2},{&ISC, 0x1, 8},{&CPX, 0x5, 3},{&SBC, 0x5, 3},{&INC, 0x5, 5},{&ISC, 0x5, 5},
                {&INX, 0x8, 2},{&SBC, 0x9, 2},{&NOP, 0x8, 2},{&USBC, 0x9, 2},{&CPX, 0xd, 4},{&SBC, 0xd, 4},{&INC, 0xd, 6},{&ISC, 0xd, 6},

                {&BEQ, 0x0, 2},{&SBC, 0x11, 5},{&JAM, 0x8, 1},{&ISC, 0x11, 8},{&NOP, 0x15, 4},{&SBC, 0x15, 4},{&INC, 0x15, 6},{&ISC, 0x15, 6},
                {&SED, 0x8, 2},{&SBC, 0x19, 4},{&NOP, 0x8, 2},{&ISC, 0x19, 7},{&NOP, 0x1d, 4},{&SBC, 0x1d, 4},{&INC, 0x1d, 7},{&ISC, 0x1d, 7}
};

/* Variablen for Communication with functions for opcodes */
enum{DEFS, IR, BYTES};

uint16_t pc;
int toSet;
uint8_t defs;
uint8_t uses;
uint8_t usedRegisters;
uint8_t optimization;
char cRepresentation [1024];
void (*used_helper_functions[9])(void) = {NULL};
uint16_t cycles;

extern uint16_t parameter;
extern uint8_t bytes;
//extern int dynamic;
uint8_t bcd;
extern uint16_t rom_addr;
extern uint16_t rriot_addr;
extern uint16_t jsr_counter;
extern uint16_t jsr[2048];
/* Functions for analysing loaded binary */

typedef struct{
	//start address
	uint16_t start;
	//end address
	uint16_t end;
	//cycles
	int cycles;
	//instructions
	int instructions;
	//Gen (used flags) for flag optimization
	uint8_t gen;
       	//Kill (defined flags) for flag optimization
	uint8_t kill;
	//uses per instruction
	uint8_t uses[32];
	//defs per instruction
	uint8_t defs[32];
}CodeBlock;

//stuff in RAM treaten differently
CodeBlock codeblocks[ROM_SIZE];
uint16_t codeBlockCapacity;
uint16_t startBlock;
uint16_t endBlock;

uint16_t
resolve_address_to_index_in_codeblocks(uint16_t address){
	//check if address is in ROM or RRIOT ROM
	if((address <= MAX_ROM) && (address >= MIN_ROM)){
		//address in ROM
		return address ^ MIN_ROM;
	}
	return -1;
}

//identify leaders: leader == Label or first instruction of code block
//identify end of CodeBlock: branch or jump
//uint16_t branch_addresses[2048];
//int number_of_branches;

uint16_t leader_addresses[2048];
int number_of_leaders;

uint16_t basicblock_startaddresses[2048];
int number_of_basicblocks;

uint16_t io_operations[2048];
int IOops;

uint16_t dynamic_targets[RAM_SIZE];
int dynamicTargetAmount;

//direction [from] -> [to]
uint8_t adjacencyMatrix[ROM_SIZE][ROM_SIZE] = {{0, 0}}; 

int
is_leader(uint16_t address){
        for(int i = 0; i < number_of_leaders; i++){
                if(leader_addresses[i] == address){
                        return i;
                }
        }
        return -1;
}

int is_branch_instruction(uint8_t opcode){
        if((opcode & 0xf) == 0){
                uint8_t hi = opcode >> 4;
                if(hi % 2 == 1){
                        return 1;
                }
        }
        return 0;
}

uint8_t
is_jump(uint8_t opcode){
	return (opcode == 0x4C) || (opcode == 0x6C) || (opcode == 0x20);
}

/* adds I/O Opertaios to a list of I/O Operations*/
uint8_t
is_in_io_operations(uint16_t address){
        for(int i = 0; i < IOops; i++){
                if(io_operations[i] == address){
                        return 1;
                }
        }
        return 0;

}

uint8_t
check_if_IO_reachable(uint16_t address){
	//IO Addresses can be rearched if difference between address without index and IO address space is representable as 8-Bit 2's complement value
	int16_t requiredIndex;
	if(address > MAX_IO){
        	//2's complement could be used to reach IO addresses
                requiredIndex = MAX_IO - address;
	}else if(address < MIN_IO){
		//IO addresses could be reached adding a positive 8 bit integer
		requiredIndex = MIN_IO - address;
	}
	//check if difference is in range of 8-Bit 2's complement
        return (requiredIndex >= -128) && (requiredIndex <= 127);
}

uint8_t 
is_io_instruction(uint8_t opcode){
	if(is_in_io_operations(pc)){
                return 1;
        }

        uint8_t hi = opcode >> 4;
        uint16_t address;
        if((hi == 0xa) || (hi == 0xb) || (hi == 0x8) || (hi == 0x9)){
                uint8_t low = opcode & 0xf;
                if((low == 0xa) || (low == 0x8)){
                        return 0;
                }else if((opcode == 0xb0) || (opcode == 0x90)){
                        //BCS, BCC
                        return 0;
                }
                //only absolute possible or absolute with index
		if(code[opcode].addressingMode == 0xd){
                	address = m[pc+1] | ((uint16_t) m[pc+2] << 8);
			return (address >= MIN_IO) && (address <= MAX_IO);

                }else if((code[opcode].addressingMode == 0x1d) || (code[opcode].addressingMode == 0x15)){
			//addressing mode is indexed by register and can potentially reach IO addresses
			address = m[pc+1] | ((uint16_t) m[pc+2] << 8);
			return ((address >= MIN_IO) && (address <= MAX_IO)) || check_if_IO_reachable(address);
		}
        }
        return 0;
}

void
add_io_instruction(uint16_t address){
	if(!is_in_io_operations(address)){
		io_operations[IOops] = address;
        	IOops++;
	}
}

void
get_used_helperfunctions(uint8_t opcode){
	toSet = DEFS;
	(*code[opcode].opcode)();
	toSet = BYTES;
}

void
get_next_instruction(void){
        toSet = BYTES;
        call_corresponding_addressingMode(m[pc]);
        pc += bytes;
        toSet = IR;
}

/* add leaders of hyperblocks to leader_address list and complete adjacent list*/
void
add_branch_and_leader(uint16_t branch, uint16_t leader){

	uint16_t index = resolve_address_to_index_in_codeblocks(leader);	
	codeblocks[index].start = leader;
	if(is_leader(leader) == -1){
		leader_addresses[number_of_leaders] = leader;
		number_of_leaders++;
	}
}

/* Identifies Leaders of Hyperblocks */
uint16_t
find_leaders_and_branches(void){
	pc = m[0xfffc] | ((uint16_t) m[0xfffd] << 8);
	bcd = 0;

	//first address is also leader
	uint16_t index = resolve_address_to_index_in_codeblocks(pc);
        codeblocks[index].start = pc;
	leader_addresses[number_of_leaders] = pc;
        number_of_leaders++;
	toSet = BYTES;

	while(pc < MEMORY){
		if(pc == END_PROGRAM){
			//0xfffa, 0xfffb, 0xfffc, 0xfffd, 0xfffe, 0xffff are vectors
			pc = IO_START;
		}else if(pc == START_TABLE){
			return pc;
		}
		
		uint8_t opcode = m[pc];
		//resolve parameters and instruction length by calling Adressing Mode 
		call_corresponding_addressingMode(opcode);
                
                if(is_branch_instruction(opcode)){
			//contitional branch found
			add_branch_and_leader(pc, parameter);
		 }else if(opcode == 0x6C){
                        //JMP indirect found
                        uint16_t vector = m[pc + 1] | ((uint16_t) m[pc + 2]);
                        if(((vector <= MAX_ROM) && (vector >= MIN_ROM)) || ((vector <= MAX_RRIOT_ROM) && (vector >= MIN_RRIOT_ROM))){
                                //target is a leader
                                add_branch_and_leader(pc, parameter);
                        }else{
                                //vector in RAM
                                exit(1);
                        }
		}else if(is_jump(opcode)){
			//JSR or JMP found
                        add_branch_and_leader(pc, parameter);
		} 

		get_used_helperfunctions(opcode);
		pc += bytes;
	}
	return pc;
}

void
add_basicBlock_to_basicblock_list(uint16_t address){
	for(int i = 0; i < number_of_basicblocks; i++){
		if(basicblock_startaddresses[i] == address){
			return;
		}
	}
	basicblock_startaddresses[number_of_basicblocks] = address;
	number_of_basicblocks++;
}

uint16_t
add_basicblock(uint16_t index, uint8_t opcode){

	codeblocks[index].end = pc;
	uint16_t next = 0;

	if(is_branch_instruction(opcode) || is_io_instruction(opcode)){
		//set arrow to next instruction
		next = resolve_address_to_index_in_codeblocks(pc + bytes);
		adjacencyMatrix[index][next] = 1;
	}

	if(is_branch_instruction(opcode) || is_jump(opcode)){
		//get target and set arrow in adjecent matrix
		next = resolve_address_to_index_in_codeblocks(parameter);
		adjacencyMatrix[index][next] = 1;
	}

	//change pc to start of next basic block
	pc += bytes;
	//add new basic block to list
	add_basicBlock_to_basicblock_list(pc);
	
	index = resolve_address_to_index_in_codeblocks(pc);
        codeblocks[index].start = pc;
	return index;
}

void
complete_block(void){
	uint16_t start = m[0xfffc] | ((uint16_t) m[0xfffd] << 8);
        uint16_t old_pc = start;
	toSet = BYTES;
	number_of_basicblocks = 0;

	//init variables for control flow graph
	startBlock = start;
	endBlock = start;

        for(int i = 0; i < number_of_leaders; i++){
		pc = leader_addresses[i];
		start = pc;

		int index = resolve_address_to_index_in_codeblocks(pc);
		int old_index = index;

		add_basicBlock_to_basicblock_list(pc);
                codeblocks[index].start = pc;
		goto next;
		
		while(is_leader(pc) == -1){
next:
			old_pc = pc;
			if(index != old_index){
				if(pc >= endBlock){
                 		       endBlock = pc;
                		}
				old_index = index;
			}

			uint8_t opcode = m[pc];
                        codeblocks[index].cycles += code[opcode].cycles;
                        codeblocks[index].instructions++;	
			
			call_corresponding_addressingMode(opcode);

			if(is_branch_instruction(opcode)){
				//add penality for branch taken and crossing page boundary
#if WCET
				if((parameter & 0xff00) == (pc & 0xff00)){
					//page boundary not crossed
					codeblocks[index].cycles += 1;
				}else{
					//page boundary crossed
					codeblocks[index].cycles += 2;
				}
#endif
				index = add_basicblock(index, opcode);
				continue;
			}else if(is_io_instruction(opcode)){
				add_io_instruction(pc);
				index = add_basicblock(index, opcode);
				continue;
			}else if(is_jump(opcode)){
				index = add_basicblock(index, opcode);
				continue;
			}else if((opcode == 0x00) && (m[pc+1] == 0)){
                                break;
			}else{
				pc += bytes;
			}
		}
		codeblocks[index].end = old_pc;
		
		adjacencyMatrix[index][resolve_address_to_index_in_codeblocks(pc)] = 1;
		
	}
}

/* Code for Optimization */
void
analyse_uses_and_defs_code_block(void){
	toSet = DEFS;
	uint8_t used_registers = 0;

        for(int i = 0; i < number_of_basicblocks; i++){
		pc = basicblock_startaddresses[i];
		
		if(pc == END_PROGRAM){
                        //0xfffa, 0xfffb, 0xfffc, 0xfffd, 0xfffe, 0xffff are vectors
                        continue;
                }else if(pc == START_TABLE){
                        continue;
                }

		uint16_t index = resolve_address_to_index_in_codeblocks(pc);
		
		for(int j = 0; j < codeblocks[index].instructions; j++){
                        (*code[m[pc]].opcode)();
                        codeblocks[index].uses[j] = uses;
                        codeblocks[index].defs[j] = defs;

			used_registers |= usedRegisters;
			usedRegisters = 0;

			toSet = BYTES;
		        call_corresponding_addressingMode(m[pc]);
        		pc += bytes;
			toSet = DEFS;

                }

		for(int j = codeblocks[index].instructions - 1; j >= 0; j--){
                        //backwards: calculating gen and kill 
                        codeblocks[index].gen = codeblocks[index].uses[j] | (codeblocks[index].gen & ~(codeblocks[index].defs[j]));
                        //Kill
                        codeblocks[index].kill = codeblocks[index].defs[j] | codeblocks[index].kill;
                }

	}
	usedRegisters = used_registers;
}

uint8_t changed;

void
compute_LV(int index, uint8_t checked[], int checked_blocks){
	uint16_t queue[number_of_basicblocks];
	uint8_t succs[number_of_basicblocks-1];
        int next = 0;
	int successors = 0;

	//get previous code blocks and successors by checking Adjacency Matrix
	for(int i = startBlock; i <= endBlock; i++){
        	uint16_t candidate = resolve_address_to_index_in_codeblocks(i);
               	if(adjacencyMatrix[candidate][index] == 1){
               		queue[next] = candidate;
                       	next++;
               	}
		if(adjacencyMatrix[index][candidate] == 1){
			succs[successors] = candidate;
			successors++;
		}
        }

	//apply Formula wcre_02, S. 5
        uint8_t lv = 0;
        for(int i = 0; i < successors; i++){
                //U_{s elem succ(u)} LV(s)
                lv |= codeblocks[succs[i]].gen;
        }
        //(Gen_{succs}-def(u)) u uses(u)
	uint8_t temp = codeblocks[index].gen;
        codeblocks[index].gen |= (lv & ~(codeblocks[index].kill));
	if(temp != codeblocks[index].gen){
		//result not stabel
		changed = 1;
	}
 	//continue with next block
	if(codeblocks[index].start == startBlock){
		//Abbruch Rekursion
		return;
	}else{
		checked[checked_blocks] = index;
		checked_blocks++;
		int seen = 0;
		//continue to compute others
		for(int i = 0; i < next; i++){
			for(int j = 0; j < checked_blocks; j++){
				if(queue[i] == checked[j]){
					seen = 1;
					break;
				}
			}
			if(!seen){
				compute_LV(queue[i], checked, checked_blocks);
			}
		}
	}
}

void
compute_optimization(void){
	uint8_t checked[number_of_basicblocks];
	int checked_blocks = 0;

	changed = 1;
	while(changed != 0){
		changed = 0;
		compute_LV(resolve_address_to_index_in_codeblocks(endBlock), checked, checked_blocks);
	}
	//compute_LV(resolve_address_to_index_in_codeblocks(endBlock), checked, checked_blocks);
}

/* Code for printing */
uint8_t
is_branch(uint16_t pc){
	return is_branch_instruction(m[pc]) || is_jump(m[pc]);
}

void
print_used_helper_function_by_program(void){
        int i = 0;
        while(used_helper_functions[i] != NULL){
                (*used_helper_functions[i])();
                fprintf(stdout, "\n\n");
                i++;
        }
}

void
set_needed_flags(uint8_t index){
	//get defs and uses of next instructions
	uint8_t defsNextInstructions = 0;
	uint8_t usesNextInstructions = 0;
	toSet = BYTES;
	
	uint16_t start = 0;
	uint16_t i = codeblocks[index].start;

	while((i <= codeblocks[index].end) && (pc != i)){
		call_corresponding_addressingMode(m[i]);
		i += bytes;
		start++;
	}

	for(int i = start+1; i < codeblocks[index].instructions; i++){
		defsNextInstructions |= codeblocks[index].defs[i];
		usesNextInstructions |= codeblocks[index].uses[i];

	}
	//get uses by next code block
	call_corresponding_addressingMode(m[codeblocks[index].end]);
	uint16_t addressNextBlock = codeblocks[index].end + bytes;
	uint8_t nextBlock = resolve_address_to_index_in_codeblocks(addressNextBlock);
	//calculate flags killed by current intruction UNION (flags used by next code block UNION next instructions - flags killed by next instructions)
	//defs = codeblocks[index].kill & ((usesNextInstructions | codeblocks[nextBlock].gen) & ~(defsNextInstructions));
	defs = codeblocks[index].defs[start] & ((usesNextInstructions | codeblocks[nextBlock].gen) & ~(defsNextInstructions));
		
	//set for AVR Optimization also used flags by next instructions and block in usese Variable for elimitating set_missing_flags optimization
	uses = ((usesNextInstructions | codeblocks[nextBlock].gen) & ~(defsNextInstructions));

	toSet = IR;
}

#if AVR
//in AVR representation jsr contains addresses of targets, in C it contains address of Jumps
uint8_t
is_jsr_target(void){
	for(int i = 0; i < jsr_counter; i++){
		if(jsr[i] == pc){
			return 1;
		}
	}
	return 0;
}
#endif

void
print_code_representation(void){
	toSet = IR;
	pc = m[0xfffc] | ((uint16_t) m[0xfffd] << 8);
	uint16_t index = 0;

        while(pc < MEMORY){
                if(pc == END_PROGRAM){
                        //0xfffa, 0xfffb, 0xfffc, 0xfffd, 0xfffe, 0xffff are vectors
                        pc = IO_START;
                }else if(pc == START_TABLE){
                        return;
                }

                if(is_leader(pc) != -1){
                        fprintf(stdout, "L%x:\n", pc);
                        index = resolve_address_to_index_in_codeblocks(pc);
#if AVR
			if(jsr_counter > 0){
				if(is_jsr_target()){
					printf("\t __asm__ volatile(\"L%x:\");\n", pc);
				}
			}
#endif
                }
		if(is_branch(pc) == 1){
                        cycles = codeblocks[index].cycles;
                        (*code[m[pc]].opcode)();
#if C
			uint8_t opcode = m[pc];
#endif
                        get_next_instruction();

                        /*if(is_branch_instruction(m[pc])){
                                printf("\t cycles += %d;\n", cycles);
                        }*/
                        index = resolve_address_to_index_in_codeblocks(pc);
#if C
			if((opcode == 0x20) && (is_leader(pc) == -1)){
				//print additional label for return from subroutine
				printf("L%x:\n", pc);
			}
#endif
                        continue;
                }else if(is_in_io_operations(pc)){
			cycles = codeblocks[index].cycles - code[m[pc]].cycles;
			if(cycles != 0){
				printf("\t cycles += %d;\n", cycles);
			}
                        (*code[m[pc]].opcode)();
                        printf("\t cycles += %d;\n", code[m[pc]].cycles);
			get_next_instruction();
                        index = resolve_address_to_index_in_codeblocks(pc);
                        continue;
		}else if(m[pc] == 0x60){
                        //RTS
                        printf("\t cycles += %d;\n", codeblocks[index].cycles);
			(*code[m[pc]].opcode)();
                        get_next_instruction();
                        index = resolve_address_to_index_in_codeblocks(pc);
                        continue;
                } 
		if((m[pc] == 0x00) && (m[pc+1] == 0)){
                        printf("\t cycles += %d;\n", codeblocks[index].cycles);
                        return;
                }

                (*code[m[pc]].opcode)();

                if(pc == codeblocks[index].end){
                        printf("\t cycles += %d;\n", codeblocks[index].cycles);
			get_next_instruction();
                        index = resolve_address_to_index_in_codeblocks(pc);
                        continue;
                }
                get_next_instruction();
        }
}

void
print_optimized_code_representation(void){
	toSet = IR;
        pc = m[0xfffc] | ((uint16_t) m[0xfffd] << 8);
        uint16_t index = 0;
        while(pc < MEMORY){
                if(pc == END_PROGRAM){
                        //0xfffa, 0xfffb, 0xfffc, 0xfffd, 0xfffe, 0xffff are vectors
                        pc = IO_START;
                }else if(pc == START_TABLE){
                        return;
                }

                if(is_leader(pc) != -1){
                        fprintf(stdout, "L%x:\n", pc);
                        index = resolve_address_to_index_in_codeblocks(pc);
                        set_needed_flags(index);
#if AVR
                        if(jsr_counter > 0){
                                if(is_jsr_target()){
                                        printf("\t __asm__ volatile(\"L%x:\");\n", pc);
                                }
                        }
#endif

                }
		if(is_branch(pc) == 1){
                        cycles = codeblocks[index].cycles;
                        (*code[m[pc]].opcode)();
#if C
                        uint8_t opcode = m[pc];
#endif

                        get_next_instruction();

                        /*if(is_branch_instruction(m[pc])){
                                printf("\t cycles += %d;\n", cycles);
                        }*/
                        index = resolve_address_to_index_in_codeblocks(pc);
#if C
                        if((opcode == 0x20) && (is_leader(pc) == -1)){
                                //print additional label for return from subroutine
                                printf("L%x:\n", pc);
                        }
#endif

			set_needed_flags(index);
                        continue;
                }else if(is_in_io_operations(pc)){
			cycles = codeblocks[index].cycles - code[m[pc]].cycles;
			if(cycles != 0){
                                printf("\t cycles += %d;\n", cycles);
                        }
                        (*code[m[pc]].opcode)();
                        printf("\t cycles += %d;\n", code[m[pc]].cycles);
                        get_next_instruction();
                        index = resolve_address_to_index_in_codeblocks(pc);
			set_needed_flags(index);
                        continue;
                }else if(m[pc] == 0x60){
			//RTS
			printf("\t cycles += %d;\n", codeblocks[index].cycles);
			(*code[m[pc]].opcode)();
			get_next_instruction();
                        index = resolve_address_to_index_in_codeblocks(pc);
			continue;
		}
		if((m[pc] == 0x00) && (m[pc+1] == 0x00)){
                        printf("\t cycles += %d;\n", codeblocks[index].cycles);
                        return;
                }

		(*code[m[pc]].opcode)();

                if(pc == codeblocks[index].end){
                        printf("\t cycles += %d;\n", codeblocks[index].cycles);
			get_next_instruction();
			index = resolve_address_to_index_in_codeblocks(pc);
			continue;
                }

//		set_needed_flags(index);
                get_next_instruction();
		set_needed_flags(index);
        }

}


void
print_code(uint16_t lastPC){
	toSet = IR;
	cycles = 0;
	print_prolog();

	//printf("#define ROM_START %d\n\n", lastPC);
	
	print_global_vars_and_functions();

	/*uint16_t size = MAX_ROM - MIN_ROM;
	printf("uint8_t rom[%d] = {", size);
	for(int i = 0; i < size-1; i++){
		printf("%d,", m[0xf000 + i]);
	}
	printf("%d};\n\n", m[0xf000 + (size-1)]);*/
	
	/*uint16_t size = MAX_ROM - lastPC;
	printf("uint8_t rom[%d] = {", size);
        for(int i = 0; i < size-1; i++){
                printf("%d,", m[lastPC + i]);
        }
        printf("%d};\n\n", m[0xf000 + (size-1)]);*/

	print_used_helper_function_by_program();
	print_main();

	if(optimization){
		print_optimized_code_representation();
	}else{
		print_code_representation();
	}

	print_epilog();
}

/* Functions for loading memory m addressable by 6502 */

void
load_program_from_file(char* filename, int size, int offset){
        int fd = open(filename, O_RDONLY);
        if (fd < 0) { perror("open"); exit(1); }
        int n = read(fd, m+offset, 65536);
	m[0xfffc] = offset & 0xff;
	m[0xfffd] = offset >> 8;
        fprintf(stderr,"%d bytes gelesen\n", n);
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

int
main(void){
	/* setup of environment */
	toSet = 0;
	codeBlockCapacity = ROM_SIZE;
	//number_of_branches = 0;
        dynamicTargetAmount = 0;
	IOops = 0;
	number_of_leaders = 0;
	jsr_counter = 0;

	rom_addr = 0;
	rriot_addr = 0;

	optimization = 1; // 1 optimization, 0 no optimization
	/* load binary or array into memory */
	//load_program_from_file("RRIOT_ROM", 1024, MIN_RRIOT_ROM);
	
	/* Clock Test */
	//load_program_from_file("clock_new.bin", 4096, 0xf000);
	//load_program_from_file("clock.bin", 4096, 0xf000);
	
	//load_program_from_file("add.bin", 4096, 0xf000);
	//load_program_from_file("display.bin", 4096,0xf000);
	//load_program_from_file("abc_delay.bin", 4096, 0xf000);
	//load_program_from_file("abc_delay_new.bin", 4096, 0xf000);
	
	//load_program_from_file("abc_300.bin", 4096, 0xf000);

	load_program_from_file("abc_300R.bin", 4096, 0xf000);

	m[0xffc6] = 0x0;
        m[0xffc7] = 0x77;
        m[0xffc8] = 0x7c;
        m[0xffc9] = 0x39;
        m[0xffca] = 0x5e;
        m[0xffcb] = 0x79;
        m[0xffcc] = 0x71;
        m[0xffcd] = 0x3d;
        m[0xffce] = 0x3e;
        m[0xffcf] = 0x00;
        
	/* ABC Test */
	/*uint8_t test_program[] = {0x18, 0xA9, 0x41, 0x8D, 0x00, 0x8b, 0x69, 0x01, 0xC9, 0x5B, 0x90, 0xF7, 0x00};
        int size = 13;*/
	//0x50 is BVC
	//uint8_t test_program[] = {0x69, 0x01, 0xC9, 0x5b, 0x50, 0xfc, 0x00};
	//int size = 7;
	
	//LDA 0x1, STA RAM, ADC RAM, STA ZEROPAGE, ASL ZEROPAGE, ADC ZEROPAGE 
	//uint8_t test_program[] = {0xA9, 0x01, 0x8d, 0x00, 0x8c, 0x6d, 0x00, 0x8c, 0x8d, 0x01, 0x00, 0x0e, 0x01, 0x00, 0x6d, 0x01, 0x00};
	//int size = 17;
	
	//absolute ROM: 0xf000, absolute RAM: 0x8b80, zeropage: 0x0001
	
	/*A: uint8_t test_program[] = {0xA9, 0x01, 0x8d, 0x00, 0x8b, 0x8d, 0x01, 0x00,
	0x6d, 0x00, 0xf0, 0x6d, 0x80, 0x8b, 0x6d, 0x01, 0x00,
	0x2d, 0x00, 0xf0, 0x2d, 0x80, 0x8b, 0x2d, 0x01, 0x00,
	0x0e, 0x80, 0x8b, 0x0e, 0x01, 0x00};
       	int size = 32;*/

	/* C */
	/*uint8_t test_program[] = {0xA9, 0x01, 0x8d, 0x00, 0x8b, 0x8d, 0x01, 0x00,
		0xcd, 0x00, 0xf0, 0xcd, 0x80, 0x8b, 0xcd, 0x01, 0x00,
		0xec, 0x00, 0xf0, 0xec, 0x80, 0x8b, 0xec, 0x01, 0x00,
		0xcc, 0x00, 0xf0, 0xcc, 0x80, 0x8b, 0xcc, 0x01, 0x00};
	int size = 35;*/

	/* D / E / I */
	/*uint8_t test_program[] = {0xA9, 0x01, 0x8d, 0x00, 0x8b, 0x8d, 0x01, 0x00,
		0xce, 0x00, 0xf0, 0xce, 0x80, 0x8b, 0xce, 0x01, 0x00,
		0x4d, 0x00, 0xf0, 0x4d, 0x80, 0x8b, 0x4d, 0x01, 0x00,
		0xee, 0x00, 0xf0, 0xee, 0x80, 0x8b, 0xee, 0x01, 0x00};
	int size = 35;*/

	/* L / O */
	/*uint8_t test_program[] = {0xA9, 0x01, 0x8d, 0x00, 0x8b, 0x8d, 0x01, 0x00,
		0xad, 0x00, 0xf0, 0xad, 0x80, 0x8b, 0xad, 0x01, 0x00,
		0xae, 0x00, 0xf0, 0xae, 0x80, 0x8b, 0xae, 0x01, 0x00,
		0xac, 0x00, 0xf0, 0xac, 0x80, 0x8b, 0xac, 0x01, 0x00,
		0x4e, 0x00, 0xf0, 0x4e, 0x80, 0x8b, 0x4e, 0x01, 0x00,
		0x0d, 0x00, 0xf0, 0x0d, 0x80, 0x8b, 0x0d, 0x01, 0x00};
	int size = 53;*/

	/* R / S */
	/*uint8_t test_program[] = {0xA9, 0x01, 0x8d, 0x00, 0x8b, 0x8d, 0x01, 0x00,
		0x2e, 0x00, 0xf0, 0x2e, 0x80, 0x8b, 0x2e, 0x01, 0x00,
		0x6e, 0x00, 0xf0, 0x6e, 0x80, 0x8b, 0x6e, 0x01, 0x00,
		0xed, 0x00, 0xf0, 0xed, 0x80, 0x8b, 0xed, 0x01, 0x00,
		0x8e, 0x00, 0xf0, 0x8e, 0x80, 0x8b, 0x8e, 0x01, 0x00,
		0x8c, 0x00, 0xf0, 0x8c, 0x80, 0x8b, 0x8c, 0x01, 0x00};
	int size = 53;*/

	//TODO BIT
/*	uint8_t test_program[] = {0x65, 0x01, 0x25, 0x01, 0x06, 0x1, 0xc5, 0x01, 0xe4, 0x01, 0xc4, 0x01, 0xc6, 0x1, 0x45, 0x1, 0xe6, 0x1,
	0xa5, 0x01, 0xa6, 0x01, 0xa4, 0x01, 0x46, 0x1, 0x5, 0x1, 0x26, 0x1, 0x66, 0x01, 0xe5, 0x1, 0x85, 0x1, 0x86, 0x1, 0x84, 0x1};
	int size = 40;*/

/*	uint8_t test_program[] = {0x69, 0x01, 0x65, 0x01, 0x75, 0x1, 0x6d, 0x80, 0x8b, 0x7d, 0x10, 0xf0, 0x79, 0x80, 0x8b};
        int size = 15; ADC all Addressing Modes*/

	/*uint8_t test_program[] = {0x20, 0xdc, 0xf6, 0x4c, 0x7f, 0xf6, 0xa2, 0x06, 0x86, 0x48, 0x60};
	int size = 11;
	load_into_memory(test_program, size, 0xf6d6);*/

	/* analyse binary */
	uint16_t lastPC = find_leaders_and_branches();

	complete_block();

	analyse_uses_and_defs_code_block();

	compute_optimization();

	//printf("start: %x, end: %x\n", startBlock, endBlock);

	print_code(lastPC);

	return 0;
}
