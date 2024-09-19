#include <stdint.h>


typedef struct{
        //opcode of instruction
        uint8_t opcode;
        //address of Instruction
        uint16_t address;
        //parameter of instruction or label
        uint16_t parameter;
} BinaryInstruction;

struct Instructions{
        //opcode
        void (*opcode)(void);
        //addressing mode
        uint8_t addressingMode;
        //clock cycles needed
        int cycles;
};

void
call_corresponding_addressingMode(uint8_t opcode);

void print_prolog(void);
void print_global_vars_and_functions(void);
void print_main(void);
void print_epilog(void);
/*void
convert_number_to_bcd(void);
void
convert_bcd_to_number(void);
*/
/* Instructions */

void ADC(void);

void AND(void);

void ASL(void);

void BCC(void);
void BCS(void);

void BEQ(void);
void BIT(void);

void BMI(void);
void BNE(void);

void BPL(void);

void BRK(void);

void BVC(void);

void BVS(void);
void CLC(void);
void CLD(void);

void CLI(void);

void CLV(void);

void CMP(void);

void CPX(void);

void CPY(void);
void DEC(void);
void DEX(void);

void DEY(void);

void EOR(void);

void INC(void);

void INX(void);

void INY(void);

void JMP(void);

void JSR(void);

void LDA(void);

void LDX(void);

void LDY(void);
void LSR(void);

void NOP(void);
void ORA(void);

void PHA(void);
void PHP(void);

void PLA(void);

void PLP(void);

void ROL(void);

void ROR(void);

void RTI(void);
void RTS(void);

void SBC(void);

void SEC(void);

void SED(void);

void SEI(void);


void TAX(void);

void TAY(void);

void TSX(void);

void TXA(void);

void TXS(void);

void TYA(void);
/* "Illegal" Opcodes */

void ALR(void);

void ANC(void);

void ANC2(void);

void ARR(void);
void DCP(void);

void ISC(void);

void LAS(void);

void LAX(void);

void RLA(void);

void RRA(void);

void SAX(void);

void SBX(void);

void SLO(void);

void SRE(void);

void USBC(void);

void JAM(void);

/* Instructions */
void STA(void);

void STX(void);

void STY(void);

/* Addressing Modes */

void absolute(void);

void absolute_x (void);

void absolute_y(void);

void immediate(void);

void implied(void);

void indirect(void);

void indirect_y(void);

void indexed_x(void);

void relative(void);

void zeropage(void);
void zeropage_x(void);

void zeropage_y(void);
