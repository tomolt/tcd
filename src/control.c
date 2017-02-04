#include "tcd.h"

#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/reg.h>
#include <sys/wait.h>

const size_t WORD_SIZE = sizeof(void*);

void tcdSync(TcdContext *debug) {
	waitpid(debug->pid, &debug->status, 0);
}

void tcdReadMemory(TcdContext *debug, uint64_t address, uint32_t size, void *data) {
	uint8_t *bytes = data;
	uint32_t read = 0;
	while (size - read >= WORD_SIZE) {
		long word = ptrace(PTRACE_PEEKDATA, debug->pid, address + read, NULL);
		memcpy(bytes + read, &word, WORD_SIZE);
		read += WORD_SIZE;
	}
	if (size - read > 0) {
		long word = ptrace(PTRACE_PEEKDATA, debug->pid, address + read, NULL);
		memcpy(bytes + read, &word, size - read);
	}
}

void tcdWriteMemory(TcdContext *debug, uint64_t address, uint32_t size, void *data) {
	uint8_t *bytes = data;
	uint32_t i = 0;
	while (size - i >= WORD_SIZE) {
		long word = 0;
		for (uint32_t b = 0; b < WORD_SIZE; b++) {
			word |= bytes[i + b] << (b * 8);
		}
		ptrace(PTRACE_POKEDATA, debug->pid, address + i, (void*)word);
		i += WORD_SIZE;
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
	/* TODO */
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
			memcpy(data, &rtloc.address, size <= 8 ? size : 8);
			break;
	}
}

void tcdStepInstruction(TcdContext *debug) {
	ptrace(PTRACE_SINGLESTEP, debug->pid, NULL, NULL);
}

uint64_t tcdStep(TcdContext *debug) {
	uint64_t ip = tcdReadIP(debug);
	TcdFunction *func = tcdSurroundingFunction(&debug->info, ip);
	do {
		tcdStepInstruction(debug);
		tcdSync(debug);
		ip = tcdReadIP(debug);
		if (func == NULL || !(ip >= func->begin && ip <= func->end)) {
			func = tcdSurroundingFunction(&debug->info, ip);
		}
		if (func != NULL) {
			TcdLine *line = tcdNearestLine(func, ip);
			if (line != NULL && line->address == ip)
				break;
		}
	} while (WIFSTOPPED(debug->status));
	return ip;
}

uint64_t tcdNext(TcdContext *debug) {
	uint64_t level = tcdReadBP(debug);
	uint64_t ip = tcdReadIP(debug);
	TcdFunction *func = tcdSurroundingFunction(&debug->info, ip);
	do {
		tcdStepInstruction(debug);
		tcdSync(debug);
		uint64_t bp = tcdReadBP(debug);
		if (bp >= level) {
			ip = tcdReadIP(debug);
			if (func == NULL || !(ip >= func->begin && ip <= func->end)) {
				func = tcdSurroundingFunction(&debug->info, ip);
			}
			if (func != NULL) {
				TcdLine *line = tcdNearestLine(func, ip);
				if (line != NULL && line->address == ip)
					break;
			}
		}
	} while (WIFSTOPPED(debug->status));
	return ip;
}

uint16_t tcdGetStackTrace(TcdContext *debug, uint64_t *trace, uint16_t max) {
	TcdFunction *fmain = tcdFunctionByName(&debug->info, "main");
	if (fmain == NULL) return 0;
	uint64_t address   = tcdReadIP(debug);
	uint64_t framebase = tcdReadBP(debug);
	uint16_t level = 0;
	while (level < max) {
		trace[level] = address;
		level++;
		if (address >= fmain->begin && address < fmain->end)
			break;
		uint64_t ufb;
		tcdReadMemory(debug, framebase    , 8, &ufb);
		tcdReadMemory(debug, framebase + 8, 8, &address);
		framebase = ufb;
	}
	return level;
}

void tcdInsertBreakpoint(TcdContext *debug, uint64_t address, uint32_t line) {
	/* Insert new break point */
	TcdBreakpoint point;
	point.address = address;
	point.line = line;
	/* Save instruction */
	tcdReadMemory(debug, address, 1, &point.saved);
	/* Insert break point */
	uint8_t int3 = 0xCC;
	tcdWriteMemory(debug, address, 1, &int3);
	/* Store break point */
	debug->breaks = realloc(debug->breaks, ++debug->numBreaks * sizeof(*debug->breaks));
	debug->breaks[debug->numBreaks - 1] = point;
}
