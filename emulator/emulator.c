#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
	ProcessInstructionIdAdd=0,
	ProcessInstructionIdSub=1,
	ProcessInstructionIdXor=2,
	ProcessInstructionIdOr=3,
	ProcessInstructionIdAnd=4,
	ProcessInstructionIdNot=5,
	ProcessInstructionIdCmp=6,
	ProcessInstructionIdShiftLeft=7,
	ProcessInstructionIdShiftRight=8,

	ProcessInstructionIdStore=9,
	ProcessInstructionIdLoad=10,
	ProcessInstructionIdLoadProgmem=11,

	ProcessInstructionIdCopy=12,
	ProcessInstructionIdSet=13,

	ProcessInstructionIdSyscall=14,
} ProcessInstructionId;

#define ProcessInstructionIdBits 5
#define ProcessInstructionIdShift (16-(ProcessInstructionIdBits))
#define ProcessInstructionGetOperandN(instructionFull, n) (((instructionFull)>>(8-3*(n)))&7)

typedef struct {
	uint16_t ip;
	uint16_t regs[8];

	uint8_t progmem[65536];
	uint8_t ram[65536];
} Process;

Process *process=NULL;
bool verbose=false;

bool processRunNextInstruction(Process *process);
void processDebug(const Process *process);

int main(int argc, char **argv) {
	FILE *inputFile=NULL;

	// Parse arguments
	if (argc!=2 && argc!=3) {
		printf("Usage: %s inputfile [--verbose]\n", argv[0]);
		goto done;
	}

	const char *inputPath=argv[1];
	verbose=(argc>2 && strcmp(argv[2], "--verbose")==0);

	// Allocate process data struct
	process=malloc(sizeof(Process));
	if (process==NULL) {
		printf("Could not allocate process data struct\n");
		goto done;
	}

	process->ip=0;

	// Read-in input file
	inputFile=fopen(inputPath, "r");
	if (inputFile==NULL) {
		printf("Could not open input file '%s' for reading\n", inputPath);
		goto done;
	}

	int c;
	uint8_t *next=process->progmem;
	while((c=fgetc(inputFile))!=EOF)
		*next++=c;

	// Run process
	do {
		if (verbose)
			processDebug(process);
	} while(processRunNextInstruction(process));

	// Done
	done:
	if (inputFile!=NULL)
		fclose(inputFile);
	free(process);

	return 0;
}

bool processRunNextInstruction(Process *process) {
	uint16_t instructionFull=process->progmem[process->ip++];
	instructionFull<<=8;
	instructionFull|=process->progmem[process->ip++];

	uint16_t instructionId=(instructionFull >> ProcessInstructionIdShift) & (((1u)<<ProcessInstructionIdBits)-1);

	switch(instructionId) {
		case ProcessInstructionIdAdd:
			// TODO: this
			return false;
		break;
		case ProcessInstructionIdSub:
			// TODO: this
			return false;
		break;
		case ProcessInstructionIdXor:
			// TODO: this
			return false;
		break;
		case ProcessInstructionIdOr:
			// TODO: this
			return false;
		break;
		case ProcessInstructionIdAnd:
			// TODO: this
			return false;
		break;
		case ProcessInstructionIdNot:
			// TODO: this
			return false;
		break;
		case ProcessInstructionIdCmp:
			// TODO: this
			return false;
		break;
		case ProcessInstructionIdShiftLeft:
			// TODO: this
			return false;
		break;
		case ProcessInstructionIdShiftRight:
			// TODO: this
			return false;
		break;
		case ProcessInstructionIdStore: {
			int addrReg=ProcessInstructionGetOperandN(instructionFull, 0);
			int valueReg=ProcessInstructionGetOperandN(instructionFull, 1);

			process->ram[process->regs[addrReg]]=process->regs[valueReg];

			if (verbose)
				printf("Set ram at addr stored in r%i to value in r%i (ram[%u]=%u)\n", addrReg, valueReg, process->regs[addrReg], process->regs[valueReg]);
		} break;
		case ProcessInstructionIdLoad: {
			int addrReg=ProcessInstructionGetOperandN(instructionFull, 0);
			int valueReg=ProcessInstructionGetOperandN(instructionFull, 1);

			process->regs[valueReg]=process->ram[addrReg];

			if (verbose)
				printf("Load from ram at addr stored in r%i to reg r%i\n", addrReg, valueReg);
		} break;
		case ProcessInstructionIdCopy: {
			int destReg=ProcessInstructionGetOperandN(instructionFull, 0);
			int srcReg=ProcessInstructionGetOperandN(instructionFull, 1);

			process->regs[destReg]=process->regs[srcReg];

			if (verbose)
				printf("Copy r%i to reg r%i (r%i=%u)\n", srcReg, destReg, destReg, process->regs[srcReg]);
		} break;
		case ProcessInstructionIdSet: {
			int destReg=ProcessInstructionGetOperandN(instructionFull, 0);
			int value=(instructionFull & 255);

			process->regs[destReg]=value;

			if (verbose)
				printf("Set r%i to %u\n", destReg, value);
		} break;
		case ProcessInstructionIdSyscall: {
			uint16_t syscallId=process->regs[0];
			switch(syscallId) {
				case 1:
					// write progmem

					if (verbose) {
						printf("Syscall (%i - writeprogmem, fd=%u, data=%u [", syscallId, process->regs[1], process->regs[2]);
						for(int i=0; i<process->regs[3]; ++i)
							printf("%c", process->progmem[process->regs[2]+i]);
						printf("], len=%u)\n", process->regs[3]);
					}

					for(int i=0; i<process->regs[3]; ++i)
						printf("%c", process->progmem[process->regs[2]+i]);
				break;
				case 2:
					if (verbose)
						printf("Syscall (%i - exit, status=%u)\n", syscallId, process->regs[1]);
					return false;
				break;
				default:
					if (verbose)
						printf("Syscall (%i - unknown)\n", syscallId);
					return false;
				break;
			}
		} break;
		default:
			return false;
		break;
	}

	return true;
}

void processDebug(const Process *process) {
	printf("Process %p:\n", process);
	printf("	IP: %u\n", process->ip);
	printf("	Regs:");
	for(int i=0; i<8; ++i)
		printf(" r%i=%u", i, process->regs[i]);
	printf("\n");
}
