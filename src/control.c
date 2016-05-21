#include "tcd.h"

#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/reg.h>
#include <sys/wait.h>

void tcdSync(TcdContext *debug) {
	waitpid(debug->pid, &debug->status, 0);
}

void tcdReadMemory(TcdContext *debug, uint64_t address, uint32_t size, void *data) {
	uint8_t *bytes = data;
	uint32_t i = 0;
	while (size - i >= sizeof(void*)) {
		long word = ptrace(PTRACE_PEEKDATA, debug->pid, address + i, NULL);
		for (uint32_t b = 0; b < sizeof(void*); b++) {
			bytes[i + b] = (word >> (b * 8)) & 0xFF;
		}
		i += sizeof(void*);
	}
	if (size - i > 0) {
		long word = ptrace(PTRACE_PEEKDATA, debug->pid, address + i, NULL);
		for (uint32_t b = 0; b < size - i; b++) {
			bytes[i + b] = (word >> (b * 8)) & 0xFF;
		}
	}
}

void tcdWriteMemory(TcdContext *debug, uint64_t address, uint32_t size, void *data) {
	uint8_t *bytes = data;
	uint32_t i = 0;
	while (size - i >= sizeof(void*)) {
		long word = 0;
		for (uint32_t b = 0; b < sizeof(void*); b++) {
			word |= bytes[i + b] << (b * 8);
		}
		ptrace(PTRACE_POKEDATA, debug->pid, address + i, (void*)word);
		i += sizeof(void*);
	}
	if (size - i > 0) {
		long word = ptrace(PTRACE_PEEKDATA, debug->pid, address + i, NULL);
		for (uint32_t b = 0; b < size - i; b++) {
			word &= ~(0xFF << (b * 8));
			word |= bytes[i + b] << (b * 8);
		}
		ptrace(PTRACE_POKEDATA, debug->pid, address + i, (void*)word);
	}
}

void tcdReadRegisters(TcdContext *debug) {

}

uint64_t tcdReadIP(TcdContext *debug) {
	return ptrace(PTRACE_PEEKUSER, debug->pid, 8 * RIP, NULL);
}

uint64_t tcdReadBP(TcdContext *debug) {
	return ptrace(PTRACE_PEEKUSER, debug->pid, 8 * RBP, NULL);
}

void tcdReadRtLoc(TcdContext *debug, TcdRtLoc rtloc, uint32_t size, void *data) {
	switch (rtloc.region) {
		case TCDR_ADDRESS:
			tcdReadMemory(debug, rtloc.address, size, data);
			break;
		case TCDR_REGISTER:
			/* TODO implement */
			memset(data, 0, size);
			break;
		case TCDR_HOST_TEMP:
			memcpy(data, &rtloc.address, size < 8 ? size : 8);
			break;
	}
}

void tcdStepInstruction(TcdContext *debug) {
	ptrace(PTRACE_SINGLESTEP, debug->pid, NULL, NULL);
}

uint64_t tcdStep(TcdContext *debug) {
	uint64_t rip = tcdReadIP(debug);
	TcdFunction *func = tcdSurroundingFunction(&debug->info, rip);
	do {
		tcdStepInstruction(debug);
		tcdSync(debug);
		rip = tcdReadIP(debug);
		if (func == NULL || !(rip >= func->begin && rip <= func->end)) {
			func = tcdSurroundingFunction(&debug->info, rip);
		}
		if (func != NULL) {
			TcdLine *line = tcdNearestLine(func, rip);
			if (line != NULL && line->address == rip)
				break;
		}
	} while (WIFSTOPPED(debug->status));
	return rip;
}

uint64_t tcdNext(TcdContext *debug) {
	uint64_t level = tcdReadBP(debug);
	uint64_t rip = tcdReadIP(debug);
	TcdFunction *func = tcdSurroundingFunction(&debug->info, rip);
	do {
		tcdStepInstruction(debug);
		tcdSync(debug);
		uint64_t rbp = tcdReadBP(debug);
		if (rbp >= level) {
			rip = tcdReadIP(debug);
			if (func == NULL || !(rip >= func->begin && rip <= func->end)) {
				func = tcdSurroundingFunction(&debug->info, rip);
			}
			if (func != NULL) {
				TcdLine *line = tcdNearestLine(func, rip);
				if (line != NULL && line->address == rip)
					break;
			}
		}
	} while (WIFSTOPPED(debug->status));
	return rip;
}

uint16_t tcdGetStackTrace(TcdContext *debug, uint64_t *trace, uint16_t max) {
	TcdFunction *fmain = tcdFunctionByName(&debug->info, "main");
	if (fmain == NULL) return 0;
	uint64_t address   = tcdReadIP(debug);
	uint64_t framebase = tcdReadBP(debug);
	uint64_t *cur = trace;
	uint16_t level = 0;
	while (level < max) {
		*cur = address;
		level++;
		if (address >= fmain->begin && address < fmain->end)
			break;
		uint64_t ufb;
		tcdReadMemory(debug, framebase    , 8, &ufb);
		tcdReadMemory(debug, framebase + 8, 8, &address);
		framebase = ufb;
		cur++;
	}
	return level;
}
