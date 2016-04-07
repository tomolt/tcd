#include "tcd.h"

#include <stdlib.h>
#include <string.h>

TcdCompUnit *tcdSurroundingCompUnit(TcdInfo *info, uint64_t address) {
	for (uint32_t u = 0; u < info->numCompUnits; u++) {
		TcdCompUnit *cu = info->compUnits + u;
		if (address >= cu->begin && address <= cu->end) {
			return cu;
		}
	}
	return NULL;
}

TcdFunction *tcdSurroundingFunction(TcdInfo *info, uint64_t address) {
	TcdCompUnit *cu = tcdSurroundingCompUnit(info, address);
	if (cu == NULL) return NULL;
	for (uint32_t i = 0; i < cu->numFuncs; i++) {
		if (address >= cu->funcs[i].begin && address < cu->funcs[i].end) {
			return cu->funcs + i;
		}
	}
	return NULL;
}

TcdFunction *tcdFunctionByName(TcdInfo *info, char *name) {
	for (uint32_t u = 0; u < info->numCompUnits; u++) {
		TcdCompUnit *cu = info->compUnits + u;
		for (uint32_t i = 0; i < cu->numFuncs; i++) {
			if (strcmp(cu->funcs[i].name, name) == 0) {
				return cu->funcs + i;
			}
		}
	}
	return NULL;
}

/* TODO Check max bounds! */
TcdLine *tcdNearestLine(TcdFunction *func, uint64_t address) {
	TcdLine *line = NULL;
	for (uint32_t i = 0; i < func->numLines; i++) {
		if (func->lines[i].address > address) break;
		if (line == NULL || func->lines[i].address > line->address) {
			line = func->lines + i;
		}
	}
	return line;
}

void tcdFreeInfo(TcdInfo *info) {
	for (uint32_t u = 0; u < info->numCompUnits; u++) {
		TcdCompUnit *cu = info->compUnits + u;
		for (uint32_t i = 0; i < cu->numFuncs; i++) {
			TcdFunction *func = cu->funcs + i;
			free(func->name);
			for (uint32_t j = 0; j < func->numLocals; j++) {
				free(func->locals[j].name);
				free(func->locals[j].exprloc);
			}
			free(func->locals);
			free(func->lines);
		}
		free(cu->funcs);
		free(cu->name);
		free(cu->compDir);
		free(cu->producer);
	}
	free(info->compUnits);
}

void tcdFreeContext(TcdContext *debug) {
	tcdFreeInfo(&debug->info);
	free(debug->breaks);
}
